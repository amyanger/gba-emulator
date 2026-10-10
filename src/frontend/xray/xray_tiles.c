#include "xray.h"
#include "frontend/overlay_draw.h"
#include "ppu/ppu.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#define SHEET 92.0f
#define SHEET_GAP 8.0f
#define SWATCH 5.0f

/* Decode a 4bpp tile pixel. Returns GBA BGR555 color, 0 for transparent. */
static uint16_t decode_tile_pixel_4bpp(const uint8_t* vram, const uint8_t* palette_ram,
                                       uint32_t tile_base, int tile_id, int px, int py,
                                       int palette_id) {
    /* Each 4bpp tile is 32 bytes (8x8 pixels, 4 bits each) */
    uint32_t byte_offset = tile_base + (uint32_t)tile_id * 32 + (uint32_t)py * 4 + (uint32_t)px / 2;
    if (byte_offset >= 0x18000) return 0; /* VRAM bounds check */

    uint8_t byte = vram[byte_offset];
    uint8_t nibble = (px & 1) ? (byte >> 4) : (byte & 0x0F);
    if (nibble == 0) return 0;

    /* Each palette is 16 colors (32 bytes) */
    uint32_t pal_addr = (uint32_t)(palette_id * 32 + nibble * 2);
    if (pal_addr + 1 >= 0x400) return 0;
    return (uint16_t)palette_ram[pal_addr] | ((uint16_t)palette_ram[pal_addr + 1] << 8);
}

/* 256 tiles as a 128x128 ARGB sheet; transparent pixels get a dark checkerboard. */
static void charblock_to_argb(const PPU* ppu, const uint8_t* pal, int charblock, uint32_t* out) {
    uint32_t base = (uint32_t)charblock * 0x4000;
    for (int t = 0; t < 256; t++)
        for (int py = 0; py < 8; py++)
            for (int px = 0; px < 8; px++) {
                uint16_t col = decode_tile_pixel_4bpp(ppu->vram, pal, base, t, px, py, 0);
                int x = (t % 16) * 8 + px, y = (t / 16) * 8 + py;
                out[y * 128 + x] = col ? gba_to_argb(col)
                                       : (((px + py) & 1) ? 0xFF1A1E29u : 0xFF14171Fu);
            }
}

static void palette_to_argb(const uint8_t* pal, uint32_t* out) {
    for (int i = 0; i < 256; i++)
        out[i] = gba_to_argb((uint16_t)pal[i * 2] | ((uint16_t)pal[i * 2 + 1] << 8));
}

/* A 92 pt sheet with its block number on a small chip in the corner (costs no height). */
static void sheet(UiCanvas* c, PPU* ppu, XRayState* s, const uint8_t* pal, int cb, float x,
                  float y) {
    charblock_to_argb(ppu, pal, cb, s->scratch);
    xray_blit_argb(c, (UiRect){x, y, SHEET, SHEET}, s->scratch, 128, 128);
    ui_fill_round_rect(c, (UiRect){x + 3.0f, y + 3.0f, 16.0f, 13.0f}, 3.0f, UI_BG);
    xray_textf(c, UI_FONT_SEMIBOLD, XRAY_SIZE_HEAD, x + 11.0f, y + 3.5f, 0, UI_ALIGN_CENTER,
               UI_DIM, "%d", cb);
}

/* Rows: section label at top, sheets at top+16 (end top+108); second label at top+116,
 * sheets at top+132 (end top+224). Palettes share the same rows. */
void xray_render_tiles(UiCanvas* c, PPU* ppu, XRayState* s) {
    float top = xray_card(c, XRAY_CARD_TILES, NULL);
    float x0 = xray_cards[XRAY_CARD_TILES].x + XRAY_PAD;
    float row2 = top + 116.0f;

    ui_section_label(c, x0, top, "BACKGROUND TILES");
    for (int cb = 0; cb < 4; cb++)
        sheet(c, ppu, s, ppu->palette_ram, cb, x0 + (float)cb * (SHEET + SHEET_GAP), top + 16.0f);
    ui_section_label(c, x0, row2, "SPRITE TILES");
    for (int cb = 4; cb < 6; cb++)
        sheet(c, ppu, s, ppu->palette_ram + 0x200, cb, x0 + (float)(cb - 4) * (SHEET + SHEET_GAP),
              row2 + 16.0f);

    float px = x0 + 430.0f;
    ui_section_label(c, px, top, "BACKGROUND COLORS");
    palette_to_argb(ppu->palette_ram, s->scratch);
    xray_blit_argb(c, (UiRect){px, top + 16.0f, 16 * SWATCH, 16 * SWATCH}, s->scratch, 16, 16);
    ui_section_label(c, px, row2, "SPRITE COLORS");
    palette_to_argb(ppu->palette_ram + 0x200, s->scratch);
    xray_blit_argb(c, (UiRect){px, row2 + 16.0f, 16 * SWATCH, 16 * SWATCH}, s->scratch, 16, 16);
}
