#ifndef CHEAT_DECODE_H
#define CHEAT_DECODE_H

#include "cheat/cheat_types.h"

/* Turns raw code lines into device-neutral ops (GBATEK "GBA Cheat Devices"
 * tables), and guesses the device for a pasted block. Pure, no memory access. */

typedef struct {
    uint32_t markers;        /* game ID, hook, key, end lines */
    uint32_t unsupported;    /* recognized but not run (ROM patch, device buttons, ...) */
    uint32_t weak;           /* ops random words decode into easily: never-true IFs, ELSE, ENDIF */
    bool id_found;           /* an "xxxxxxxx 001DC0DE" game ID line */
    uint32_t id_word;        /* its first word: the game code from ROM 0xAC */
    char fail[96];           /* why decoding failed, "" on success */
} CheatDecodeInfo;

typedef struct {
    CheatDevice device;      /* CHEAT_DEV_AUTO when nothing decodes */
    bool confident;
    bool id_match;           /* the block's game ID line matches this ROM */
} CheatDetect;

/* True if [addr, addr+width) is RAM, palette, VRAM, OAM or I/O. */
bool cheat_addr_allowed(uint32_t addr, uint32_t width);

bool cheat_decode(const CheatRawList* raw, CheatDevice dev, CheatOp* ops, uint32_t max_ops,
                  uint32_t* op_count, CheatDecodeInfo* info);

/* game_code: little-endian word at ROM 0xAC (0 if unknown). */
void cheat_detect(const CheatRawList* raw, uint32_t game_code, CheatDetect* out);

#endif // CHEAT_DECODE_H
