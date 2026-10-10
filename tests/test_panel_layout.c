#include "test_harness.h"
#include "frontend/panel_layout.h"

#include <math.h>

static LayoutInput input(int scale, float pt_to_px, float os_scale, int uw, int uh) {
    LayoutInput in;
    in.game_scale = scale;
    in.panel_visible = true;
    in.pt_to_px = pt_to_px;
    in.os_scale = os_scale;
    in.usable_w_pt = uw;
    in.usable_h_pt = uh;
    return in;
}

TEST(layout_retina_scale3) {
    LayoutInput in = input(3, 2.0f, 1.0f, 0, 0);
    LayoutPlan p = panel_layout_plan(&in);
    ASSERT_EQ((int)p.density, 2);
    ASSERT_EQ(p.game_px_scale, 6);
    ASSERT_EQ(p.window_w, 720 + 600);
    ASSERT_EQ(p.window_h, 480);
}

TEST(layout_standard_monitor_scale2) {
    LayoutInput in = input(2, 1.0f, 1.0f, 0, 0);
    LayoutPlan p = panel_layout_plan(&in);
    PxRect g, pn;
    ASSERT_EQ((int)p.density, 1);
    ASSERT_EQ(p.game_px_scale, 2);
    ASSERT_EQ(p.window_w, 1080);
    ASSERT_EQ(p.window_h, 480);
    panel_layout_rects(&p, true, 1080, 480, &g, &pn);
    ASSERT_EQ(g.x, 0);
    ASSERT_EQ(g.y, 80);
    ASSERT_EQ(g.w, 480);
    ASSERT_EQ(g.h, 320);
    ASSERT_EQ(pn.x, 480);
    ASSERT_EQ(pn.y, 0);
    ASSERT_EQ(pn.w, 600);
    ASSERT_EQ(pn.h, 480);
}

TEST(layout_x11_dpi_150) {
    LayoutInput in = input(3, 1.0f, 1.5f, 0, 0);
    LayoutPlan p = panel_layout_plan(&in);
    ASSERT_EQ((int)(p.density * 100.0f + 0.5f), 150);
    ASSERT_EQ(p.game_px_scale, 5);
}

TEST(layout_fits_small_display) {
    LayoutInput in = input(4, 1.0f, 1.0f, 1366, 728);
    LayoutPlan p = panel_layout_plan(&in);
    ASSERT_EQ((int)(p.density * 100.0f + 0.5f), 80);
    ASSERT_EQ(p.game_px_scale, 3);
    ASSERT_EQ(p.window_w, 720 + 480);
    ASSERT_EQ(p.window_h, 480);
    ASSERT_TRUE(13.0f * p.density >= 10.0f);
}

TEST(layout_fullscreen_grows_game) {
    LayoutInput in = input(2, 1.0f, 1.0f, 0, 0);
    LayoutPlan p = panel_layout_plan(&in);
    PxRect g, pn;
    panel_layout_rects(&p, true, 1920, 1080, &g, &pn);
    ASSERT_EQ(g.w, 1200);
    ASSERT_EQ(g.h, 800);
    ASSERT_EQ(g.x, 60);
    ASSERT_EQ(g.y, 140);
    ASSERT_EQ(pn.x, 1260);
    ASSERT_EQ(pn.y, 300);
    ASSERT_EQ(pn.w, 600);
    ASSERT_EQ(pn.h, 480);
}

TEST(layout_no_panel) {
    LayoutInput in = input(2, 1.0f, 1.0f, 0, 0);
    LayoutPlan p;
    PxRect g, pn;
    in.panel_visible = false;
    p = panel_layout_plan(&in);
    ASSERT_EQ(p.window_w, 480);
    ASSERT_EQ(p.window_h, 320);
    panel_layout_rects(&p, false, 1000, 400, &g, &pn);
    ASSERT_EQ(pn.w, 0);
    ASSERT_EQ(pn.h, 0);
    ASSERT_EQ(g.w, 480);
    ASSERT_EQ(g.h, 320);
    ASSERT_EQ(g.x, 260);
    ASSERT_EQ(g.y, 40);
}

TEST(layout_recomputes_for_new_density) {
    LayoutInput in = input(3, 1.0f, 1.0f, 0, 0);
    LayoutPlan a = panel_layout_plan(&in);
    LayoutPlan b;
    in.pt_to_px = 2.0f;
    b = panel_layout_plan(&in);
    ASSERT_EQ((int)a.density, 1);
    ASSERT_EQ((int)b.density, 2);
    ASSERT_EQ(a.window_w, 240 * 3 + 600);
    ASSERT_EQ(b.window_w, 240 * 3 + 600);
}

static void check_inside(PxRect r, int w, int h) {
    ASSERT_TRUE(r.x >= 0 && r.y >= 0 && r.w >= 0 && r.h >= 0);
    ASSERT_TRUE(r.x + r.w <= w && r.y + r.h <= h);
}

TEST(layout_text_floor_on_base_density) {
    LayoutInput in = input(2, 1.0f, 0.75f, 0, 0);
    LayoutPlan p = panel_layout_plan(&in);
    ASSERT_TRUE(13.0f * p.density >= 9.999f);
    in = input(2, 0.5f, 1.0f, 0, 0);
    p = panel_layout_plan(&in);
    ASSERT_TRUE(13.0f * p.density >= 9.999f);
    in = input(2, 0.5f, 1.0f, 1366, 728);
    p = panel_layout_plan(&in);
    ASSERT_TRUE(13.0f * p.density >= 9.999f);
}

TEST(layout_sanitizes_input) {
    LayoutInput in = input(0, 0.0f, 0.0f, -5, -5);
    LayoutPlan p = panel_layout_plan(&in);
    ASSERT_TRUE(p.window_w > 0 && p.window_h > 0 && p.game_px_scale >= 1);
    in = input(99, 1.0f, 1.0f, 0, 0);
    p = panel_layout_plan(&in);
    ASSERT_EQ(p.game_px_scale, 10);
    ASSERT_EQ(p.window_w, 2400 + 600);
    volatile float zero = 0.0f; /* MSVC rejects a constant 0.0f / 0.0f (C2124) */
    in = input(3, zero / zero, 1.0f, 0, 0);
    p = panel_layout_plan(&in);
    ASSERT_EQ(p.game_px_scale, 3);
}

TEST(layout_rects_stay_inside_small_drawable) {
    LayoutInput in = input(2, 1.0f, 1.0f, 0, 0);
    LayoutPlan p = panel_layout_plan(&in);
    PxRect g, pn;
    panel_layout_rects(&p, true, 300, 480, &g, &pn);
    check_inside(g, 300, 480);
    check_inside(pn, 300, 480);
    panel_layout_rects(&p, true, 1080, 100, &g, &pn);
    check_inside(g, 1080, 100);
    check_inside(pn, 1080, 100);
    panel_layout_rects(&p, true, 0, 0, &g, &pn);
    check_inside(g, 0, 0);
    check_inside(pn, 0, 0);
    panel_layout_rects(&p, false, -4, -4, &g, &pn);
    check_inside(g, 0, 0);
}

TEST(layout_hidden_panel_window_is_shorter) {
    LayoutInput in = input(2, 1.0f, 1.0f, 0, 0);
    LayoutPlan shown = panel_layout_plan(&in);
    LayoutPlan hidden;
    in.panel_visible = false;
    hidden = panel_layout_plan(&in);
    ASSERT_EQ(hidden.window_h, 320);
    ASSERT_TRUE(hidden.window_h < shown.window_h);
}

/* The planned window, once the OS turns it into a drawable (floored or rounded),
 * must still hold the planned integer game scale. Rounding the window size down
 * used to leave the drawable 1 px short, so the game dropped to gps - 1. */
TEST(layout_drawable_keeps_game_scale) {
    static const float ratios[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 3.0f};
    for (int r = 0; r < 7; r++) {
        for (int s = 1; s <= 6; s++) {
            for (int pv = 0; pv < 2; pv++) {
                LayoutInput in = input(s, ratios[r], 1.0f, 0, 0);
                LayoutPlan p;
                in.panel_visible = pv != 0;
                p = panel_layout_plan(&in);
                for (int mode = 0; mode < 2; mode++) {
                    float fw = (float)p.window_w * ratios[r];
                    float fh = (float)p.window_h * ratios[r];
                    int ow = mode ? (int)lroundf(fw) : (int)fw;
                    int oh = mode ? (int)lroundf(fh) : (int)fh;
                    PxRect g, pn;
                    panel_layout_rects(&p, in.panel_visible, ow, oh, &g, &pn);
                    ASSERT_EQ(g.w / 240, p.game_px_scale);
                }
            }
        }
    }
}

static PxRect rect(int x, int y, int w, int h) {
    PxRect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

TEST(clamp_window_already_fits) {
    PxRect u = rect(0, 25, 1440, 875);
    int x = 100, y = 200;
    panel_layout_clamp_window(&x, &y, 720, 480, 28, &u);
    ASSERT_EQ(x, 100);
    ASSERT_EQ(y, 200);
}

TEST(clamp_window_overflows_right) {
    PxRect u = rect(0, 25, 1440, 875);
    int x = 900, y = 200;
    panel_layout_clamp_window(&x, &y, 1320, 480, 28, &u);
    ASSERT_EQ(x, 120);
    ASSERT_EQ(y, 200);
}

TEST(clamp_window_overflows_bottom) {
    PxRect u = rect(0, 25, 1440, 875);
    int x = 100, y = 600;
    panel_layout_clamp_window(&x, &y, 720, 480, 28, &u);
    ASSERT_EQ(x, 100);
    ASSERT_EQ(y, 900 - 480);
}

TEST(clamp_window_larger_than_usable) {
    PxRect u = rect(0, 25, 1440, 875);
    int x = 300, y = 300;
    panel_layout_clamp_window(&x, &y, 2000, 1200, 28, &u);
    ASSERT_EQ(x, 0);
    ASSERT_EQ(y, 25 + 28);
    ASSERT_TRUE(y - 28 >= u.y);
}

TEST(clamp_window_second_monitor) {
    PxRect u = rect(1440, 25, 1920, 1055);
    int x = 2500, y = 700;
    panel_layout_clamp_window(&x, &y, 1320, 480, 28, &u);
    ASSERT_EQ(x, 1440 + 1920 - 1320);
    ASSERT_EQ(y, 25 + 1055 - 480);
    x = 1500;
    y = 100;
    panel_layout_clamp_window(&x, &y, 1320, 480, 28, &u);
    ASSERT_EQ(x, 1500);
    ASSERT_EQ(y, 100);
}

TEST(clamp_window_pulled_in_from_left_and_above) {
    PxRect u = rect(1440, 25, 1920, 1055);
    int x = 1000, y = 10;
    panel_layout_clamp_window(&x, &y, 720, 480, 28, &u);
    ASSERT_EQ(x, 1440);
    ASSERT_EQ(y, 25 + 28);
    x = 1000;
    y = 10;
    panel_layout_clamp_window(&x, &y, 720, 480, -5, &u);
    ASSERT_EQ(x, 1440);
    ASSERT_EQ(y, 25);
}

TEST(clamp_window_unknown_bounds) {
    PxRect u = rect(0, 0, 0, 900);
    int x = 5000, y = -300;
    panel_layout_clamp_window(&x, &y, 1320, 480, 28, &u);
    ASSERT_EQ(x, 5000);
    ASSERT_EQ(y, -300);
    u = rect(0, 0, 1440, 0);
    panel_layout_clamp_window(&x, &y, 1320, 480, 28, &u);
    ASSERT_EQ(x, 5000);
    ASSERT_EQ(y, -300);
}

void run_panel_layout_tests(void) {
    TEST_SUITE("panel_layout");
    RUN_TEST(layout_retina_scale3);
    RUN_TEST(layout_standard_monitor_scale2);
    RUN_TEST(layout_x11_dpi_150);
    RUN_TEST(layout_fits_small_display);
    RUN_TEST(layout_fullscreen_grows_game);
    RUN_TEST(layout_no_panel);
    RUN_TEST(layout_recomputes_for_new_density);
    RUN_TEST(layout_text_floor_on_base_density);
    RUN_TEST(layout_sanitizes_input);
    RUN_TEST(layout_rects_stay_inside_small_drawable);
    RUN_TEST(layout_hidden_panel_window_is_shorter);
    RUN_TEST(layout_drawable_keeps_game_scale);
    RUN_TEST(clamp_window_already_fits);
    RUN_TEST(clamp_window_overflows_right);
    RUN_TEST(clamp_window_overflows_bottom);
    RUN_TEST(clamp_window_larger_than_usable);
    RUN_TEST(clamp_window_second_monitor);
    RUN_TEST(clamp_window_pulled_in_from_left_and_above);
    RUN_TEST(clamp_window_unknown_bounds);
}
