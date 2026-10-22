#ifndef RANDO_H
#define RANDO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/game_profile.h"

/* Opt-in Emerald randomizer: rewrites starters, wild slots and trainer parties in the
 * in-memory ROM copy at load (docs/superpowers/specs/2026-10-22-randomizer-design.md).
 * The seed lives in <save path>.rando as one line "seed XXXXXXXX v1". */

#define RANDO_FILE_EXT ".rando"

/* Emerald profile with starter_mons set. */
bool rando_supported(const GameProfile* p);
/* No-op unless rando_supported(p). Idempotent: every field rolls from seed and its own offset. */
void rando_apply(uint8_t* rom, uint32_t rom_size, const GameProfile* p, uint32_t seed);

/* "seed 7F3A91C2 v1" (trailing whitespace allowed) -> true; anything else false. */
bool rando_parse(const char* text, uint32_t* seed);
/* Writes "seed %08X v1\n". */
void rando_format(char* out, size_t n, uint32_t seed);

/* <save_path>.rando: load reads and parses it, store writes it, clear removes it (true if gone). */
bool rando_load(const char* save_path, uint32_t* seed);
bool rando_store(const char* save_path, uint32_t seed);
bool rando_clear(const char* save_path);

#endif // RANDO_H
