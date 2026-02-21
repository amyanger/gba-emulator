#include "ppu.h"

// Apply color blending/special effects between layers
void ppu_apply_blending(PPU* ppu, uint16_t* top_pixel, uint16_t* bot_pixel,
                        uint16_t* result) {
    // TODO: Implement based on BLDCNT register
    // Mode 0: No blending
    // Mode 1: Alpha blend (EVA * top + EVB * bottom) from BLDALPHA
    // Mode 2: Brightness increase (white fade) from BLDY
    // Mode 3: Brightness decrease (black fade) from BLDY
}

// Determine which layers are visible in each window region
void ppu_apply_windowing(PPU* ppu, int x) {
    // TODO: Check if pixel (x, vcount) is inside WIN0, WIN1, or OBJ window
    // Return the layer enable mask for that window region
    // Fallback to WINOUT if not in any window
}

// Apply mosaic effect to background or sprite scanline
void ppu_apply_mosaic(PPU* ppu, uint16_t* scanline, int width, bool is_obj) {
    // TODO: Group pixels into mosaic_h x mosaic_v blocks
    // All pixels in a block take the color of the top-left pixel
}
