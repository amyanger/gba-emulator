#include "xray.h"
#include "xray_draw.h"
#include "ppu/ppu.h"

/* Decode a 4bpp tile pixel. Returns GBA BGR555 color. */
static uint16_t decode_tile_pixel_4bpp(const uint8_t* vram,
                                       const uint8_t* palette_ram,
                                       uint32_t tile_base, int tile_id,
                                       int px, int py, int palette_id) {
    /* Each 4bpp tile is 32 bytes (8x8 pixels, 4 bits each) */
    uint32_t tile_addr = tile_base + (uint32_t)tile_id * 32;
    uint32_t byte_offset = tile_addr + (uint32_t)py * 4 + (uint32_t)px / 2;

    if (byte_offset >= 0x18000) return 0; /* VRAM bounds check */

    uint8_t byte = vram[byte_offset];
    uint8_t nibble = (px & 1) ? (byte >> 4) : (byte & 0x0F);

    if (nibble == 0) return 0; /* Transparent */

    /* Palette lookup: each palette is 16 colors (32 bytes).
     * BG palette starts at palette_ram[0]. */
    uint32_t pal_addr = (uint32_t)(palette_id * 32 + nibble * 2);
    if (pal_addr + 1 >= 0x400) return 0;

    return (uint16_t)palette_ram[pal_addr] |
           ((uint16_t)palette_ram[pal_addr + 1] << 8);
}

/* Render a charblock (256 tiles at 4bpp) as a 16x16 tile grid */
static void render_charblock_4bpp(uint32_t* buf, int buf_w, int buf_h,
                                  int dst_x, int dst_y,
                                  const uint8_t* vram,
                                  const uint8_t* palette_ram,
                                  int charblock, int palette_id) {
    /* Charblock base: each charblock is 16KB */
    uint32_t base = (uint32_t)charblock * 0x4000;

    /* 256 tiles arranged in 16x16 grid, each tile 8x8 pixels
     * Total size: 128x128 pixels. Draw at 1:1. */
    int tiles_per_row = 16;
    int max_tiles = 256;

    for (int t = 0; t < max_tiles; t++) {
        int tx = t % tiles_per_row;
        int ty = t / tiles_per_row;

        for (int py = 0; py < 8; py++) {
            for (int px = 0; px < 8; px++) {
                uint16_t color = decode_tile_pixel_4bpp(
                    vram, palette_ram, base, t, px, py, palette_id);

                int dx = dst_x + tx * 8 + px;
                int dy = dst_y + ty * 8 + py;

                if (dx >= 0 && dx < buf_w && dy >= 0 && dy < buf_h) {
                    if (color != 0) {
                        buf[dy * buf_w + dx] = gba_to_argb(color);
                    } else {
                        /* Transparent pixel: dark checkerboard pattern */
                        bool checker = ((px + py) & 1) != 0;
                        buf[dy * buf_w + dx] = checker ? 0xFF1A1A2E
                                                       : 0xFF0D0D20;
                    }
                }
            }
        }
    }
}

/* Render palette grid (256 colors as 16x16 swatches) */
static void render_palette_grid(uint32_t* buf, int buf_w, int buf_h,
                                int dst_x, int dst_y, int swatch_size,
                                const uint8_t* palette_ram, int palette_offset) {
    for (int i = 0; i < 256; i++) {
        int cx = i % 16;
        int cy = i / 16;

        uint32_t pal_addr = (uint32_t)(palette_offset + i * 2);
        uint16_t color = (uint16_t)palette_ram[pal_addr] |
                         ((uint16_t)palette_ram[pal_addr + 1] << 8);
        uint32_t argb = gba_to_argb(color);

        int sx = dst_x + cx * swatch_size;
        int sy = dst_y + cy * swatch_size;

        xray_draw_rect(buf, buf_w, buf_h, sx, sy, swatch_size, swatch_size,
                       argb);
    }

    /* Grid outline */
    xray_draw_rect_outline(buf, buf_w, buf_h, dst_x, dst_y,
                           16 * swatch_size, 16 * swatch_size,
                           XRAY_COL_BORDER);
}

void xray_render_tiles(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                       int pw, int ph, PPU* ppu) {
    (void)pw;
    (void)ph;

    int x0 = px + 8;
    int y0 = py + 18;

    /* === TILE VIEWER (charblocks 0-3 for BG, 4-5 for OBJ) === */
    xray_draw_text(buf, buf_w, buf_h, x0, y0, "BG Tiles", XRAY_COL_HEADER);
    y0 += 12;

    /* Charblocks 0-3 (BG tiles) in a 2x2 grid of 128x128 each */
    for (int cb = 0; cb < 4; cb++) {
        int col = cb % 2;
        int row = cb / 2;
        int cx = x0 + col * 140;
        int cy = y0 + row * 140;

        xray_draw_textf(buf, buf_w, buf_h, cx, cy, XRAY_COL_LABEL,
                        "CB%d", cb);
        render_charblock_4bpp(buf, buf_w, buf_h, cx, cy + 10, ppu->vram,
                              ppu->palette_ram, cb, 0);
    }

    /* OBJ charblocks (4-5) */
    int obj_y = y0 + 2 * 140 + 8;
    xray_draw_text(buf, buf_w, buf_h, x0, obj_y, "OBJ Tiles",
                   XRAY_COL_HEADER);
    obj_y += 12;

    /* OBJ tiles are in charblocks 4-5 (VRAM 0x10000-0x17FFF) */
    for (int cb = 4; cb < 6; cb++) {
        int col = cb - 4;
        int cx = x0 + col * 140;
        render_charblock_4bpp(buf, buf_w, buf_h, cx, obj_y, ppu->vram,
                              ppu->palette_ram + 0x200, cb, 0);
    }

    /* === PALETTE VIEWER === */
    int pal_x = x0 + 300;
    int pal_y = py + 18;

    xray_draw_text(buf, buf_w, buf_h, pal_x, pal_y, "BG Palette",
                   XRAY_COL_HEADER);
    pal_y += 12;

    /* BG palette: 256 colors, 16x16 grid, 4px swatches */
    int swatch = 4;
    render_palette_grid(buf, buf_w, buf_h, pal_x, pal_y, swatch,
                        ppu->palette_ram, 0);

    pal_y += 16 * swatch + 8;
    xray_draw_text(buf, buf_w, buf_h, pal_x, pal_y, "OBJ Palette",
                   XRAY_COL_HEADER);
    pal_y += 12;

    /* OBJ palette: 256 colors at offset 0x200 */
    render_palette_grid(buf, buf_w, buf_h, pal_x, pal_y, swatch,
                        ppu->palette_ram, 0x200);

    /* Palette index labels */
    pal_y += 16 * swatch + 8;
    xray_draw_textf(buf, buf_w, buf_h, pal_x, pal_y, XRAY_COL_DIM,
                    "Each swatch = %dpx. 256 colors (16x16)", swatch);
}
