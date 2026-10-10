#include "ui/ui_canvas.h"
#include <math.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void ui_canvas_init(UiCanvas* c, uint32_t* px, int w, int h, float scale) {
    c->px = px; c->w = w; c->h = h; c->scale = scale;
    ui_canvas_unclip(c);
}

void ui_canvas_unclip(UiCanvas* c) {
    c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h;
}

void ui_canvas_clip(UiCanvas* c, UiRect r) {
    c->cx0 = clampi(ui_px(c, r.x), 0, c->w);
    c->cy0 = clampi(ui_px(c, r.y), 0, c->h);
    c->cx1 = clampi(ui_px(c, r.x + r.w), c->cx0, c->w);
    c->cy1 = clampi(ui_px(c, r.y + r.h), c->cy0, c->h);
}

void ui_canvas_clear(UiCanvas* c, uint32_t argb) {
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = argb | 0xFF000000u;
}

void ui_blend_coverage(UiCanvas* c, int x, int y, uint32_t argb, uint32_t coverage) {
    if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1 || coverage == 0) return;
    uint32_t a = ((argb >> 24) * coverage + 127) / 255;
    uint32_t* d = &c->px[y * c->w + x];
    if (a >= 255) { *d = argb | 0xFF000000u; return; }
    uint32_t inv = 255 - a, out = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        uint32_t s = (argb >> shift) & 0xFF, b = (*d >> shift) & 0xFF;
        out |= ((s * a + b * inv + 127) / 255) << shift;
    }
    *d = out;
}

void ui_fill_rect(UiCanvas* c, UiRect r, uint32_t argb) {
    if (r.w <= 0 || r.h <= 0) return;
    int x0 = clampi(ui_px(c, r.x), c->cx0, c->cx1), x1 = clampi(ui_px(c, r.x + r.w), c->cx0, c->cx1);
    int y0 = clampi(ui_px(c, r.y), c->cy0, c->cy1), y1 = clampi(ui_px(c, r.y + r.h), c->cy0, c->cy1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) ui_blend_coverage(c, x, y, argb, 255);
}

/* Coverage from a signed distance (pixels, negative inside), 1 px AA band. */
static uint32_t sdf_coverage(float d) {
    float a = 0.5f - d;
    if (a <= 0.0f) return 0;
    if (a >= 1.0f) return 255;
    return (uint32_t)(a * 255.0f + 0.5f);
}

void ui_fill_round_rect(UiCanvas* c, UiRect r, float radius, uint32_t argb) {
    if (r.w <= 0 || r.h <= 0) return;
    /* Snap the box to whole pixels so straight edges stay crisp; only corners anti-alias. */
    float x0 = (float)ui_px(c, r.x), x1 = (float)ui_px(c, r.x + r.w);
    float y0 = (float)ui_px(c, r.y), y1 = (float)ui_px(c, r.y + r.h);
    float hw = (x1 - x0) * 0.5f, hh = (y1 - y0) * 0.5f;
    if (hw <= 0 || hh <= 0) return;
    float rad = radius * c->scale;
    if (rad > hw) rad = hw;
    if (rad > hh) rad = hh;
    float cx = x0 + hw, cy = y0 + hh;
    int ix0 = clampi((int)x0, c->cx0, c->cx1), ix1 = clampi((int)x1, c->cx0, c->cx1);
    int iy0 = clampi((int)y0, c->cy0, c->cy1), iy1 = clampi((int)y1, c->cy0, c->cy1);
    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            float qx = fabsf(x + 0.5f - cx) - (hw - rad), qy = fabsf(y + 0.5f - cy) - (hh - rad);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float inside = qx > qy ? qx : qy;
            float d = sqrtf(ox * ox + oy * oy) + (inside < 0 ? inside : 0) - rad;
            ui_blend_coverage(c, x, y, argb, sdf_coverage(d));
        }
    }
}

void ui_hline(UiCanvas* c, float x, float y, float w, uint32_t argb) {
    if (w <= 0) return;
    int py = (int)(y * c->scale);
    int x0 = clampi(ui_px(c, x), c->cx0, c->cx1), x1 = clampi(ui_px(c, x + w), c->cx0, c->cx1);
    for (int px = x0; px < x1; px++) ui_blend_coverage(c, px, py, argb, 255);
}

void ui_ring(UiCanvas* c, float cx, float cy, float r, float thickness, uint32_t argb) {
    float pcx = cx * c->scale, pcy = cy * c->scale, pr = r * c->scale, ht = thickness * c->scale * 0.5f;
    int x0 = clampi((int)(pcx - pr - ht - 1), c->cx0, c->cx1), x1 = clampi((int)(pcx + pr + ht + 2), c->cx0, c->cx1);
    int y0 = clampi((int)(pcy - pr - ht - 1), c->cy0, c->cy1), y1 = clampi((int)(pcy + pr + ht + 2), c->cy0, c->cy1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - pcx, dy = y + 0.5f - pcy;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - pr) - ht;
            ui_blend_coverage(c, x, y, argb, sdf_coverage(d));
        }
}

void ui_capsule(UiCanvas* c, float ax, float ay, float bx, float by, float thickness, uint32_t argb) {
    float s = c->scale, pax = ax * s, pay = ay * s, pbx = bx * s, pby = by * s, ht = thickness * s * 0.5f;
    float minx = (pax < pbx ? pax : pbx) - ht - 1, maxx = (pax > pbx ? pax : pbx) + ht + 2;
    float miny = (pay < pby ? pay : pby) - ht - 1, maxy = (pay > pby ? pay : pby) + ht + 2;
    int x0 = clampi((int)minx, c->cx0, c->cx1), x1 = clampi((int)maxx, c->cx0, c->cx1);
    int y0 = clampi((int)miny, c->cy0, c->cy1), y1 = clampi((int)maxy, c->cy0, c->cy1);
    float vx = pbx - pax, vy = pby - pay, len2 = vx * vx + vy * vy;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f - pax, py = y + 0.5f - pay;
            float t = len2 > 0 ? (px * vx + py * vy) / len2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float dx = px - vx * t, dy = py - vy * t;
            ui_blend_coverage(c, x, y, argb, sdf_coverage(sqrtf(dx * dx + dy * dy) - ht));
        }
}
