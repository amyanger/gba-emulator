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
// Alpha-blend the pixel at x against its second-priority pixel.
static void alpha_blend_pixel(PPU* ppu, uint32_t x, uint32_t eva, uint32_t evb) {
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
}

void ppu_apply_blend_scanline(PPU* ppu) {
    uint16_t bldcnt = ppu->bldcnt;
    uint8_t mode = (uint8_t)BITS(bldcnt, 7, 6);

    // Alpha blend coefficients (BLDALPHA register), clamped to 0-16
    uint32_t eva = ppu->bldalpha & 0x1F;
    uint32_t evb = (ppu->bldalpha >> 8) & 0x1F;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;

    // Brightness coefficient (BLDY register), clamped to 0-16
    uint32_t evy = ppu->bldy & 0x1F;
    if (evy > 16) evy = 16;

    // Check if any window is enabled — if so, the per-pixel blend flag applies.
    bool windowing_active = BIT(ppu->dispcnt, 13) || BIT(ppu->dispcnt, 14)
                          || BIT(ppu->dispcnt, 15);

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        // If windowing is active and color effects are disabled for this pixel, skip
        if (windowing_active && !ppu->win_blend_enable[x]) continue;

        uint8_t top_id = ppu->top_layer[x];

        // Semi-transparent OBJ pixels (GFX mode 1) force alpha blending
        // against a valid 2nd target, regardless of the BLDCNT mode bits
        // and of the OBJ 1st-target bit.
        if (top_id == 4 && ppu->obj_semitransparent[x]
            && is_second_target(bldcnt, ppu->second_layer[x])) {
            alpha_blend_pixel(ppu, x, eva, evb);
            continue;
        }

        // Mode 0: no (further) blending
        if (mode == 0) continue;

        // All modes require the top pixel to be a 1st target
        if (!is_first_target(bldcnt, top_id)) continue;

        if (mode == 1) {
            // Alpha blend: 2nd target must also match
            if (!is_second_target(bldcnt, ppu->second_layer[x])) continue;
            alpha_blend_pixel(ppu, x, eva, evb);
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
// Windowing
// ---------------------------------------------------------------------------

// Check if the current scanline (vcount) falls within a window's vertical range.
static bool win_v_active(uint16_t win_v, uint16_t vcount) {
    uint32_t top    = win_v >> 8;
    uint32_t bottom = win_v & 0xFF;

    // GBATEK: garbage values (Y1 > Y2, or Y2 > 160) are interpreted as
    // Y2 = 160 — the window extends to the bottom edge, no wrapping.
    if (top > bottom || bottom > SCREEN_HEIGHT) {
        bottom = SCREEN_HEIGHT;
    }
    return (vcount >= top && vcount < bottom);
}

// Check if a pixel X coordinate falls within a window's horizontal range.
static bool win_h_active(uint16_t win_h, uint32_t x) {
    uint32_t left  = win_h >> 8;
    uint32_t right = win_h & 0xFF;

    // GBATEK: garbage values (X1 > X2, or X2 > 240) are interpreted as
    // X2 = 240 — the window extends to the right edge, no wrapping.
    if (left > right || right > SCREEN_WIDTH) {
        right = SCREEN_WIDTH;
    }
    return (x >= left && x < right);
}

// Apply windowing to the fully composited scanline.
// For each pixel, determine which window region it belongs to (WIN0 > WIN1 >
// OBJWIN > outside), then check if the top layer is enabled in that region.
// If the layer is disabled, replace the pixel with the second-priority pixel
// (if that layer IS enabled), otherwise fall back to backdrop.
//
// The "color effects" enable bit (bit 5 of each window's mask) controls whether
// blending applies to pixels in that region. We store this per-pixel so the
// subsequent blend pass can consult it.
void ppu_apply_windowing_scanline(PPU* ppu) {
    // Windowing is only active if at least one window is enabled in DISPCNT
    // bits 13 (WIN0), 14 (WIN1), 15 (OBJWIN).
    bool win0_on  = BIT(ppu->dispcnt, 13);
    bool win1_on  = BIT(ppu->dispcnt, 14);
    bool objwin_on = BIT(ppu->dispcnt, 15);

    if (!win0_on && !win1_on && !objwin_on) return;

    // Precompute vertical containment (same for all pixels on this scanline)
    bool win0_v = win0_on && win_v_active(ppu->win_v[0], ppu->vcount);
    bool win1_v = win1_on && win_v_active(ppu->win_v[1], ppu->vcount);

    // Extract the 6-bit enable masks for each window region once:
    //   bits 0-3 = BG0-BG3 enable, bit 4 = OBJ enable, bit 5 = color effects
    uint8_t win0_mask   = (uint8_t)(ppu->winin & 0x3F);
    uint8_t win1_mask   = (uint8_t)((ppu->winin >> 8) & 0x3F);
    uint8_t outside_mask = (uint8_t)(ppu->winout & 0x3F);
    uint8_t objwin_mask = (uint8_t)((ppu->winout >> 8) & 0x3F);

    // Backdrop color (palette entry 0) for pixels that get fully masked
    uint16_t backdrop = (uint16_t)ppu->palette_ram[0]
                      | ((uint16_t)ppu->palette_ram[1] << 8);

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        // Determine which window region this pixel belongs to.
        // Priority: WIN0 > WIN1 > OBJWIN > outside
        uint8_t mask;

        if (win0_v && win_h_active(ppu->win_h[0], x)) {
            mask = win0_mask;
        } else if (win1_v && win_h_active(ppu->win_h[1], x)) {
            mask = win1_mask;
        } else if (objwin_on && ppu->obj_window[x]) {
            mask = objwin_mask;
        } else {
            mask = outside_mask;
        }

        // Check if the top layer is enabled in this window region.
        // Layer IDs: 0-3 = BG0-BG3, 4 = OBJ, 5 = backdrop
        uint8_t top_id = ppu->top_layer[x];
        bool top_enabled;

        if (top_id <= 3) {
            top_enabled = BIT(mask, top_id);
        } else if (top_id == 4) {
            top_enabled = BIT(mask, 4);  // OBJ enable
        } else {
            top_enabled = true;  // Backdrop is always visible
        }

        if (!top_enabled) {
            // Top pixel is masked out. Try the second pixel.
            uint8_t second_id = ppu->second_layer[x];
            bool second_enabled;

            if (second_id <= 3) {
                second_enabled = BIT(mask, second_id);
            } else if (second_id == 4) {
                second_enabled = BIT(mask, 4);
            } else {
                second_enabled = true;
            }

            if (second_enabled) {
                ppu->scanline_buffer[x] = ppu->second_pixel[x];
                ppu->top_layer[x] = second_id;
                // Second becomes backdrop (nothing behind it)
                ppu->second_pixel[x] = backdrop;
                ppu->second_layer[x] = 5;
            } else {
                // Both masked — show backdrop
                ppu->scanline_buffer[x] = backdrop;
                ppu->top_layer[x] = 5;
                ppu->second_pixel[x] = backdrop;
                ppu->second_layer[x] = 5;
            }
        }

        // Store whether color effects are enabled for this pixel's window region.
        ppu->win_blend_enable[x] = BIT(mask, 5);
    }
}

// ---------------------------------------------------------------------------
// Mosaic
// ---------------------------------------------------------------------------

// Apply mosaic post-processing to the composited scanline.
//
// BG horizontal mosaic is handled during per-BG rendering (background.c snaps
// the source X coordinate). BG vertical mosaic is also in background.c.
// OBJ vertical mosaic is in sprites.c (snaps local_y per-sprite).
//
// This function handles OBJ horizontal mosaic as a post-process, using the
// per-pixel obj_mosaic[] flag set by sprites.c to respect per-sprite enable.
//
// MOSAIC register (0x400004C):
//   Bits 8-11:  OBJ mosaic H-size (minus 1)
void ppu_apply_mosaic_scanline(PPU* ppu) {
    uint16_t obj_h_size = BITS(ppu->mosaic, 11, 8) + 1;
    if (obj_h_size <= 1) return;

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        // Only apply to OBJ pixels from mosaic-enabled sprites
        if (ppu->top_layer[x] != 4) continue;
        if (!ppu->obj_mosaic[x]) continue;

        uint32_t block_start = x - (x % obj_h_size);
        if (block_start != x
            && ppu->top_layer[block_start] == 4
            && ppu->obj_mosaic[block_start]) {
            ppu->scanline_buffer[x] = ppu->scanline_buffer[block_start];
        }
    }
}
