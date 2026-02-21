#include "ppu.h"

void ppu_init(PPU* ppu) {
    memset(ppu, 0, sizeof(PPU));
    // Don't zero the memory pointers — they'll be set by gba_init
}

void ppu_render_scanline(PPU* ppu) {
    uint8_t mode = ppu->dispcnt & 0x7;
    uint16_t line = ppu->vcount;

    if (line >= VDRAW_LINES) return;

    // Clear scanline buffer
    memset(ppu->scanline_buffer, 0, sizeof(ppu->scanline_buffer));

    switch (mode) {
    case 0:
        // TODO: Mode 0 — four regular tiled BG layers
        break;
    case 1:
        // TODO: Mode 1 — two regular + one affine BG
        break;
    case 2:
        // TODO: Mode 2 — two affine BG layers
        break;
    case 3:
        // TODO: Mode 3 — 240x160 16bpp bitmap
        break;
    case 4:
        // TODO: Mode 4 — 240x160 8bpp indexed bitmap (page flipping)
        break;
    case 5:
        // TODO: Mode 5 — 160x128 16bpp bitmap (page flipping)
        break;
    }

    // TODO: Render sprites (OBJ layer)
    // TODO: Composite layers with priority, windowing, blending

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
