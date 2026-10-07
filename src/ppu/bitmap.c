#include "ppu.h"

// In bitmap modes BG2 is an affine layer (GBATEK "LCD I/O BG Rotation/
// Scaling"). Each screen pixel maps to a texture coordinate through the
// BG2 internal reference point (24.8 fixed point, advanced by PB/PD per
// line in ppu.c) plus PA/PC per screen x. Coordinates outside the bitmap
// are transparent: the BGCNT wraparound bit does not apply to bitmaps.
//
// Returns false when screen pixel x falls outside the w x h bitmap.
// Mosaic handling mirrors ppu_render_bg_affine() in background.c.
static bool bitmap_tex_coord(const PPU* ppu, uint32_t x, int32_t w, int32_t h,
                             int32_t* tex_x, int32_t* tex_y) {
    int32_t px = ppu->bg_ref_x[0];
    int32_t py = ppu->bg_ref_y[0];
    uint32_t sx = x;

    if (BIT(ppu->bg_cnt[2], 6)) {
        // Vertical mosaic: rewind the ref point to the top of the block.
        uint16_t bg_v_size = BITS(ppu->mosaic, 7, 4) + 1;
        if (bg_v_size > 1) {
            uint16_t lines_back = ppu->vcount % bg_v_size;
            px -= (int32_t)ppu->bg_pb[0] * (int32_t)lines_back;
            py -= (int32_t)ppu->bg_pd[0] * (int32_t)lines_back;
        }
        // Horizontal mosaic: sample at the block-start column.
        uint16_t bg_h_size = BITS(ppu->mosaic, 3, 0) + 1;
        sx -= x % bg_h_size;
    }

    px += (int32_t)ppu->bg_pa[0] * (int32_t)sx;
    py += (int32_t)ppu->bg_pc[0] * (int32_t)sx;

    *tex_x = px >> 8;
    *tex_y = py >> 8;
    return *tex_x >= 0 && *tex_x < w && *tex_y >= 0 && *tex_y < h;
}

// Mode 3: 240x160 direct color (16bpp), single page.
// Each pixel is a little-endian 15-bit BGR color at (y * 240 + x) * 2.
// No transparent color: every in-bounds pixel is opaque (bit 15 ignored).
void ppu_render_mode3(PPU* ppu) {
    // BG2 enable is bit 10 of DISPCNT. If it's off, leave the scanline
    // at the backdrop color (already filled by ppu_render_scanline).
    if (!BIT(ppu->dispcnt, 10)) return;

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        int32_t tx, ty;
        if (!bitmap_tex_coord(ppu, x, SCREEN_WIDTH, SCREEN_HEIGHT, &tx, &ty)) {
            continue;
        }
        uint32_t offset = ((uint32_t)ty * SCREEN_WIDTH + (uint32_t)tx) * 2;
        uint16_t color = (uint16_t)ppu->vram[offset]
                       | ((uint16_t)ppu->vram[offset + 1] << 8);
        ppu_push_pixel(ppu, x, color, 2);
    }
}

// Mode 4: 240x160 indexed color (8bpp) with page flipping.
// Each pixel is a 1-byte palette index into BG palette RAM. DISPCNT bit 4
// selects the page (0x0000 or 0xA000). Index 0 is transparent.
void ppu_render_mode4(PPU* ppu) {
    if (!BIT(ppu->dispcnt, 10)) return;

    uint32_t page_base = BIT(ppu->dispcnt, 4) ? 0xA000 : 0x0000;

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        int32_t tx, ty;
        if (!bitmap_tex_coord(ppu, x, SCREEN_WIDTH, SCREEN_HEIGHT, &tx, &ty)) {
            continue;
        }
        uint8_t palette_idx =
            ppu->vram[page_base + (uint32_t)ty * SCREEN_WIDTH + (uint32_t)tx];
        if (palette_idx == 0) continue;
        uint16_t color = (uint16_t)ppu->palette_ram[palette_idx * 2]
                       | ((uint16_t)ppu->palette_ram[palette_idx * 2 + 1] << 8);
        ppu_push_pixel(ppu, x, color, 2);
    }
}

// Mode 5: 160x128 direct color (16bpp) with page flipping.
// Like mode 3 but a smaller bitmap. The bounds apply to texture coords,
// so a scaled or offset BG2 can cover the full 240-pixel screen width.
void ppu_render_mode5(PPU* ppu) {
    if (!BIT(ppu->dispcnt, 10)) return;

    uint32_t page_base = BIT(ppu->dispcnt, 4) ? 0xA000 : 0x0000;

    for (uint32_t x = 0; x < SCREEN_WIDTH; x++) {
        int32_t tx, ty;
        if (!bitmap_tex_coord(ppu, x, 160, 128, &tx, &ty)) continue;
        uint32_t offset = page_base + ((uint32_t)ty * 160 + (uint32_t)tx) * 2;
        uint16_t color = (uint16_t)ppu->vram[offset]
                       | ((uint16_t)ppu->vram[offset + 1] << 8);
        ppu_push_pixel(ppu, x, color, 2);
    }
}
