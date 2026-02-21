#include "ppu.h"

// Render a single scanline for a regular (non-affine) tiled background
void ppu_render_bg_regular(PPU* ppu, int bg_index) {
    // TODO: Implement regular BG rendering
    // 1. Read BGxCNT to get tile base, map base, palette mode, priority
    // 2. Calculate the tile row based on vcount + bg_vofs
    // 3. For each pixel in the scanline (0-239):
    //    a. Calculate tile column from pixel X + bg_hofs
    //    b. Read tile entry from screen block (map base)
    //    c. Read pixel from character block (tile base)
    //    d. Look up color from palette RAM
    //    e. Write to scanline buffer
}

// Render a single scanline for an affine (rotation/scaling) background
void ppu_render_bg_affine(PPU* ppu, int bg_index) {
    // TODO: Implement affine BG rendering
    // 1. Use internal reference point (bg_ref_x, bg_ref_y)
    // 2. For each pixel, apply affine matrix (pa, pb, pc, pd)
    // 3. Sample from tile map at transformed coordinates
    // 4. Advance reference point by (pb, pd) after the scanline
}
