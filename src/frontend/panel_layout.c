#include "frontend/panel_layout.h"

#define GBA_W 240
#define GBA_H 160
#define MIN_BODY_PX 10.0f
#define BODY_PT 13.0f

static int round_px(float v) { return (int)(v + 0.5f); }

/* Window size in window units for a game scale and panel density. */
static void window_units(const LayoutInput* in, int gps, float density, int* w, int* h) {
    float pw = in->panel_visible ? PANEL_W_PT * density : 0.0f;
    float ph = in->panel_visible ? PANEL_H_PT * density : 0.0f;
    float gw = (float)(GBA_W * gps);
    float gh = (float)(GBA_H * gps);
    float hh = gh > ph ? gh : ph;
    *w = round_px((gw + pw) / in->pt_to_px);
    *h = round_px(hh / in->pt_to_px);
}

LayoutPlan panel_layout_plan(const LayoutInput* in) {
    static const float steps[] = {1.0f, 0.9f, 0.8f};
    LayoutPlan p;
    float ui = in->pt_to_px * in->os_scale;
    int gps = round_px((float)in->game_scale * ui);
    float density = ui;
    bool known = in->usable_w_pt > 0 && in->usable_h_pt > 0;

    if (gps < 1) gps = 1;
    window_units(in, gps, density, &p.window_w, &p.window_h);

    if (known) {
        bool fits = false;
        for (int i = 0; i < 3 && !fits; i++) {
            float d = ui * steps[i];
            if (i > 0 && BODY_PT * d < MIN_BODY_PX) continue;
            density = d;
            window_units(in, gps, density, &p.window_w, &p.window_h);
            fits = p.window_w <= in->usable_w_pt && p.window_h <= in->usable_h_pt;
        }
        while (!fits && gps > 1) {
            gps--;
            window_units(in, gps, density, &p.window_w, &p.window_h);
            fits = p.window_w <= in->usable_w_pt && p.window_h <= in->usable_h_pt;
        }
    }

    p.density = density;
    p.game_px_scale = gps;
    return p;
}

void panel_layout_rects(const LayoutPlan* plan, bool panel_visible, int out_w, int out_h,
                        PxRect* game, PxRect* panel) {
    int pw = panel_visible ? round_px(PANEL_W_PT * plan->density) : 0;
    int ph = panel_visible ? round_px(PANEL_H_PT * plan->density) : 0;
    int gx = (out_w - pw) / GBA_W;
    int gy = out_h / GBA_H;
    int g = gx < gy ? gx : gy;
    int content_w, x0;

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
}
