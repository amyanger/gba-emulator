#include "frontend/panel_layout.h"

#include <math.h>

#define GBA_W 240
#define GBA_H 160
#define MIN_BODY_PX 10.0f
#define BODY_PT 13.0f

static int round_px(float v) { return (int)(v + 0.5f); }

/* Window size in window units for a game scale and panel density. Rounds up
 * (with a small float tolerance) so the drawable is never 1 px short of the
 * planned content, which would drop the game to the next integer scale down. */
static void window_units(const LayoutInput* in, int gps, float density, int* w, int* h) {
    float pw = in->panel_visible ? PANEL_W_PT * density : 0.0f;
    float ph = in->panel_visible ? PANEL_H_PT * density : 0.0f;
    float gw = (float)(GBA_W * gps);
    float gh = (float)(GBA_H * gps);
    float hh = gh > ph ? gh : ph;
    *w = (int)ceilf((gw + pw) / in->pt_to_px - 0.001f);
    *h = (int)ceilf(hh / in->pt_to_px - 0.001f);
}

LayoutPlan panel_layout_plan(const LayoutInput* in) {
    static const float steps[] = {1.0f, 0.9f, 0.8f};
    LayoutPlan p;
    LayoutInput c = *in;
    float ui;
    int gps;
    float density;
    bool known;

    /* !(x > 0) also catches NaN. */
    if (!(c.pt_to_px > 0.0f)) c.pt_to_px = 1.0f;
    if (!(c.os_scale > 0.0f)) c.os_scale = 1.0f;
    if (c.game_scale < 1) c.game_scale = 1;
    if (c.game_scale > 10) c.game_scale = 10;

    ui = c.pt_to_px * c.os_scale;
    gps = round_px((float)c.game_scale * ui);
    if (gps < 1) gps = 1;
    density = ui;
    if (BODY_PT * density < MIN_BODY_PX) density = MIN_BODY_PX / BODY_PT;
    known = c.usable_w_pt > 0 && c.usable_h_pt > 0;

    window_units(&c, gps, density, &p.window_w, &p.window_h);

    if (known) {
        bool fits = false;
        float base = density;
        int nsteps = c.panel_visible ? 3 : 1;
        for (int i = 0; i < nsteps && !fits; i++) {
            float d = base * steps[i];
            if (i > 0 && BODY_PT * d < MIN_BODY_PX) continue;
            density = d;
            window_units(&c, gps, density, &p.window_w, &p.window_h);
            fits = p.window_w <= c.usable_w_pt && p.window_h <= c.usable_h_pt;
        }
        while (!fits && gps > 1) {
            gps--;
            window_units(&c, gps, density, &p.window_w, &p.window_h);
            fits = p.window_w <= c.usable_w_pt && p.window_h <= c.usable_h_pt;
        }
    }

    p.density = density;
    p.game_px_scale = gps;
    return p;
}

static void clamp_rect(PxRect* r, int out_w, int out_h) {
    if (r->x < 0) { r->w += r->x; r->x = 0; }
    if (r->y < 0) { r->h += r->y; r->y = 0; }
    if (r->x > out_w) r->x = out_w;
    if (r->y > out_h) r->y = out_h;
    if (r->w < 0) r->w = 0;
    if (r->h < 0) r->h = 0;
    if (r->x + r->w > out_w) r->w = out_w - r->x;
    if (r->y + r->h > out_h) r->h = out_h - r->y;
}

void panel_layout_rects(const LayoutPlan* plan, bool panel_visible, int out_w, int out_h,
                        PxRect* game, PxRect* panel) {
    int pw = panel_visible ? round_px(PANEL_W_PT * plan->density) : 0;
    int ph = panel_visible ? round_px(PANEL_H_PT * plan->density) : 0;
    int gx, gy, g, content_w, x0;

    if (out_w < 0) out_w = 0;
    if (out_h < 0) out_h = 0;

    /* Shrink the panel uniformly if it cannot fit beside a 1x game. */
    if (panel_visible && (GBA_W + pw > out_w || ph > out_h)) {
        float fw = (float)(out_w - GBA_W) / (float)pw;
        float fh = (float)out_h / (float)ph;
        float f = fw < fh ? fw : fh;
        if (f < 0.0f) f = 0.0f;
        pw = (int)((float)pw * f);
        ph = (int)((float)ph * f);
    }

    gx = (out_w - pw) / GBA_W;
    gy = out_h / GBA_H;
    g = gx < gy ? gx : gy;
    if (g < 1) g = 1;
    content_w = GBA_W * g + pw;
    x0 = (out_w - content_w) / 2;

    game->x = x0;
    game->w = GBA_W * g;
    game->h = GBA_H * g;
    game->y = (out_h - game->h) / 2;

    if (panel_visible) {
        panel->x = x0 + game->w;
        panel->w = pw;
        panel->h = ph;
        panel->y = (out_h - ph) / 2;
    } else {
        panel->x = panel->y = panel->w = panel->h = 0;
    }
    clamp_rect(game, out_w, out_h);
    clamp_rect(panel, out_w, out_h);
}
