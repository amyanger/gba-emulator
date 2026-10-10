#include "xray.h"
#include "frontend/overlay_draw.h"
#include "ppu/ppu.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

/* Layer colors indexed by layer ID: 0-3 = BG, 4 = OBJ, 5 = backdrop */
static const uint32_t layer_colors[6] = {
    XRAY_COL_BG0, XRAY_COL_BG1, XRAY_COL_BG2, XRAY_COL_BG3, XRAY_COL_OBJ, XRAY_COL_BACKDROP
};

void xray_capture_ppu_layers(PPU* ppu, XRayState* state) {
    if (!state || !state->active) return;

    uint8_t mode = ppu->dispcnt & 0x7;
    uint16_t saved_vcount = ppu->vcount;

    /* Saved scanline/layer state */
    uint16_t saved_scanline[SCREEN_WIDTH];
    uint8_t saved_top_layer[SCREEN_WIDTH];
    uint16_t saved_second_pixel[SCREEN_WIDTH];
    uint8_t saved_second_layer[SCREEN_WIDTH];
    uint8_t saved_win_mask[SCREEN_WIDTH];

    memcpy(saved_scanline, ppu->scanline_buffer, sizeof(saved_scanline));
    memcpy(saved_top_layer, ppu->top_layer, sizeof(saved_top_layer));
    memcpy(saved_second_pixel, ppu->second_pixel, sizeof(saved_second_pixel));
    memcpy(saved_second_layer, ppu->second_layer, sizeof(saved_second_layer));
    memcpy(saved_win_mask, ppu->win_mask, sizeof(saved_win_mask));

    /* Save affine internal refs */
    int32_t saved_ref_x[2], saved_ref_y[2];
    memcpy(saved_ref_x, ppu->bg_ref_x, sizeof(saved_ref_x));
    memcpy(saved_ref_y, ppu->bg_ref_y, sizeof(saved_ref_y));

    /* Clear layer buffers */
    memset(state->layer_bg, 0, sizeof(state->layer_bg));
    memset(state->layer_obj, 0, sizeof(state->layer_obj));

    /* Get backdrop color */
    uint16_t backdrop = (uint16_t)ppu->palette_ram[0] |
                        ((uint16_t)ppu->palette_ram[1] << 8);

    /* Isolated layer previews are deliberately unwindowed. */
    memset(ppu->win_mask, 0x3F, sizeof(ppu->win_mask));

    /* Re-render each BG layer in isolation */
    for (int bg = 0; bg < 4; bg++) {
        if (!BIT(ppu->dispcnt, 8 + bg)) continue;

        /* Reset affine refs for re-rendering */
        memcpy(ppu->bg_ref_x, ppu->bg_ref_x_latch, sizeof(ppu->bg_ref_x));
        memcpy(ppu->bg_ref_y, ppu->bg_ref_y_latch, sizeof(ppu->bg_ref_y));

        for (int line = 0; line < VDRAW_LINES; line++) {
            ppu->vcount = (uint16_t)line;

            /* Fill with backdrop */
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                ppu->scanline_buffer[x] = backdrop;
                ppu->top_layer[x] = 5;
            }

            /* Render just this BG */
            bool is_affine = false;
            if (mode == 0) {
                ppu_render_bg_regular(ppu, bg);
            } else if (mode == 1) {
                if (bg == 2) {
                    ppu_render_bg_affine(ppu, 2);
                    is_affine = true;
                } else if (bg < 2) {
                    ppu_render_bg_regular(ppu, bg);
                }
            } else if (mode == 2) {
                if (bg >= 2) {
                    ppu_render_bg_affine(ppu, bg);
                    is_affine = true;
                }
            }
            /* Bitmap modes: only one "layer" — use the composite */

            memcpy(&state->layer_bg[bg][line * SCREEN_WIDTH],
                   ppu->scanline_buffer, SCREEN_WIDTH * sizeof(uint16_t));

            /* Advance affine refs for next scanline */
            if (is_affine) {
                int aidx = bg - 2;
                ppu->bg_ref_x[aidx] += (int32_t)ppu->bg_pb[aidx];
                ppu->bg_ref_y[aidx] += (int32_t)ppu->bg_pd[aidx];
            }
        }
    }

    /* Re-render sprites in isolation */
    if (BIT(ppu->dispcnt, 12)) {
        for (int line = 0; line < VDRAW_LINES; line++) {
            ppu->vcount = (uint16_t)line;

            for (int x = 0; x < SCREEN_WIDTH; x++) {
                ppu->scanline_buffer[x] = backdrop;
                ppu->top_layer[x] = 5;
            }

            ppu_render_sprites(ppu);

            memcpy(&state->layer_obj[line * SCREEN_WIDTH],
                   ppu->scanline_buffer, SCREEN_WIDTH * sizeof(uint16_t));
        }
    }

    /* Restore PPU state */
    ppu->vcount = saved_vcount;
    memcpy(ppu->scanline_buffer, saved_scanline, sizeof(saved_scanline));
    memcpy(ppu->top_layer, saved_top_layer, sizeof(saved_top_layer));
    memcpy(ppu->second_pixel, saved_second_pixel, sizeof(saved_second_pixel));
    memcpy(ppu->second_layer, saved_second_layer, sizeof(saved_second_layer));
    memcpy(ppu->win_mask, saved_win_mask, sizeof(saved_win_mask));
    memcpy(ppu->bg_ref_x, saved_ref_x, sizeof(saved_ref_x));
    memcpy(ppu->bg_ref_y, saved_ref_y, sizeof(saved_ref_y));
}

static const char* mode_pill(uint8_t mode) {
    switch (mode) {
    case 0:  return "Mode 0 \xC2\xB7 4 tiled layers";
    case 1:  return "Mode 1 \xC2\xB7 2 tiled + 1 rotating";
    case 2:  return "Mode 2 \xC2\xB7 2 rotating layers";
    case 3:  return "Mode 3 \xC2\xB7 full-color bitmap";
    case 4:  return "Mode 4 \xC2\xB7 256-color bitmap";
    case 5:  return "Mode 5 \xC2\xB7 small bitmap";
    default: return "Unknown mode";
    }
}

/* What a BG's scroll column shows: "" for scroll offsets, a word for other kinds,
 * NULL when the mode has no such layer. */
static const char* bg_kind(uint8_t mode, int bg) {
    if (mode >= 3) return bg == 2 ? "bitmap" : NULL;
    if (mode == 2) return bg >= 2 ? "rotating" : NULL;
    if (mode == 1 && bg == 2) return "rotating";
    if (mode == 1 && bg == 3) return NULL;
    return "";
}

#define THUMB_W 129.0f
#define THUMB_H 86.0f
#define THUMB_GAP 10.0f

/* The composited frame tinted by which layer produced each pixel. */
static void layer_map_to_argb(const PPU* ppu, const XRayState* s, uint32_t* out) {
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint8_t layer = s->layer_map[y][x];
            if (layer > 5) layer = 5;
            uint32_t tint = layer_colors[layer];
            uint32_t argb = gba_to_argb(ppu->framebuffer[y * SCREEN_WIDTH + x]);
            uint32_t r = ((argb >> 16) & 0xFF) / 2 + ((tint >> 16) & 0xFF) / 2;
            uint32_t g = ((argb >> 8) & 0xFF) / 2 + ((tint >> 8) & 0xFF) / 2;
            uint32_t b = (argb & 0xFF) / 2 + (tint & 0xFF) / 2;
            out[y * SCREEN_WIDTH + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
}

static void thumb(UiCanvas* c, float x, float y, const char* label, uint32_t color, bool on,
                  const uint16_t* img555, const uint32_t* img_argb) {
    ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_HEAD, x, y, THUMB_W, UI_ALIGN_LEFT,
            on ? color : UI_FAINT, label);
    UiRect img = {x, y + 15.0f, THUMB_W, THUMB_H};
    if (on && img555) {
        xray_blit_555(c, img, img555, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else if (on && img_argb) {
        xray_blit_argb(c, img, img_argb, SCREEN_WIDTH, SCREEN_HEIGHT);
    } else {
        ui_fill_round_rect(c, img, 4.0f, UI_BG);
        ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_HEAD, x + THUMB_W / 2, img.y + THUMB_H / 2 - 7.0f,
                0, UI_ALIGN_CENTER, UI_FAINT, "Not used");
    }
}

static void head_right(UiCanvas* c, float x, float y, const char* t) {
    ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_HEAD, x, y, 0, UI_ALIGN_RIGHT, UI_DIM, t);
}

void xray_render_ppu(UiCanvas* c, PPU* ppu, XRayState* s) {
    uint8_t mode = ppu->dispcnt & 0x7;
    float top = xray_card(c, XRAY_CARD_SEE, mode_pill(mode));
    UiRect r = xray_cards[XRAY_CARD_SEE];
    float x0 = r.x + XRAY_PAD;

    static const char* names[4] = {"BG0", "BG1", "BG2", "BG3"};
    for (int i = 0; i < 6; i++) {
        float tx = x0 + (float)(i % 3) * (THUMB_W + THUMB_GAP);
        float ty = top + (float)(i / 3) * (THUMB_H + 15.0f + 10.0f);
        if (i < 4) {
            bool on = BIT(ppu->dispcnt, 8 + i) && bg_kind(mode, i) != NULL;
            thumb(c, tx, ty, names[i], layer_colors[i], on, s->layer_bg[i], NULL);
        } else if (i == 4) {
            thumb(c, tx, ty, "Sprites", layer_colors[4], BIT(ppu->dispcnt, 12), s->layer_obj, NULL);
        } else {
            layer_map_to_argb(ppu, s, s->scratch);
            thumb(c, tx, ty, "Who drew each pixel", UI_MUTED, true, NULL, s->scratch);
        }
    }

    /* Layer table, right of the thumbnails */
    float tx = x0 + 3 * (THUMB_W + THUMB_GAP) + 10.0f;
    float tw = r.x + r.w - XRAY_PAD - tx;
    float sx = tx + 130.0f;
    float y = top;
    ui_section_label(c, tx, y, "LAYER");
    head_right(c, sx, y, "SCROLL");
    head_right(c, tx + tw, y, "PRIORITY");
    y += XRAY_ROW;
    for (int bg = 0; bg < 4; bg++, y += XRAY_ROW) {
        const char* kind = bg_kind(mode, bg);
        bool on = BIT(ppu->dispcnt, 8 + bg) && kind != NULL;
        ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_TEXT, tx, y, 0, UI_ALIGN_LEFT,
                on ? layer_colors[bg] : UI_FAINT, names[bg]);
        if (!on) {
            ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, sx, y, 0, UI_ALIGN_RIGHT, UI_FAINT, "off");
            continue;
        }
        if (kind[0])
            ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, sx, y, 0, UI_ALIGN_RIGHT, UI_TEXT, kind);
        else
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, sx, y, 0, UI_ALIGN_RIGHT, UI_TEXT,
                       "%u, %u", (unsigned)(ppu->bg_hofs[bg] & 0x1FF),
                       (unsigned)(ppu->bg_vofs[bg] & 0x1FF));   /* 9-bit registers */
        xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, tx + tw, y, 0, UI_ALIGN_RIGHT, UI_TEXT,
                   "%u", (unsigned)(ppu->bg_cnt[bg] & 3));
    }

    /* Blending in words */
    static const char* blend_names[4] = {"None", "Mix two layers", "Brighten", "Darken"};
    uint8_t blend = (ppu->bldcnt >> 6) & 3;
    y += 8.0f;
    ui_section_label(c, tx, y, "BLENDING");
    y += XRAY_ROW;
    ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, tx, y, tw, UI_ALIGN_LEFT, UI_TEXT, blend_names[blend]);
    y += XRAY_ROW;
    if (blend == 1)
        xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, tx, y, tw, UI_ALIGN_LEFT, UI_MUTED,
                   "Top %u/16, bottom %u/16", (unsigned)(ppu->bldalpha & 0x1F),
                   (unsigned)((ppu->bldalpha >> 8) & 0x1F));
    else if (blend >= 2)
        xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, tx, y, tw, UI_ALIGN_LEFT, UI_MUTED,
                   "Strength %u/16", (unsigned)(ppu->bldy & 0x1F));
}
