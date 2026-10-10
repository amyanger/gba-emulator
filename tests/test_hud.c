#include "test_harness.h"
#include "frontend/hud_draw.h"
#include "screenshot/screenshot.h"

#include <stdlib.h>

#define HW 720
#define HH 480
static uint32_t s_hud[HW * HH];
static ToastQueue q;

static UiCanvas canvas(int w, int h, float density) {
    UiCanvas c;
    ui_canvas_init(&c, s_hud, w, h, density);
    return c;
}

TEST(hud_strip_fits_badges_and_cap_toasts) {
    float h = hud_strip_height_pt();
    ASSERT_TRUE(h > 250.0f && h < 320.0f);
}

TEST(hud_visible_needs_toast_or_badge) {
    toast_init(&q);
    ASSERT_TRUE(!hud_visible(&q));
    toast_set_badge(&q, BADGE_MUTED, true);
    ASSERT_TRUE(hud_visible(&q));
    toast_init(&q);
    toast_push(&q, 0, TOAST_OK, "A", NULL);
    ASSERT_TRUE(hud_visible(&q));
}

TEST(hud_badges_fixed_order) {
    toast_init(&q);
    toast_set_badge(&q, BADGE_MUTED, true);
    toast_set_badge(&q, BADGE_PAUSED, true);
    UiCanvas c = canvas(HW, HH, 1.0f);
    HudLayout l;
    hud_layout(&c, &q, 0, (float)HW, &l);
    ASSERT_TRUE(l.badges[BADGE_PAUSED].w > 0 && l.badges[BADGE_MUTED].w > 0);
    ASSERT_EQ(l.badges[BADGE_FAST].w, 0);
    ASSERT_TRUE(l.badges[BADGE_PAUSED].x < l.badges[BADGE_MUTED].x);
    ASSERT_EQ((int)l.badges[BADGE_PAUSED].x, 10);
}

TEST(hud_stack_moves_below_badges_only_when_overlapping) {
    UiCanvas c = canvas(HW, HH, 1.0f);
    HudLayout l;

    toast_init(&q);
    toast_push(&q, 0, TOAST_OK, "State saved", "Slot 3");
    hud_layout(&c, &q, 1000, (float)HW, &l);
    ASSERT_EQ((int)l.toasts[0].y, 10);                 /* no badges: top */
    ASSERT_TRUE(!l.below_badges);

    toast_set_badge(&q, BADGE_PAUSED, true);
    hud_layout(&c, &q, 1000, (float)HW, &l);
    ASSERT_TRUE(!l.below_badges);                      /* wide area, short toast: no overlap */
    ASSERT_EQ((int)l.toasts[0].y, 10);

    hud_layout(&c, &q, 1000, 260.0f, &l);              /* narrow area: centered card hits badge */
    ASSERT_TRUE(l.below_badges);
    ASSERT_TRUE(l.toasts[0].y >= l.badges[BADGE_PAUSED].y + l.badges[BADGE_PAUSED].h);
}

TEST(hud_layout_caps_width_and_drops_detail) {
    UiCanvas c = canvas(HW, HH, 1.0f);
    HudLayout l;
    toast_init(&q);
    toast_push(&q, 0, TOAST_INFO, "Controller connected with a very long title here",
               "Some Extremely Long Third Party Controller Name");
    hud_layout(&c, &q, 1000, 240.0f, &l);
    ASSERT_TRUE(l.toasts[0].w <= 240.0f - 20.0f + 0.01f);
    ASSERT_TRUE(l.toasts[0].x >= 10.0f - 0.01f);
    ASSERT_EQ(l.detail_w[0], 0);                       /* detail dropped first */
    ASSERT_TRUE(l.title_w[0] > 0);
}

TEST(hud_layout_keeps_short_detail) {
    UiCanvas c = canvas(HW, HH, 1.0f);
    HudLayout l;
    toast_init(&q);
    toast_push(&q, 0, TOAST_OK, "State saved", "Slot 3");
    hud_layout(&c, &q, 1000, (float)HW, &l);
    ASSERT_TRUE(l.detail_w[0] > 0);
    ASSERT_TRUE(l.toasts[0].x + l.toasts[0].w / 2 > HW / 2 - 1 &&
                l.toasts[0].x + l.toasts[0].w / 2 < HW / 2 + 1);   /* centered */
}

TEST(hud_slide_offsets_new_toast) {
    UiCanvas c = canvas(HW, HH, 1.0f);
    HudLayout l;
    toast_init(&q);
    toast_push(&q, 1000, TOAST_OK, "A", NULL);
    hud_layout(&c, &q, 1000, (float)HW, &l);
    ASSERT_TRUE(l.toasts[0].y < 10.0f - 7.0f);
}

TEST(hud_draw_leaves_outside_transparent) {
    UiCanvas c = canvas(HW, HH, 2.0f);
    toast_init(&q);
    toast_push(&q, 0, TOAST_OK, "State saved", "Slot 3");
    toast_set_badge(&q, BADGE_PAUSED, true);
    hud_draw(&c, &q, 1000);
    ASSERT_TRUE(c.keep_alpha);
    ASSERT_EQ_HEX(s_hud[(HH - 1) * HW + (HW - 1)], 0x00000000);   /* bottom right: empty */
    HudLayout l;
    hud_layout(&c, &q, 1000, HW / 2.0f, &l);
    int cx = (int)((l.toasts[0].x + l.toasts[0].w / 2) * 2.0f);
    int cy = (int)((l.toasts[0].y + 4.0f) * 2.0f);
    ASSERT_TRUE((s_hud[cy * HW + cx] >> 24) >= 0xE0);             /* card body ~94% */
}

TEST(hud_draw_clips_to_small_canvas) {
    UiCanvas c = canvas(240, 160, 1.0f);   /* --scale 1, density 1 */
    toast_init(&q);
    for (int i = 0; i < TOAST_CAP; i++) {
        char t[8];
        snprintf(t, sizeof(t), "T%d", i);
        toast_push(&q, 0, TOAST_ERROR, t, "detail");
    }
    toast_set_badge(&q, BADGE_PAUSED, true);
    toast_set_badge(&q, BADGE_FAST, true);
    toast_set_badge(&q, BADGE_REWIND, true);
    toast_set_badge(&q, BADGE_MUTED, true);
    s_hud[240 * 160] = 0x12345678;          /* sentinel just past the canvas */
    hud_draw(&c, &q, 1000);
    ASSERT_EQ_HEX(s_hud[240 * 160], 0x12345678);
}

/* Writes hud_preview_d1.png / _d2.png over a green game-like background
 * when HUD_PREVIEW is set, for eyeballing against the mockup. */
TEST(hud_preview_png) {
    if (!getenv("HUD_PREVIEW")) return;
    for (int d = 1; d <= 2; d++) {
        int w = 360 * d, h = 240 * d;
        UiCanvas c = canvas(w, h, (float)d);
        toast_init(&q);
        toast_push(&q, 0, TOAST_OK, "State saved", "Slot 3");
        toast_push(&q, 0, TOAST_ERROR, "Slot 4 is empty", NULL);
        toast_set_badge(&q, BADGE_PAUSED, true);
        toast_set_badge(&q, BADGE_MUTED, true);
        hud_draw(&c, &q, 1000);
        uint32_t* out = (uint32_t*)malloc((size_t)w * h * 4);
        ASSERT_TRUE(out != NULL);
        for (int i = 0; i < w * h; i++) {
            uint32_t s = s_hud[i], a = s >> 24, bg = 0xFF6FBF8Fu, o = 0xFF000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                uint32_t sc = (s >> sh) & 0xFF, bc = (bg >> sh) & 0xFF;
                o |= ((sc * a + bc * (255 - a) + 127) / 255) << sh;
            }
            out[i] = o;
        }
        char path[32];
        snprintf(path, sizeof(path), "hud_preview_d%d.png", d);
        bool ok = screenshot_save_argb(out, w, h, path);
        free(out);
        ASSERT_TRUE(ok);
    }
}

/* --scale 1: the newest toast must stay on screen; older ones give way. */
TEST(hud_newest_toast_fits_small_area) {
    UiCanvas c = canvas(240, 160, 1.0f);
    HudLayout l;
    toast_init(&q);
    toast_set_badge(&q, BADGE_MUTED, true);
    toast_push(&q, 0, TOAST_WARN, "Running without BIOS", "Using built-in BIOS");
    toast_push(&q, 0, TOAST_OK, "3 cheats loaded", "x.cht");
    toast_push(&q, 0, TOAST_INFO, "Link cable connected", NULL);
    toast_push(&q, 0, TOAST_INFO, "Controller connected", "Pad");
    hud_layout(&c, &q, 1000, 240.0f, &l);
    uint32_t last = q.count - 1;
    ASSERT_TRUE(l.toasts[last].w > 0);
    ASSERT_TRUE(l.toasts[last].y + l.toasts[last].h <= 160.0f + 0.01f);
    ASSERT_EQ(l.toasts[0].w, 0);                       /* oldest hidden */
}

void run_hud_tests(void) {
    TEST_SUITE("hud");
    RUN_TEST(hud_strip_fits_badges_and_cap_toasts);
    RUN_TEST(hud_visible_needs_toast_or_badge);
    RUN_TEST(hud_badges_fixed_order);
    RUN_TEST(hud_stack_moves_below_badges_only_when_overlapping);
    RUN_TEST(hud_layout_caps_width_and_drops_detail);
    RUN_TEST(hud_layout_keeps_short_detail);
    RUN_TEST(hud_slide_offsets_new_toast);
    RUN_TEST(hud_draw_leaves_outside_transparent);
    RUN_TEST(hud_draw_clips_to_small_canvas);
    RUN_TEST(hud_newest_toast_fits_small_area);
    RUN_TEST(hud_preview_png);
}
