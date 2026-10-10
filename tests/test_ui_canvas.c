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

void run_ui_canvas_tests(void) {
    TEST_SUITE("ui_canvas");
    RUN_TEST(canvas_fill_rect_snaps_and_scales);
    RUN_TEST(canvas_alpha_blends_over_background);
    RUN_TEST(canvas_round_rect_corners_are_partial);
    RUN_TEST(canvas_clip_stops_writes);
    RUN_TEST(canvas_out_of_bounds_and_degenerate_shapes_are_safe);
    RUN_TEST(canvas_ring_and_capsule_draw_something);
}
