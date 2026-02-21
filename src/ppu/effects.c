#include "ppu.h"

// ---------------------------------------------------------------------------
// Color blending / special effects (BLDCNT, BLDALPHA, BLDY)
// ---------------------------------------------------------------------------

// Check if a layer is a 1st target in BLDCNT (bits 0-5: BG0-BG3, OBJ, BD)
static bool is_first_target(uint16_t bldcnt, uint8_t layer) {
    return BIT(bldcnt, layer) != 0;
}

// Check if a layer is a 2nd target in BLDCNT (bits 8-13)
static bool is_second_target(uint16_t bldcnt, uint8_t layer) {
    return BIT(bldcnt, 8 + layer) != 0;
}

// Extract RGB components from 15-bit BGR555 color (0BBBBBGGGGGRRRRR)
static inline void unpack_rgb(uint16_t color,
                               uint32_t* r, uint32_t* g, uint32_t* b) {
    *r = color & 0x1F;
    *g = (color >> 5) & 0x1F;
    *b = (color >> 10) & 0x1F;
}

// Pack RGB components back to 15-bit BGR555
static inline uint16_t pack_rgb(uint32_t r, uint32_t g, uint32_t b) {
    return (uint16_t)((b << 10) | (g << 5) | r);
}

// Apply blending effects to the entire scanline after compositing.
// Must be called after all BG and OBJ layers have been rendered and
// layer-tracking arrays (top_layer, second_pixel, second_layer) are filled.
void ppu_apply_blend_scanline(PPU* ppu) {
    uint16_t bldcnt = ppu->bldcnt;
    uint8_t mode = (uint8_t)BITS(bldcnt, 7, 6);

    // Mode 0: no blending -- early out
    if (mode == 0) return;

    // Alpha blend coefficients (BLDALPHA register), clamped to 0-16
    uint32_t eva = ppu->bldalpha & 0x1F;
    uint32_t evb = (ppu->bldalpha >> 8) & 0x1F;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;

    // Brightness coefficient (BLDY register), clamped to 0-16
    uint32_t evy = ppu->bldy & 0x1F;
    if (evy > 16) evy = 16;

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        uint8_t top_id = ppu->top_layer[x];

        // All modes require the top pixel to be a 1st target
        if (!is_first_target(bldcnt, top_id)) continue;

        if (mode == 1) {
            // Alpha blend: 2nd target must also match
            if (!is_second_target(bldcnt, ppu->second_layer[x])) continue;

            uint32_t r1, g1, b1, r2, g2, b2;
            unpack_rgb(ppu->scanline_buffer[x], &r1, &g1, &b1);
            unpack_rgb(ppu->second_pixel[x], &r2, &g2, &b2);

            uint32_t r = (r1 * eva + r2 * evb) >> 4;
            uint32_t g = (g1 * eva + g2 * evb) >> 4;
            uint32_t b = (b1 * eva + b2 * evb) >> 4;
            if (r > 31) r = 31;
            if (g > 31) g = 31;
            if (b > 31) b = 31;

            ppu->scanline_buffer[x] = pack_rgb(r, g, b);
        } else if (mode == 2) {
            // Brightness increase (fade to white)
            uint32_t r, g, b;
            unpack_rgb(ppu->scanline_buffer[x], &r, &g, &b);

            r = r + ((31 - r) * evy >> 4);
            g = g + ((31 - g) * evy >> 4);
            b = b + ((31 - b) * evy >> 4);

            ppu->scanline_buffer[x] = pack_rgb(r, g, b);
        } else if (mode == 3) {
            // Brightness decrease (fade to black)
            uint32_t r, g, b;
            unpack_rgb(ppu->scanline_buffer[x], &r, &g, &b);

            r = r - (r * evy >> 4);
            g = g - (g * evy >> 4);
            b = b - (b * evy >> 4);

            ppu->scanline_buffer[x] = pack_rgb(r, g, b);
        }
    }
}

// ---------------------------------------------------------------------------
// Windowing (stub -- not yet implemented)
// ---------------------------------------------------------------------------

// Determine which layers are visible in each window region
void ppu_apply_windowing(PPU* ppu, int x) {
    // TODO: Check if pixel (x, vcount) is inside WIN0, WIN1, or OBJ window
    // Return the layer enable mask for that window region
    // Fallback to WINOUT if not in any window
    (void)ppu;
    (void)x;
}

// ---------------------------------------------------------------------------
// Mosaic (stub -- not yet implemented)
// ---------------------------------------------------------------------------

// Apply mosaic effect to background or sprite scanline
void ppu_apply_mosaic(PPU* ppu, uint16_t* scanline, int width, bool is_obj) {
    // TODO: Group pixels into mosaic_h x mosaic_v blocks
    // All pixels in a block take the color of the top-left pixel
    (void)ppu;
    (void)scanline;
    (void)width;
    (void)is_obj;
}
