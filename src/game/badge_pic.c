#include <string.h>
#include "game/badge_pic.h"
#include "game/mon_pic.h"

#define SHEET_BYTES 0x400

void badge_pic_load(const GameMem* m, const GameProfile* p, BadgeArt* out) {
    static uint8_t tiles[SHEET_BYTES];

    memset(out, 0, sizeof(*out));
    if (!p || !p->badge_gfx || !mon_pic_lz77(m, p->badge_gfx, tiles, SHEET_BYTES)) return;

    for (int i = 0; i < 8; i++) {
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                uint32_t t = (uint32_t)((y / 8) * 16 + 2 * i + x / 8);
                uint8_t b = tiles[t * 32 + (uint32_t)(y % 8) * 4 + (uint32_t)(x % 8) / 2];
                out->px[i][y * 16 + x] = (x & 1) ? (uint8_t)(b >> 4) : (uint8_t)(b & 15);
            }
        }
    }
    out->ok = true;
}
