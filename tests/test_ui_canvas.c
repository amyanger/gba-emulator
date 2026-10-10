#include "test_harness.h"
#include "ui/ui_canvas.h"

#define W 64
#define H 48
static uint32_t s_px[W * H];
#define AT(x, y) s_px[(y) * W + (x)]

static UiCanvas fresh(float scale) {
    UiCanvas c;
    ui_canvas_init(&c, s_px, W, H, scale);
    ui_canvas_clear(&c, 0xFF000000);
    return c;
}

TEST(canvas_fill_rect_snaps_and_scales) {
    UiCanvas c = fresh(2.0f);
    ui_fill_rect(&c, (UiRect){2, 3, 4, 5}, 0xFFFF0000);   /* pixels x 4..11, y 6..15 */
    ASSERT_EQ_HEX(AT(4, 6), 0xFFFF0000);
    ASSERT_EQ_HEX(AT(11, 15), 0xFFFF0000);
    ASSERT_EQ_HEX(AT(3, 6), 0xFF000000);
    ASSERT_EQ_HEX(AT(12, 15), 0xFF000000);
    ASSERT_EQ_HEX(AT(4, 16), 0xFF000000);
}

TEST(canvas_alpha_blends_over_background) {
    UiCanvas c = fresh(1.0f);
    ui_fill_rect(&c, (UiRect){0, 0, 1, 1}, 0x80FFFFFF);
    uint32_t p = AT(0, 0);
    ASSERT_TRUE(((p >> 16) & 0xFF) >= 0x7F && ((p >> 16) & 0xFF) <= 0x81);
    ASSERT_EQ(p >> 24, 0xFF);
}

TEST(canvas_round_rect_corners_are_partial) {
    UiCanvas c = fresh(1.0f);
    ui_fill_round_rect(&c, (UiRect){8, 8, 30, 20}, 8.0f, 0xFFFFFFFF);
    ASSERT_EQ_HEX(AT(20, 18), 0xFFFFFFFF);          /* center: solid */
    ASSERT_EQ_HEX(AT(8, 8), 0xFF000000);            /* outer corner pixel: empty */
    uint32_t edge = AT(10, 10);                     /* on the arc: partial */
    ASSERT_TRUE((edge & 0xFF) > 0 && (edge & 0xFF) < 0xFF);
    ASSERT_EQ_HEX(AT(20, 8), 0xFFFFFFFF);           /* straight top edge: solid */
}

TEST(canvas_clip_stops_writes) {
    UiCanvas c = fresh(1.0f);
    ui_canvas_clip(&c, (UiRect){10, 10, 5, 5});
    ui_fill_rect(&c, (UiRect){0, 0, 64, 48}, 0xFF00FF00);
    ASSERT_EQ_HEX(AT(9, 10), 0xFF000000);
    ASSERT_EQ_HEX(AT(10, 10), 0xFF00FF00);
    ASSERT_EQ_HEX(AT(15, 15), 0xFF000000);
    ui_canvas_unclip(&c);
    ui_fill_rect(&c, (UiRect){0, 0, 1, 1}, 0xFF00FF00);
    ASSERT_EQ_HEX(AT(0, 0), 0xFF00FF00);
}

TEST(canvas_out_of_bounds_and_degenerate_shapes_are_safe) {
    UiCanvas c = fresh(3.0f);
    ui_fill_rect(&c, (UiRect){-100, -100, 1000, 1000}, 0xFF123456);
    ui_fill_round_rect(&c, (UiRect){50, 40, 0, -3}, 4.0f, 0xFFFFFFFF);
    ui_fill_round_rect(&c, (UiRect){5, 5, 2, 2}, 50.0f, 0xFFFFFFFF);   /* radius larger than rect */
    ui_ring(&c, 1000, 1000, 5, 1, 0xFFFFFFFF);
    ui_capsule(&c, -50, -50, 500, 500, 2, 0xFFFFFFFF);
    ui_hline(&c, 0, 47.9f, 1000, 0xFFFFFFFF);
    /* Off-diagonal corner: only the full-canvas fill reached it. ASan proves no stray writes. */
    ASSERT_EQ_HEX(AT(W - 1, H - 1), 0xFF123456);
    ASSERT_EQ_HEX(AT(0, 0), 0xFFFFFFFF);                /* the clipped capsule did draw */
}

TEST(canvas_ring_and_capsule_draw_something) {
    UiCanvas c = fresh(1.0f);
    ui_ring(&c, 20, 20, 8, 2, 0xFFFFFFFF);
    ASSERT_EQ_HEX(AT(20, 20), 0xFF000000);          /* hollow center */
    ASSERT_TRUE((AT(28, 20) & 0xFF) > 0x80);        /* on the ring */
    ui_capsule(&c, 40, 10, 50, 10, 2, 0xFFFFFFFF);
    ASSERT_TRUE((AT(45, 10) & 0xFF) > 0x80);
}

TEST(canvas_keep_alpha_clear_is_transparent) {
    UiCanvas c;
    ui_canvas_init(&c, s_px, W, H, 1.0f);
    ASSERT_TRUE(!c.keep_alpha);
    c.keep_alpha = true;
    ui_canvas_clear(&c, 0x00000000);
    ASSERT_EQ_HEX(AT(0, 0), 0x00000000);
    ASSERT_EQ_HEX(AT(W - 1, H - 1), 0x00000000);
}

TEST(canvas_keep_alpha_opaque_fill_exact) {
    UiCanvas c;
    ui_canvas_init(&c, s_px, W, H, 1.0f);
    c.keep_alpha = true;
    ui_canvas_clear(&c, 0x00000000);
    ui_fill_rect(&c, (UiRect){2, 2, 4, 4}, 0xFF3DDC97);
    ASSERT_EQ_HEX(AT(3, 3), 0xFF3DDC97);
    ASSERT_EQ_HEX(AT(1, 1), 0x00000000);
}

/* Half coverage onto transparent keeps the source color (no dark halo). */
TEST(canvas_keep_alpha_edge_has_no_halo) {
    UiCanvas c;
    ui_canvas_init(&c, s_px, W, H, 1.0f);
    c.keep_alpha = true;
    ui_canvas_clear(&c, 0x00000000);
    ui_blend_coverage(&c, 5, 5, 0xFFE7E9EE, 128);
    uint32_t p = AT(5, 5);
    ASSERT_EQ((p >> 24) & 0xFF, 128);
    ASSERT_EQ((p >> 16) & 0xFF, 0xE7);
    ASSERT_EQ((p >> 8) & 0xFF, 0xE9);
    ASSERT_EQ(p & 0xFF, 0xEE);
}

/* Over a translucent destination, alpha accumulates and color mixes. */
TEST(canvas_keep_alpha_over_translucent) {
    UiCanvas c;
    ui_canvas_init(&c, s_px, W, H, 1.0f);
    c.keep_alpha = true;
    ui_canvas_clear(&c, 0x80000000);               /* black, 50% */
    ui_blend_coverage(&c, 1, 1, 0x80FFFFFF, 255);  /* white, 50% */
    uint32_t p = AT(1, 1);
    uint32_t a = (p >> 24) & 0xFF, r = (p >> 16) & 0xFF;
    ASSERT_TRUE(a >= 0xBF && a <= 0xC1);           /* 0.5 + 0.5*0.5 = 0.75 */
    ASSERT_TRUE(r >= 0xA9 && r <= 0xAB);           /* 0.5 / 0.75 = 0.667 */
}

/* Opaque destination: keep_alpha must give exactly the old result. */
TEST(canvas_keep_alpha_matches_opaque_path_on_opaque_dst) {
    static uint32_t a_px[W * H], b_px[W * H];
    UiCanvas a, b;
    ui_canvas_init(&a, a_px, W, H, 2.0f);
    ui_canvas_init(&b, b_px, W, H, 2.0f);
    b.keep_alpha = true;
    ui_canvas_clear(&a, 0xFF11131A);
    ui_canvas_clear(&b, 0xFF11131A);
    UiRect r = {3.3f, 2.7f, 20.1f, 11.6f};
    ui_fill_round_rect(&a, r, 5.0f, 0xC05B8CFF);
    ui_fill_round_rect(&b, r, 5.0f, 0xC05B8CFF);
    ui_ring(&a, 12, 12, 6, 1.5f, 0x80FFFFFF);
    ui_ring(&b, 12, 12, 6, 1.5f, 0x80FFFFFF);
    ASSERT_MEM_EQ(a_px, b_px, sizeof(a_px));
}

void run_ui_canvas_tests(void) {
    TEST_SUITE("ui_canvas");
    RUN_TEST(canvas_fill_rect_snaps_and_scales);
    RUN_TEST(canvas_alpha_blends_over_background);
    RUN_TEST(canvas_round_rect_corners_are_partial);
    RUN_TEST(canvas_clip_stops_writes);
    RUN_TEST(canvas_out_of_bounds_and_degenerate_shapes_are_safe);
    RUN_TEST(canvas_ring_and_capsule_draw_something);
    RUN_TEST(canvas_keep_alpha_clear_is_transparent);
    RUN_TEST(canvas_keep_alpha_opaque_fill_exact);
    RUN_TEST(canvas_keep_alpha_edge_has_no_halo);
    RUN_TEST(canvas_keep_alpha_over_translucent);
    RUN_TEST(canvas_keep_alpha_matches_opaque_path_on_opaque_dst);
}
