#include "ppu.h"

void ppu_init(PPU* ppu) {
    memset(ppu, 0, sizeof(PPU));
    // Note: palette_ram/vram/oam pointers are zeroed here.
    // gba_init() must assign them AFTER calling ppu_init().
}

void ppu_render_scanline(PPU* ppu) {
    uint8_t mode = ppu->dispcnt & 0x7;
    uint16_t line = ppu->vcount;

    if (line >= VDRAW_LINES) return;

    // Forced blank (DISPCNT bit 7): display white when set
    if (BIT(ppu->dispcnt, 7)) {
        for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
            ppu->scanline_buffer[x] = 0x7FFF; // White (all RGB bits set)
        }
        memcpy(&ppu->framebuffer[line * SCREEN_WIDTH], ppu->scanline_buffer,
               SCREEN_WIDTH * sizeof(uint16_t));
        return;
    }

    // Fill scanline with backdrop color (palette RAM entry 0)
    uint16_t backdrop = (uint16_t)ppu->palette_ram[0]
                      | ((uint16_t)ppu->palette_ram[1] << 8);
    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        ppu->scanline_buffer[x] = backdrop;
    }

    switch (mode) {
    case 0:
        // Mode 0: four regular tiled BG layers + sprites.
        // Render back-to-front (priority 3 down to 0). Within each priority,
        // higher-numbered BGs render first so lower-numbered BGs overwrite them.
        // Sprites render after BGs at each priority so they appear on top.
        for (int prio = 3; prio >= 0; prio--) {
            for (int bg = 3; bg >= 0; bg--) {
                if (!BIT(ppu->dispcnt, 8 + bg)) continue;
                if ((ppu->bg_cnt[bg] & 3) != prio) continue;
                ppu_render_bg_regular(ppu, bg);
            }
            if (BIT(ppu->dispcnt, 12)) {
                ppu_render_sprites_at_priority(ppu, prio);
            }
        }
        break;
    case 1:
        // Mode 1: BG0, BG1 regular + BG2 affine. BG3 not available.
        for (int prio = 3; prio >= 0; prio--) {
            if (BIT(ppu->dispcnt, 10) && (ppu->bg_cnt[2] & 3) == prio) {
                ppu_render_bg_affine(ppu, 2);
            }
            for (int bg = 1; bg >= 0; bg--) {
                if (!BIT(ppu->dispcnt, 8 + bg)) continue;
                if ((ppu->bg_cnt[bg] & 3) != prio) continue;
                ppu_render_bg_regular(ppu, bg);
            }
            if (BIT(ppu->dispcnt, 12)) {
                ppu_render_sprites_at_priority(ppu, prio);
            }
        }
        break;
    case 2:
        // Mode 2: BG2, BG3 affine. BG0, BG1 not available.
        for (int prio = 3; prio >= 0; prio--) {
            for (int bg = 3; bg >= 2; bg--) {
                if (!BIT(ppu->dispcnt, 8 + bg)) continue;
                if ((ppu->bg_cnt[bg] & 3) != prio) continue;
                ppu_render_bg_affine(ppu, bg);
            }
            if (BIT(ppu->dispcnt, 12)) {
                ppu_render_sprites_at_priority(ppu, prio);
            }
        }
        break;
    case 3:
        ppu_render_mode3(ppu);
        if (BIT(ppu->dispcnt, 12)) ppu_render_sprites(ppu);
        break;
    case 4:
        ppu_render_mode4(ppu);
        if (BIT(ppu->dispcnt, 12)) ppu_render_sprites(ppu);
        break;
    case 5:
        ppu_render_mode5(ppu);
        if (BIT(ppu->dispcnt, 12)) ppu_render_sprites(ppu);
        break;
    }

    // Copy scanline to framebuffer
    memcpy(&ppu->framebuffer[line * SCREEN_WIDTH], ppu->scanline_buffer,
           SCREEN_WIDTH * sizeof(uint16_t));
}

void ppu_set_hblank(PPU* ppu, bool active) {
    if (active) {
        ppu->dispstat |= (1 << 1); // Set HBlank flag
    } else {
        ppu->dispstat &= ~(1 << 1);
    }
}

void ppu_set_vblank(PPU* ppu, bool active) {
    if (active) {
        ppu->dispstat |= (1 << 0); // Set VBlank flag
    } else {
        ppu->dispstat &= ~(1 << 0);
    }
}

void ppu_increment_vcount(PPU* ppu) {
    ppu->vcount++;
    if (ppu->vcount >= TOTAL_LINES) {
        ppu->vcount = 0;
    }
}

bool ppu_vcount_match(PPU* ppu) {
    uint8_t target = (ppu->dispstat >> 8) & 0xFF;
    bool match = (ppu->vcount == target);
    if (match) {
        ppu->dispstat |= (1 << 2); // Set VCount flag
    } else {
        ppu->dispstat &= ~(1 << 2);
    }
    return match && (ppu->dispstat & (1 << 5)); // Only fire if VCount IRQ enabled
}
