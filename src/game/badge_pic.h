#ifndef BADGE_PIC_H
#define BADGE_PIC_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/game_profile.h"

/* The eight 16x16 gym badges from the trainer card sheet in the ROM (128x16, 4bpp,
 * LZ77). px[i][y * 16 + x] is a palette index 0..15 (0 transparent), badge i in
 * FLAG_BADGE01..08 order. ok is false when the sheet couldn't be read. */
typedef struct {
    bool ok;
    uint8_t px[8][256];
} BadgeArt;

/* Fully overwrites out. ok false (px zeroed) when p is NULL, badge_gfx is 0 or the decode fails. */
void badge_pic_load(const GameMem* m, const GameProfile* p, BadgeArt* out);

#endif
