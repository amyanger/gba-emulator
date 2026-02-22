#include "xray.h"
#include "xray_draw.h"
#include "ppu/ppu.h"

/* Layer overlay colors (indexed by layer ID: 0-3=BG, 4=OBJ, 5=backdrop) */
static const uint32_t layer_colors[6] = {
    XRAY_COL_BG0, XRAY_COL_BG1, XRAY_COL_BG2, XRAY_COL_BG3,
    XRAY_COL_OBJ, XRAY_COL_BACKDROP
};

static const char* layer_names[6] = {
    "BG0", "BG1", "BG2", "BG3", "OBJ", "BDR"
};

static const char* ppu_mode_name(uint8_t mode) {
    switch (mode) {
    case 0:  return "Mode 0 (4x Tiled)";
    case 1:  return "Mode 1 (2T+1A)";
    case 2:  return "Mode 2 (2x Affine)";
    case 3:  return "Mode 3 (Bitmap 16b)";
    case 4:  return "Mode 4 (Bitmap 8b)";
    case 5:  return "Mode 5 (Bitmap 16b small)";
    default: return "Mode ???";
    }
}

void xray_capture_ppu_layers(PPU* ppu, XRayState* state) {
    if (!state || !state->active) return;

    uint8_t mode = ppu->dispcnt & 0x7;
    uint16_t saved_vcount = ppu->vcount;

    /* Saved scanline/layer state */
    uint16_t saved_scanline[SCREEN_WIDTH];
    uint8_t saved_top_layer[SCREEN_WIDTH];
    uint16_t saved_second_pixel[SCREEN_WIDTH];
    uint8_t saved_second_layer[SCREEN_WIDTH];

    memcpy(saved_scanline, ppu->scanline_buffer, sizeof(saved_scanline));
    memcpy(saved_top_layer, ppu->top_layer, sizeof(saved_top_layer));
    memcpy(saved_second_pixel, ppu->second_pixel, sizeof(saved_second_pixel));
    memcpy(saved_second_layer, ppu->second_layer, sizeof(saved_second_layer));

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
    memcpy(ppu->bg_ref_x, saved_ref_x, sizeof(saved_ref_x));
    memcpy(ppu->bg_ref_y, saved_ref_y, sizeof(saved_ref_y));
}

/* Half-resolution blit: sample every other pixel for 120x80 output */
static void blit_gba_half(uint32_t* buf, int buf_w, int buf_h,
                          int dst_x, int dst_y,
                          const uint16_t* src, int src_w, int src_h) {
    for (int sy = 0; sy < src_h; sy += 2) {
        int dy = dst_y + sy / 2;
        if (dy < 0 || dy >= buf_h) continue;
        for (int sx = 0; sx < src_w; sx += 2) {
            int dx = dst_x + sx / 2;
            if (dx >= 0 && dx < buf_w) {
                buf[dy * buf_w + dx] = gba_to_argb(src[sy * src_w + sx]);
            }
        }
    }
}

void xray_render_ppu(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                     int pw, int ph, PPU* ppu, XRayState* state) {
    (void)ph;
    (void)pw;

    int x0 = px + 4;
    int y0 = py + 16;

    uint8_t mode = ppu->dispcnt & 0x7;

    /* Half-resolution mini-views: 120x80 each.
     * 3 columns: 3*120 + 2*8 + margins = 392, fits in 640px.
     * 2 rows: 2*(80+14+4) + header = 212px, leaves room for info text. */
    int view_w = SCREEN_WIDTH / 2;   /* 120 */
    int view_h = SCREEN_HEIGHT / 2;  /* 80 */
    int gap = 8;
    int grid_x = x0 + 2;
    int grid_y = y0 + 2;

    typedef struct {
        const char* label;
        const uint16_t* data;
        bool active;
        int layer_id;
    } LayerView;

    LayerView views[6] = {
        {"BG0", state->layer_bg[0], BIT(ppu->dispcnt, 8) != 0, 0},
        {"BG1", state->layer_bg[1], BIT(ppu->dispcnt, 9) != 0, 1},
        {"BG2", state->layer_bg[2], BIT(ppu->dispcnt, 10) != 0, 2},
        {"BG3", state->layer_bg[3], BIT(ppu->dispcnt, 11) != 0, 3},
        {"OBJ", state->layer_obj, BIT(ppu->dispcnt, 12) != 0, 4},
        {"MAP", NULL, true, -1}
    };

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int vx = grid_x + col * (view_w + gap);
            int vy = grid_y + row * (view_h + 14 + gap);

            /* Label */
            uint32_t label_col = views[idx].active
                ? layer_colors[views[idx].layer_id < 0 ? 5 : views[idx].layer_id]
                : XRAY_COL_DIM;
            xray_draw_text(buf, buf_w, buf_h, vx, vy, views[idx].label,
                           label_col);
            if (!views[idx].active) {
                xray_draw_text(buf, buf_w, buf_h, vx + 32, vy, "(off)",
                               XRAY_COL_DIM);
            }
            vy += 10;

            /* View border */
            xray_draw_rect_outline(buf, buf_w, buf_h, vx - 1, vy - 1,
                                   view_w + 2, view_h + 2, XRAY_COL_BORDER);

            if (idx == 5) {
                /* Layer map overlay: composited framebuffer with color tinting
                 * based on which layer produced each pixel */
                for (int ly = 0; ly < SCREEN_HEIGHT; ly += 2) {
                    int dy = vy + ly / 2;
                    if (dy < 0 || dy >= buf_h) continue;
                    for (int lx = 0; lx < SCREEN_WIDTH; lx += 2) {
                        int dx = vx + lx / 2;
                        if (dx < 0 || dx >= buf_w) continue;

                        uint8_t layer = state->layer_map[ly][lx];
                        if (layer > 5) layer = 5;
                        uint32_t tint = layer_colors[layer];

                        uint16_t gba_pix = ppu->framebuffer[ly * SCREEN_WIDTH + lx];
                        uint32_t argb = gba_to_argb(gba_pix);

                        /* Blend: 50% original + 50% tint */
                        uint32_t r = ((argb >> 16) & 0xFF) / 2 +
                                     ((tint >> 16) & 0xFF) / 2;
                        uint32_t g = ((argb >> 8) & 0xFF) / 2 +
                                     ((tint >> 8) & 0xFF) / 2;
                        uint32_t b = (argb & 0xFF) / 2 + (tint & 0xFF) / 2;

                        buf[dy * buf_w + dx] =
                            0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            } else if (views[idx].active && views[idx].data) {
                blit_gba_half(buf, buf_w, buf_h, vx, vy, views[idx].data,
                              SCREEN_WIDTH, SCREEN_HEIGHT);
            } else {
                /* Inactive: fill with dark */
                xray_draw_rect(buf, buf_w, buf_h, vx, vy, view_w, view_h,
                               0xFF050510);
            }
        }
    }

    /* PPU info text below the views */
    int info_y = grid_y + 2 * (view_h + 14 + gap) + 4;
    xray_draw_textf(buf, buf_w, buf_h, x0, info_y, XRAY_COL_LABEL,
                    "%s", ppu_mode_name(mode));
    info_y += 12;

    /* Active layers list */
    int lx = x0;
    xray_draw_text(buf, buf_w, buf_h, lx, info_y, "Layers:", XRAY_COL_LABEL);
    lx += 64;
    for (int i = 0; i < 5; i++) {
        bool active = BIT(ppu->dispcnt, 8 + i);
        const char* name = (i < 4) ? layer_names[i] : "OBJ";
        uint32_t col = active ? layer_colors[i] : XRAY_COL_DIM;
        lx = xray_draw_text(buf, buf_w, buf_h, lx, info_y, name, col);
        lx += 8;
    }
    info_y += 12;

    /* Scroll offsets for tiled modes */
    if (mode <= 1) {
        for (int bg = 0; bg < (mode == 0 ? 4 : 2); bg++) {
            if (!BIT(ppu->dispcnt, 8 + bg)) continue;
            xray_draw_textf(buf, buf_w, buf_h, x0, info_y, layer_colors[bg],
                            "BG%d scroll: (%d, %d)  prio: %d", bg,
                            ppu->bg_hofs[bg], ppu->bg_vofs[bg],
                            ppu->bg_cnt[bg] & 3);
            info_y += 10;
        }
    }

    /* Blend mode info */
    uint8_t blend_mode = (ppu->bldcnt >> 6) & 3;
    const char* blend_names[] = {"None", "Alpha", "Brighten", "Darken"};
    xray_draw_textf(buf, buf_w, buf_h, x0, info_y, XRAY_COL_LABEL,
                    "Blend: %s  EVA=%d EVB=%d EVY=%d", blend_names[blend_mode],
                    ppu->bldalpha & 0x1F, (ppu->bldalpha >> 8) & 0x1F,
                    ppu->bldy & 0x1F);
}
