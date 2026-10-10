#ifndef CHEAT_H
#define CHEAT_H

#include <stddef.h>
#include "common.h"
#include "cheat/cheat_types.h"

typedef struct Bus Bus;

/* A named cheat. The raw lines are what was pasted (and what the .cht file
 * stores); ops are decoded from them once, so a decoder fix re-applies to
 * saved cheats on the next load. */
typedef struct {
    char name[CHEAT_NAME_LEN];
    CheatDevice device;
    bool device_override;    /* the user picked the device */
    bool enabled;
    bool broken;             /* lines did not decode: kept for the file, never run */
    bool confident;          /* detection was sure (always true when overridden) */
    CheatRawLine lines[CHEAT_MAX_LINES];
    uint8_t line_count;
    CheatOp ops[CHEAT_MAX_OPS];
    uint8_t op_count;
    uint8_t markers, unsupported;
    char error[96];          /* why it is broken */
} CheatEntry;

typedef struct {
    CheatEntry cheats[CHEAT_MAX_CHEATS];
    uint32_t cheat_count;
    uint32_t game_code;      /* ROM header word at 0xAC, for game ID lines */
} CheatEngine;

void cheat_init(CheatEngine* engine);
void cheat_set_game_code(CheatEngine* engine, uint32_t game_code);

/* Parses pasted text, detects the device (unless dev is not CHEAT_DEV_AUTO),
 * decodes, and appends an enabled cheat. name may be NULL to use the paste's
 * label. Returns the index, or -1 with err explaining why (bad paste, unsure
 * device, codes that don't decode, engine full). */
int32_t cheat_add_text(CheatEngine* engine, const char* text, const char* name, CheatDevice dev,
                       char* err, size_t err_len);

/* Appends already-parsed lines (the .cht loader). Undecodable cheats are kept
 * as broken so saving the file never drops them. Returns index or -1 if full. */
int32_t cheat_add_raw(CheatEngine* engine, const char* name, const CheatRawList* raw, CheatDevice dev,
                      bool override, bool enabled);

/* Re-decodes with a forced device. On failure the cheat is left unchanged. */
bool cheat_set_device(CheatEngine* engine, uint32_t index, CheatDevice dev);

void cheat_remove(CheatEngine* engine, uint32_t index);
void cheat_toggle(CheatEngine* engine, uint32_t index);
void cheat_clear_all(CheatEngine* engine);
bool cheat_any_enabled(const CheatEngine* engine);

/* Runs every enabled cheat once (called at VBlank start). RAM is read and
 * written directly, without bus timing; palette, VRAM, OAM and I/O go through
 * the bus so their side effects happen. */
void cheat_apply(CheatEngine* engine, Bus* bus);

#endif // CHEAT_H
