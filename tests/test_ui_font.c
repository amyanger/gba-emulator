#include "test_harness.h"
#include "ui/ui_font.h"
#include <string.h>

#define W 400
#define H 80
static uint32_t s_px[W * H];

static UiCanvas fresh(float scale) {
    UiCanvas c;
    ui_canvas_init(&c, s_px, W, H, scale);
    ui_canvas_clear(&c, 0xFF000000);
    return c;
}

static int lit_columns(int x0, int x1) {
    int n = 0;
    for (int x = x0; x < x1; x++)
        for (int y = 0; y < H; y++)
            if ((s_px[y * W + x] & 0xFF) > 0x40) { n++; break; }
    return n;
}

TEST(font_init_and_width_grows) {
    ASSERT_TRUE(ui_font_init());
    UiCanvas c = fresh(1.0f);
    float a = ui_text_width(&c, UI_FONT_REGULAR, 13, "W");
    float b = ui_text_width(&c, UI_FONT_REGULAR, 13, "WW");
    ASSERT_TRUE(a > 5 && b > a * 1.8f);
    ASSERT_TRUE(ui_text_width(&c, UI_FONT_SEMIBOLD, 26, "W") > a * 1.8f);
    ASSERT_EQ((int)ui_text_width(&c, UI_FONT_REGULAR, 13, ""), 0);
}

TEST(font_draws_pixels_where_expected) {
    UiCanvas c = fresh(2.0f);
    ui_text(&c, UI_FONT_REGULAR, 13, 10, 5, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "Wurmple");
    ASSERT_TRUE(lit_columns(20, 120) > 40);
    ASSERT_EQ(lit_columns(0, 18), 0);
}

TEST(font_right_align_ends_at_x) {
    UiCanvas c = fresh(1.0f);
    ui_text(&c, UI_FONT_REGULAR, 13, 200, 5, 0, UI_ALIGN_RIGHT, 0xFFFFFFFF, "33.7%");
    ASSERT_TRUE(lit_columns(160, 200) > 10);
    ASSERT_EQ(lit_columns(202, 400), 0);
}

TEST(font_ellipsis_fits_max_width) {
    UiCanvas c = fresh(1.0f);
    float full = ui_text_width(&c, UI_FONT_REGULAR, 13, "BRIGHTPOWDER SUPER LONG");
    float drawn = ui_text(&c, UI_FONT_REGULAR, 13, 0, 5, 60, UI_ALIGN_LEFT, 0xFFFFFFFF,
                          "BRIGHTPOWDER SUPER LONG");
    ASSERT_TRUE(full > 60);
    ASSERT_TRUE(drawn <= 60.5f);
    ASSERT_EQ(lit_columns(62, 400), 0);
}

TEST(font_unknown_codepoint_and_bad_utf8_are_safe) {
    UiCanvas c = fresh(1.0f);
    ui_text(&c, UI_FONT_REGULAR, 13, 0, 5, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "a\xE2\x99\x82" "b"); /* U+2642 */
    ui_text(&c, UI_FONT_REGULAR, 13, 0, 30, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "\xFF\xC3");       /* invalid */
    ASSERT_TRUE(lit_columns(0, 40) > 0);
}

TEST(font_baseline_snaps_to_whole_pixel) {
    UiCanvas c = fresh(1.25f);
    float asc = ui_text_ascent(&c, UI_FONT_REGULAR, 13);
    float px = asc * 1.25f;
    ASSERT_TRUE(px - (float)(int)(px + 0.5f) < 0.01f && px - (float)(int)(px + 0.5f) > -0.01f);
}

TEST(font_cache_survives_overflow) {
    UiCanvas c = fresh(1.0f);
    ui_font_cache_clear();
    for (int size = 8; size < 80; size++)
        ui_text(&c, (UiFont)(size & 1), (float)size, 0, 0, 0, UI_ALIGN_LEFT, 0xFFFFFFFF,
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    ASSERT_TRUE(ui_font_cache_count() > 0);
    ui_canvas_clear(&c, 0xFF000000);
    ui_text(&c, UI_FONT_REGULAR, 13, 10, 5, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "Still works");
    ASSERT_TRUE(lit_columns(10, 100) > 30);
}

/* Sizes are em sizes, as in the CSS mockups: Inter's ascender is 0.97 em and
 * its cap height 0.73 em. Pixel-height scaling made both about 17% smaller. */
TEST(font_size_is_em_size) {
    UiCanvas c = fresh(1.0f);
    float asc = ui_text_ascent(&c, UI_FONT_REGULAR, 13);
    ASSERT_TRUE(asc >= 12.0f && asc <= 14.0f);
    c = fresh(4.0f);
    ui_text(&c, UI_FONT_REGULAR, 13, 2, 0, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "H");
    int rows = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if ((s_px[y * W + x] & 0xFF) > 0x80) { rows++; break; }
    ASSERT_TRUE(rows >= 36 && rows <= 40); /* 0.727 * 52 px */
}

TEST(font_cap_middle_matches_cap_height) {
    UiCanvas c = fresh(1.0f);
    float asc = ui_text_ascent(&c, UI_FONT_REGULAR, 13);
    float mid = ui_text_cap_middle(&c, UI_FONT_REGULAR, 13);
    ASSERT_TRUE(asc - mid > 4.2f && asc - mid < 5.2f); /* 0.727 * 13 / 2 */
}

/* Coverage-weighted x centroid of the whole canvas. */
static float centroid_x(void) {
    double sum = 0, wsum = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint32_t v = s_px[y * W + x] & 0xFF;
            sum += (double)v * x;
            wsum += v;
        }
    return wsum > 0 ? (float)(sum / wsum) : 0.0f;
}

TEST(font_positions_glyphs_at_subpixel_precision) {
    float cx[3];
    const float xs[3] = {10.0f, 10.25f, 10.5f};
    static uint32_t first[W * H];
    for (int i = 0; i < 3; i++) {
        UiCanvas c = fresh(1.0f);
        ui_text(&c, UI_FONT_REGULAR, 13, xs[i], 5, 0, UI_ALIGN_LEFT, 0xFFFFFFFF, "I");
        cx[i] = centroid_x();
        if (i == 0) memcpy(first, s_px, sizeof(first));
        else ASSERT_TRUE(memcmp(first, s_px, sizeof(first)) != 0);
    }
    /* Whole-pixel snapping would move these by 0 and 1 px. */
    ASSERT_TRUE(cx[1] - cx[0] > 0.1f && cx[1] - cx[0] < 0.4f);
    ASSERT_TRUE(cx[2] - cx[0] > 0.35f && cx[2] - cx[0] < 0.65f);
}

void run_ui_font_tests(void) {
    TEST_SUITE("ui_font");
    RUN_TEST(font_init_and_width_grows);
    RUN_TEST(font_draws_pixels_where_expected);
    RUN_TEST(font_right_align_ends_at_x);
    RUN_TEST(font_ellipsis_fits_max_width);
    RUN_TEST(font_unknown_codepoint_and_bad_utf8_are_safe);
    RUN_TEST(font_baseline_snaps_to_whole_pixel);
    RUN_TEST(font_cache_survives_overflow);
    RUN_TEST(font_size_is_em_size);
    RUN_TEST(font_cap_middle_matches_cap_height);
    RUN_TEST(font_positions_glyphs_at_subpixel_precision);
}
