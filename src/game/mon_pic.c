#include "game/mon_pic.h"
#include <string.h>

#define PIC_BYTES 0x800u
#define PAL_BYTES 32u
#define ENTRY 8u

bool mon_pic_lz77(const GameMem* m, uint32_t addr, uint8_t* out, uint32_t need) {
    uint32_t header;
    if (!game_mem_read32(m, addr, &header) || (header & 0xFF) != 0x10 || (header >> 8) < need)
        return false;
    uint32_t src = addr + 4, n = 0;
    while (n < need) {
        uint8_t flags;
        if (!game_mem_read8(m, src++, &flags)) return false;
        for (int b = 0; b < 8 && n < need; b++) {
            if (flags & (0x80 >> b)) {
                uint8_t hi, lo;
                if (!game_mem_read8(m, src, &hi) || !game_mem_read8(m, src + 1, &lo)) return false;
                src += 2;
                uint32_t len = (uint32_t)(hi >> 4) + 3, dist = (((uint32_t)(hi & 0xF) << 8) | lo) + 1;
                if (dist > n) return false;
                for (uint32_t i = 0; i < len && n < need; i++, n++) out[n] = out[n - dist];
            } else {
                if (!game_mem_read8(m, src++, &out[n])) return false;
                n++;
            }
        }
    }
    return true;
}

static uint32_t argb555(uint16_t c) {
    uint32_t r = (c & 31u) * 255u / 31u, g = ((c >> 5) & 31u) * 255u / 31u,
             b = ((c >> 10) & 31u) * 255u / 31u;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

bool mon_pic_decode(const GameMem* m, const GameProfile* p, uint16_t species, bool egg, bool shiny,
                    uint32_t out[MON_PIC_PIXELS]) {
    static uint8_t tiles[PIC_BYTES];
    uint8_t pal[PAL_BYTES];
    uint32_t pic_ptr, pal_ptr, colors[16];
    if (egg) species = MON_PIC_SPECIES_EGG;
    else if (species == 0 || species >= MON_PIC_SPECIES_EGG) return false;
    uint32_t pal_table = shiny && !egg ? p->mon_shiny_palettes : p->mon_palettes;
    if (!p->mon_still_front_pics || !pal_table) return false;
    if (!game_mem_read32(m, p->mon_still_front_pics + ENTRY * species, &pic_ptr) ||
        !game_mem_read32(m, pal_table + ENTRY * species, &pal_ptr))
        return false;
    if (!mon_pic_lz77(m, pic_ptr, tiles, PIC_BYTES) || !mon_pic_lz77(m, pal_ptr, pal, PAL_BYTES))
        return false;
    for (int i = 0; i < 16; i++) colors[i] = argb555((uint16_t)(pal[2 * i] | pal[2 * i + 1] << 8));
    colors[0] = 0;
    for (int t = 0; t < 64; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                uint8_t byte = tiles[t * 32 + y * 4 + x / 2];
                uint8_t idx = (x & 1) ? byte >> 4 : byte & 15;
                out[((t / 8) * 8 + y) * MON_PIC_W + (t % 8) * 8 + x] = colors[idx];
            }
    return true;
}
