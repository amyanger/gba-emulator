#include "test_harness.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

#define W 600
#define H 120
static uint32_t s_px[W * H];
#define AT(x, y) s_px[(y) * W + (x)]

static UiCanvas fresh(void) {
    UiCanvas c;
    ui_font_init();
    ui_canvas_init(&c, s_px, W, H, 1.0f);
    ui_canvas_clear(&c, UI_BG);
    return c;
}

/* Pixels that differ from the background inside [x0,x1) x [y0,y1). */
static int lit_in(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (x >= 0 && y >= 0 && x < W && y < H && s_px[y * W + x] != UI_BG) n++;
    return n;
}

TEST(widgets_type_color_table) {
    ASSERT_EQ_HEX(ui_type_color(10), 0xFFEE8130u);
    ASSERT_EQ_HEX(ui_type_color(0), 0xFFA8A77Au);
    ASSERT_EQ_HEX(ui_type_color(17), 0xFF705746u);
    ASSERT_EQ_HEX(ui_type_color(9), 0xFF68A090u);
    ASSERT_EQ_HEX(ui_type_color(200), UI_DIM);
}

TEST(widgets_pill_width_and_background) {
    UiCanvas c = fresh();
    float tw = ui_text_width(&c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, "FIRE");
    float w = ui_pill(&c, 20, 20, "FIRE", ui_type_color(10));
    ASSERT_TRUE(w >= tw + 14.0f && w < tw + 16.0f);   /* text plus 2 x 7 padding */
    int h = (int)(UI_SIZE_PILL + 5);
    ASSERT_EQ_HEX(AT(23, 20 + h / 2), 0xFFEE8130u);              /* left padding, inside the pill */
    ASSERT_EQ_HEX(AT(20 + (int)w - 4, 20 + h / 2), 0xFFEE8130u); /* right padding */
    ASSERT_EQ_HEX(AT(20, 20), UI_BG);                            /* rounded corner is empty */
    ASSERT_EQ(lit_in(0, 0, 18, H), 0);
    ASSERT_EQ(lit_in(20 + (int)w + 2, 0, W, H), 0);
}

TEST(widgets_pill_returns_same_width_on_any_color) {
    UiCanvas c = fresh();
    float a = ui_pill(&c, 10, 10, "ICE", ui_type_color(15));
    float b = ui_pill(&c, 10, 40, "ICE", ui_type_color(10));
    ASSERT_TRUE(a == b);
}

TEST(widgets_bar_fill_and_clamp) {
    UiCanvas c = fresh();
    UiRect r = {10, 10, 100, 6};
    ui_bar(&c, r, 0.5f, UI_GOOD);
    ASSERT_EQ_HEX(AT(35, 13), UI_GOOD);
    ASSERT_EQ_HEX(AT(85, 13), UI_TRACK);
    ASSERT_EQ_HEX(AT(10, 10), UI_BG);                            /* rounded end */

    ui_canvas_clear(&c, UI_BG);
    ui_bar(&c, r, -2.0f, UI_GOOD);
    ASSERT_EQ_HEX(AT(12, 13), UI_TRACK);
    ASSERT_EQ_HEX(AT(60, 13), UI_TRACK);

    ui_canvas_clear(&c, UI_BG);
    ui_bar(&c, r, 7.0f, UI_GOOD);
    ASSERT_EQ_HEX(AT(60, 13), UI_GOOD);
    ASSERT_EQ_HEX(AT(106, 13), UI_GOOD);
    ASSERT_EQ_HEX(AT(112, 13), UI_BG);                           /* no overshoot */
}

static void check_gender(bool female) {
    UiCanvas c = fresh();
    ui_gender(&c, 100, 30, 11, female);
    ASSERT_TRUE(lit_in(100, 30, 111, 41) >= 10);
    ASSERT_EQ(lit_in(0, 0, 99, H), 0);
    ASSERT_EQ(lit_in(112, 0, W, H), 0);
    ASSERT_EQ(lit_in(0, 0, W, 29), 0);
    ASSERT_EQ(lit_in(0, 42, W, H), 0);
}

TEST(widgets_gender_male_stays_in_box) { check_gender(false); }
TEST(widgets_gender_female_stays_in_box) { check_gender(true); }

TEST(widgets_gender_marks_differ_and_use_colors) {
    UiCanvas c = fresh();
    ui_gender(&c, 100, 30, 22, false);
    ui_gender(&c, 200, 30, 22, true);
    int diff = 0;
    for (int y = 0; y < 22; y++)
        for (int x = 0; x < 22; x++)
            if (AT(100 + x, 30 + y) != AT(200 + x, 30 + y)) diff++;
    ASSERT_TRUE(diff > 20);
    /* Male: arrow tip near the upper right corner. Female: stem at the bottom center. */
    ASSERT_TRUE(lit_in(115, 30, 123, 38) > 3);
    ASSERT_TRUE(AT(200 + 11, 30 + 20) != UI_BG);
}

TEST(widgets_check_draws_glyph) {
    UiCanvas c = fresh();
    ui_check(&c, 50, 20, 13, UI_GOOD);
    ASSERT_TRUE(lit_in(50, 20, 66, 40) > 8);
    ASSERT_EQ(lit_in(0, 0, 48, H), 0);
}

TEST(widgets_tabs_underline_under_active_only) {
    UiCanvas c = fresh();
    const char* labels[] = {"Party", "Wild", "Battle"};
    float below = ui_tabs(&c, 10, 10, 500, labels, 3, 1, NULL, NULL);
    ASSERT_TRUE(below > 10.0f);
    float w0 = ui_text_width(&c, UI_FONT_REGULAR, UI_SIZE_SMALL, "Party");
    float x1 = 10 + w0 + 18;
    float w1 = ui_text_width(&c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, "Wild");
    int mid1 = (int)(x1 + w1 / 2), mid0 = (int)(10 + w0 / 2);
    ASSERT_EQ_HEX(AT(mid1, 30), UI_ACCENT);
    ASSERT_EQ_HEX(AT(mid1, 31), UI_ACCENT);
    ASSERT_TRUE(AT(mid0, 30) != UI_ACCENT);
    ASSERT_EQ_HEX(AT(mid0, 31), UI_DIVIDER);                     /* divider across the whole width */
    ASSERT_EQ_HEX(AT(505, 31), UI_DIVIDER);
    ASSERT_EQ_HEX(AT(520, 31), UI_BG);
}

TEST(widgets_tabs_badge_and_hint) {
    UiCanvas c = fresh();
    const char* labels[] = {"Party", "Wild"};
    ui_tabs(&c, 10, 10, 500, labels, 2, 0, "3", "Tab to switch");
    float hint_w = ui_text_width(&c, UI_FONT_REGULAR, UI_SIZE_LABEL, "Tab to switch");
    ASSERT_TRUE(lit_in((int)(510 - hint_w) - 1, 12, 511, 28) > 20);   /* hint right-aligned */
    ASSERT_EQ(lit_in(512, 0, W, 29), 0);
    /* badge pill (UI_SELECTED) exists somewhere between the labels and the hint */
    int found = 0;
    for (int x = 10; x < 500; x++)
        if (AT(x, 18) == UI_SELECTED) found++;
    ASSERT_TRUE(found > 3);
}

TEST(widgets_section_label_draws_dim_text) {
    UiCanvas c = fresh();
    ui_section_label(&c, 20, 20, "STATS");
    ASSERT_TRUE(lit_in(20, 20, 70, 36) > 15);
    ASSERT_EQ(lit_in(0, 0, 18, H), 0);
}

void run_ui_widgets_tests(void) {
    TEST_SUITE("ui_widgets");
    RUN_TEST(widgets_type_color_table);
    RUN_TEST(widgets_pill_width_and_background);
    RUN_TEST(widgets_pill_returns_same_width_on_any_color);
    RUN_TEST(widgets_bar_fill_and_clamp);
    RUN_TEST(widgets_gender_male_stays_in_box);
    RUN_TEST(widgets_gender_female_stays_in_box);
    RUN_TEST(widgets_gender_marks_differ_and_use_colors);
    RUN_TEST(widgets_check_draws_glyph);
    RUN_TEST(widgets_tabs_underline_under_active_only);
    RUN_TEST(widgets_tabs_badge_and_hint);
    RUN_TEST(widgets_section_label_draws_dim_text);
}
