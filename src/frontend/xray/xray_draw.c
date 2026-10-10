#include "xray.h"
#include "frontend/overlay_draw.h"
#include "gba.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

/* Global pointer — NULL when X-Ray is not wired up */
XRayState* g_xray = NULL;

void xray_init(XRayState* state) {
    memset(state, 0, sizeof(XRayState));
}

void xray_decay_flash(XRayState* s) {
    for (int i = 0; i < 4; i++) {
        if (s->timer_flash[i] > 0) s->timer_flash[i]--;
        if (s->dma_flash[i] > 0) s->dma_flash[i]--;
    }
    for (int i = 0; i < 16; i++) {
        if (s->irq_flash[i] > 0) s->irq_flash[i]--;
    }
}

/* Friendly first (top), technical last (bottom). */
const UiRect xray_cards[XRAY_CARD_COUNT] = {
    [XRAY_CARD_SEE]    = {12, 12, 660, 300},
    [XRAY_CARD_HEAR]   = {682, 12, 506, 300},
    [XRAY_CARD_TILES]  = {12, 322, 660, 290},
    [XRAY_CARD_CPU]    = {682, 322, 506, 290},
    [XRAY_CARD_SCENES] = {12, 622, 1176, 186},
};

const char* const xray_titles[XRAY_CARD_COUNT] = {
    [XRAY_CARD_SEE]    = "What you see",
    [XRAY_CARD_HEAR]   = "What you hear",
    [XRAY_CARD_TILES]  = "Tiles and colors",
    [XRAY_CARD_CPU]    = "Processor",
    [XRAY_CARD_SCENES] = "Behind the scenes",
};

const char* const xray_captions[XRAY_CARD_COUNT] = {
    [XRAY_CARD_SEE]    = "The GBA builds each frame from up to 4 background layers plus sprites. "
                         "Here is each one on its own.",
    [XRAY_CARD_HEAR]   = "Music and sound effects. Most of it plays on two sample channels.",
    [XRAY_CARD_TILES]  = "The 8x8 picture pieces and color palettes stored in video memory.",
    [XRAY_CARD_CPU]    = "What the ARM CPU is doing right now. Registers are its 16 working values.",
    [XRAY_CARD_SCENES] = "Timers keep the game on schedule, DMA copies memory without the CPU, and "
                         "interrupts tell the CPU something just happened.",
};

XRayFit xray_fit(int usable_w_pt, int usable_h_pt, float px_per_pt) {
    float scale = 1.0f;
    if (usable_w_pt > 0 && usable_h_pt > XRAY_TITLEBAR_PT) {
        float sw = (float)usable_w_pt / XRAY_W_PT;
        float sh = (float)(usable_h_pt - XRAY_TITLEBAR_PT) / XRAY_H_PT;
        if (sw < scale) scale = sw;
        if (sh < scale) scale = sh;
    }
    if (px_per_pt <= 0) px_per_pt = 1.0f;
    /* Never shrink the smallest text below 10 pixels. */
    float floor_scale = 10.0f / (XRAY_MIN_TEXT_PT * px_per_pt);
    if (scale < floor_scale) scale = floor_scale;
    if (scale > 1.0f) scale = 1.0f;
    XRayFit f = {scale, (int)ceilf(XRAY_W_PT * scale), (int)ceilf(XRAY_H_PT * scale)};
    return f;
}

void xray_canvas_size(int out_w, int out_h, int* cw, int* ch, float* density) {
    if (out_w > XRAY_FB_MAX_W) {
        /* Denser than 2x: draw at 2x and let SDL scale the texture up. */
        *density = XRAY_MAX_DENSITY;
        *cw = XRAY_FB_MAX_W;
        *ch = XRAY_FB_MAX_H;
        return;
    }
    *density = (float)out_w / XRAY_W_PT;
    *cw = out_w;
    *ch = out_h > XRAY_FB_MAX_H ? XRAY_FB_MAX_H : out_h;
}

float xray_textf(UiCanvas* c, UiFont f, float size, float x, float y, float max_w, UiAlign a,
                 uint32_t argb, const char* fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return ui_text(c, f, size, x, y, max_w, a, argb, buf);
}

/* Straight edges are plain fills; only the four corner squares pay for anti-aliasing. */
static void fill_card(UiCanvas* c, UiRect r) {
    float rad = UI_RADIUS, d = 2 * rad;
    ui_fill_rect(c, (UiRect){r.x + rad, r.y, r.w - d, r.h}, UI_SURFACE);
    ui_fill_rect(c, (UiRect){r.x, r.y + rad, rad, r.h - d}, UI_SURFACE);
    ui_fill_rect(c, (UiRect){r.x + r.w - rad, r.y + rad, rad, r.h - d}, UI_SURFACE);
    ui_fill_round_rect(c, (UiRect){r.x, r.y, d, d}, rad, UI_SURFACE);
    ui_fill_round_rect(c, (UiRect){r.x + r.w - d, r.y, d, d}, rad, UI_SURFACE);
    ui_fill_round_rect(c, (UiRect){r.x, r.y + r.h - d, d, d}, rad, UI_SURFACE);
    ui_fill_round_rect(c, (UiRect){r.x + r.w - d, r.y + r.h - d, d, d}, rad, UI_SURFACE);
}

float xray_card(UiCanvas* c, XRayCard id, const char* pill) {
    UiRect r = xray_cards[id];
    fill_card(c, r);
    float x = r.x + XRAY_PAD;
    float tw = ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_TITLE, x, r.y + 11.0f, 0, UI_ALIGN_LEFT,
                       UI_TEXT, xray_titles[id]);
    if (pill) {
        float mid = r.y + 11.0f + ui_text_cap_middle(c, UI_FONT_SEMIBOLD, XRAY_SIZE_TITLE);
        ui_pill(c, x + tw + 10.0f, mid - (UI_SIZE_PILL + 5.0f) / 2, pill, UI_SELECTED);
    }
    ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_CAPTION, x, r.y + 33.0f, r.w - 2 * XRAY_PAD,
            UI_ALIGN_LEFT, UI_MUTED, xray_captions[id]);
    return r.y + 56.0f;
}

void xray_blit_argb(UiCanvas* c, UiRect dst, const uint32_t* src, int sw, int sh) {
    int x0 = ui_px(c, dst.x), y0 = ui_px(c, dst.y);
    int dw = ui_px(c, dst.x + dst.w) - x0, dh = ui_px(c, dst.y + dst.h) - y0;
    if (dw <= 0 || dh <= 0) return;
    int ya = y0 > c->cy0 ? y0 : c->cy0, yb = y0 + dh < c->cy1 ? y0 + dh : c->cy1;
    int xa = x0 > c->cx0 ? x0 : c->cx0, xb = x0 + dw < c->cx1 ? x0 + dw : c->cx1;
    for (int y = ya; y < yb; y++) {
        const uint32_t* row = src + (size_t)((y - y0) * sh / dh) * (size_t)sw;
        for (int x = xa; x < xb; x++) c->px[y * c->w + x] = row[(x - x0) * sw / dw] | 0xFF000000u;
    }
}

void xray_blit_555(UiCanvas* c, UiRect dst, const uint16_t* src, int sw, int sh) {
    int x0 = ui_px(c, dst.x), y0 = ui_px(c, dst.y);
    int dw = ui_px(c, dst.x + dst.w) - x0, dh = ui_px(c, dst.y + dst.h) - y0;
    if (dw <= 0 || dh <= 0) return;
    int ya = y0 > c->cy0 ? y0 : c->cy0, yb = y0 + dh < c->cy1 ? y0 + dh : c->cy1;
    int xa = x0 > c->cx0 ? x0 : c->cx0, xb = x0 + dw < c->cx1 ? x0 + dw : c->cx1;
    for (int y = ya; y < yb; y++) {
        const uint16_t* row = src + (size_t)((y - y0) * sh / dh) * (size_t)sw;
        for (int x = xa; x < xb; x++) c->px[y * c->w + x] = gba_to_argb(row[(x - x0) * sw / dw]);
    }
}

void xray_draw(UiCanvas* c, GBA* gba, XRayState* s) {
    ui_canvas_clear(c, UI_BG);
    xray_render_ppu(c, &gba->ppu, s);
    xray_render_audio(c, &gba->apu, s);
    xray_render_tiles(c, &gba->ppu, s);
    xray_render_cpu(c, &gba->cpu);
#ifdef ENABLE_REWIND
    UiRect cpu = xray_cards[XRAY_CARD_CPU];
    xray_render_rewind(c, cpu.x + XRAY_PAD, cpu.y + cpu.h - 24.0f, cpu.w - 2 * XRAY_PAD,
                       &gba->rewind);
#endif
    xray_render_activity(c, gba->timers, &gba->dma, &gba->interrupts, s);
}
