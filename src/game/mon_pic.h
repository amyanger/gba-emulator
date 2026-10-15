#ifndef MON_PIC_H
#define MON_PIC_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/emerald_profile.h"

/* Front sprites straight from the ROM tables (still-frame sheet, one 64x64 frame).
 * Unown shows form A, Castform its normal form, Spinda without spots (the game draws
 * those at runtime). */
#define MON_PIC_W 64
#define MON_PIC_PIXELS (64 * 64)
#define MON_PIC_SPECIES_EGG 412

/* LZ77 (BIOS type 0x10). Decodes the first need bytes into out. False when the
 * header type isn't 0x10, the declared size < need, a back-reference points
 * before the start, or the stream runs past the ROM. */
bool mon_pic_lz77(const GameMem* m, uint32_t addr, uint8_t* out, uint32_t need);

/* 64x64 ARGB8888 front sprite, color 0 transparent (alpha 0). species 1..411;
 * egg ignores species. False -> caller draws a placeholder. */
bool mon_pic_decode(const GameMem* m, const GameProfile* p, uint16_t species, bool egg,
                    bool shiny, uint32_t out[MON_PIC_PIXELS]);

#endif
