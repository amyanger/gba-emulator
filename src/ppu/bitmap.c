#include "ppu.h"

// Mode 3: 240x160 direct color (16bpp)
// VRAM layout: each pixel is a 15-bit BGR color stored as a little-endian
// uint16_t at address (y * 240 + x) * 2. Only BG2 is usable in this mode.
void ppu_render_mode3(PPU* ppu) {
    // BG2 enable is bit 10 of DISPCNT. If it's off, leave the scanline
    // at the backdrop color (already filled by ppu_render_scanline).
    if (!BIT(ppu->dispcnt, 10)) return;

    uint32_t y = ppu->vcount;
    uint32_t base = y * SCREEN_WIDTH * 2;  // 2 bytes per pixel

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        uint32_t offset = base + x * 2;
        // Read 16-bit little-endian color directly from VRAM
        uint16_t color = (uint16_t)ppu->vram[offset]
                       | ((uint16_t)ppu->vram[offset + 1] << 8);
        // Track layers for blending: bitmap modes use BG2 (layer 2)
        ppu->second_pixel[x] = ppu->scanline_buffer[x];
        ppu->second_layer[x] = ppu->top_layer[x];
        ppu->scanline_buffer[x] = color;
        ppu->top_layer[x] = 2;
    }
}

// Mode 4: 240x160 indexed color (8bpp) with page flipping
// Each pixel is a 1-byte palette index. The actual color is looked up
// from palette RAM. DISPCNT bit 4 selects the page (0 or 1).
void ppu_render_mode4(PPU* ppu) {
    if (!BIT(ppu->dispcnt, 10)) return;

    // Page select: bit 4 of DISPCNT. Page 0 starts at 0x0000, page 1 at 0xA000.
    uint32_t page_base = BIT(ppu->dispcnt, 4) ? 0xA000 : 0x0000;
    uint32_t y = ppu->vcount;
    uint32_t row_offset = page_base + y * SCREEN_WIDTH;  // 1 byte per pixel

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        uint8_t palette_idx = ppu->vram[row_offset + x];
        // Each palette entry is 2 bytes (15-bit color) in palette RAM
        uint16_t color = (uint16_t)ppu->palette_ram[palette_idx * 2]
                       | ((uint16_t)ppu->palette_ram[palette_idx * 2 + 1] << 8);
        // Track layers for blending: bitmap modes use BG2 (layer 2)
        ppu->second_pixel[x] = ppu->scanline_buffer[x];
        ppu->second_layer[x] = ppu->top_layer[x];
        ppu->scanline_buffer[x] = color;
        ppu->top_layer[x] = 2;
    }
}

// Mode 5: 160x128 direct color (16bpp) with page flipping
// Like Mode 3 but smaller (160x128) and supports page flipping.
// Pixels outside the 160x128 area show the backdrop color.
void ppu_render_mode5(PPU* ppu) {
    if (!BIT(ppu->dispcnt, 10)) return;

    uint32_t y = ppu->vcount;
    // Mode 5 only renders 128 visible lines
    if (y >= 128) return;

    // Page select: bit 4 of DISPCNT. Page 0 at 0x0000, page 1 at 0xA000.
    uint32_t page_base = BIT(ppu->dispcnt, 4) ? 0xA000 : 0x0000;
    uint32_t base = page_base + y * 160 * 2;  // 160 pixels wide, 2 bytes each

    // Only 160 pixels wide — the rest stay as backdrop
    for (uint32_t x = 0; x < 160; x++) {
        uint32_t offset = base + x * 2;
        uint16_t color = (uint16_t)ppu->vram[offset]
                       | ((uint16_t)ppu->vram[offset + 1] << 8);
        // Track layers for blending: bitmap modes use BG2 (layer 2)
        ppu->second_pixel[x] = ppu->scanline_buffer[x];
        ppu->second_layer[x] = ppu->top_layer[x];
        ppu->scanline_buffer[x] = color;
        ppu->top_layer[x] = 2;
    }
}
