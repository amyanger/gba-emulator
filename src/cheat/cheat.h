#ifndef CHEAT_H
#define CHEAT_H

#include "common.h"

// Forward declarations
typedef struct Bus Bus;

// Limits (static allocation)
#define CHEAT_MAX_CHEATS      64    // Max named cheat groups
#define CHEAT_MAX_LINES       16    // Max code lines per cheat group
#define CHEAT_NAME_LEN        64    // Max name length per cheat

// Cheat code format
typedef enum {
    CHEAT_FORMAT_GAMESHARK,     // GameShark v1/v2 / Action Replay
    CHEAT_FORMAT_CODEBREAKER    // CodeBreaker / GameShark Advance
} CheatFormat;

// A single raw code line (one address/value pair)
typedef struct {
    uint32_t address;   // First 32 bits (encodes type + address)
    uint32_t value;     // Second 32 bits (value / parameter)
} CheatCode;

// A named cheat (e.g., "Infinite HP") consisting of 1+ code lines
typedef struct {
    char name[CHEAT_NAME_LEN];
    CheatFormat format;
    CheatCode codes[CHEAT_MAX_LINES];
    uint32_t code_count;
    bool enabled;
} CheatEntry;

// Top-level cheat engine state
typedef struct {
    CheatEntry cheats[CHEAT_MAX_CHEATS];
    uint32_t cheat_count;
} CheatEngine;

// Lifecycle
void cheat_init(CheatEngine* engine);

// Management — cheat_add returns index of new cheat, or -1 if full
int32_t cheat_add(CheatEngine* engine, const char* name,
                  CheatFormat format, const CheatCode* codes,
                  uint32_t code_count);
void cheat_remove(CheatEngine* engine, uint32_t index);
void cheat_toggle(CheatEngine* engine, uint32_t index);
void cheat_clear_all(CheatEngine* engine);

// Apply all enabled cheats (call once per frame at VBlank)
void cheat_apply(CheatEngine* engine, Bus* bus);

#endif // CHEAT_H
