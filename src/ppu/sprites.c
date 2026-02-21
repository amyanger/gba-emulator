#include "ppu.h"

// Render all visible sprites for the current scanline
void ppu_render_sprites(PPU* ppu) {
    // TODO: Implement OAM sprite rendering
    // 1. Iterate through 128 OAM entries (each 8 bytes)
    // 2. For each sprite, check if it intersects the current scanline
    // 3. Determine sprite size from OAM attributes (shape + size bits)
    // 4. Handle regular vs affine sprites
    // 5. Read tile data from OBJ character blocks
    // 6. Handle horizontal/vertical flip
    // 7. Apply sprite priority for compositing
    // 8. Handle 1D vs 2D tile mapping (DISPCNT bit 6)
}
