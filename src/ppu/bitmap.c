#include "ppu.h"

// Mode 3: 240x160 direct color (16bpp)
void ppu_render_mode3(PPU* ppu) {
    // TODO: Read directly from VRAM as 16-bit color values
    // Each pixel is a 15-bit BGR value at VRAM[y * 240 + x] * 2
}

// Mode 4: 240x160 indexed color (8bpp) with page flipping
void ppu_render_mode4(PPU* ppu) {
    // TODO: Read palette index from VRAM, look up color in palette RAM
    // Page select via DISPCNT bit 4 (0 = 0x06000000, 1 = 0x0600A000)
}

// Mode 5: 160x128 direct color (16bpp) with page flipping
void ppu_render_mode5(PPU* ppu) {
    // TODO: Similar to mode 3 but smaller resolution with page flipping
}
