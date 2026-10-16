#include "test_harness.h"
#include "frontend/badge_draw.h"
#include <stdlib.h>
#include <string.h>

#define W 160
#define H 160
#define BG 0xFF101010u
#define MISSING 0xFF2E3442u

static uint32_t s_px[W * H];

static void canvas(UiCanvas* c, float scale) {
    for (int i = 0; i < W * H; i++) s_px[i] = BG;
    ui_canvas_init(c, s_px, W, H, scale);
}

/* Indices 0..4 across the top row of a 16x16 badge, 4 in the rest of row 1. */
static void make_px(uint8_t px[256]) {
    memset(px, 0, 256);
    for (int x = 0; x < 5; x++) px[x] = (uint8_t)x;
    px[16 + 15] = 15;
    px[8 * 16 + 8] = 3;
}

static void bounds(int* x0, int* y0, int* x1, int* y1) {
    *x0 = W; *y0 = H; *x1 = -1; *y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (s_px[y * W + x] != BG) {
                if (x < *x0) *x0 = x;
                if (y < *y0) *y0 = y;
                if (x > *x1) *x1 = x;
                if (y > *y1) *y1 = y;
            }
}

TEST(badge_color_tables) {
    static const uint32_t hoenn[8] = {0xFFA8B0C4, 0xFFD9A85B, 0xFFF2C94C, 0xFFEB5757,
                                      0xFFC9B27C, 0xFF8DB2F0, 0xFFFF5D8F, 0xFF4C9BFF};
    static const uint32_t kanto[8] = {0xFFA8B0C4, 0xFF4C9BFF, 0xFFF2C94C, 0xFFB58CFF,
                                      0xFFFF5D8F, 0xFFD9A85B, 0xFFEB5757, 0xFF5BBF6A};
    for (int i = 0; i < 8; i++) {
        ASSERT_EQ_HEX(badge_color(GAME_REGION_HOENN, i), hoenn[i]);
        ASSERT_EQ_HEX(badge_color(GAME_REGION_KANTO, i), kanto[i]);
    }
}

TEST(badge_draw_tint_and_missing) {
    uint8_t px[256];
    make_px(px);
    UiCanvas c;
    canvas(&c, 1.0f);   /* k = 2, 32 px square at (0,0) when the box is at 0,0 */
    badge_draw(&c, 0, 0, px, GAME_REGION_HOENN, 1, true);
    /* badge 1 base 0xD9A85B; ROM pixel (x,0) covers device (2x..2x+1, 0..1) */
    ASSERT_EQ_HEX(s_px[0 * W + 0], BG);   /* index 0 untouched */
    ASSERT_EQ_HEX(s_px[0 * W + 6], 0xFFD9A85Bu);   /* index 3 is the base colour */
    /* index 1 = mix(b, white, 0.65): 217->(217+38*.65)=242, 168->(168+87*.65)=225, 91->(91+164*.65)=198 */
    ASSERT_EQ_HEX(s_px[0 * W + 2], 0xFFF2E1C6u);
    /* index 4 = mix(b, black, 0.35): 141, 109, 59 */
    ASSERT_EQ_HEX(s_px[0 * W + 8], 0xFF8D6D3Bu);
    /* index 15 = mix(b, black, 0.78): 48, 37, 20 */
    ASSERT_EQ_HEX(s_px[3 * W + 30], 0xFF302514u);

    canvas(&c, 1.0f);
    badge_draw(&c, 0, 0, px, GAME_REGION_HOENN, 1, false);
    ASSERT_EQ_HEX(s_px[0 * W + 6], MISSING);
    ASSERT_EQ_HEX(s_px[0 * W + 0], BG);
}

TEST(badge_draw_square_size_follows_scale) {
    uint8_t px[256];
    memset(px, 3, 256);
    static const float scales[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    static const int ks[] = {1, 2, 2, 3, 4};
    for (int s = 0; s < 5; s++) {
        UiCanvas c;
        canvas(&c, scales[s]);
        badge_draw(&c, 20, 20, px, GAME_REGION_HOENN, 0, true);
        int x0, y0, x1, y1;
        bounds(&x0, &y0, &x1, &y1);
        ASSERT_EQ(x1 - x0 + 1, 16 * ks[s]);
        ASSERT_EQ(y1 - y0 + 1, 16 * ks[s]);
        /* centred on the 32 pt box, within a pixel */
        int mid = ui_px(&c, 36.0f);
        ASSERT_TRUE(abs((x0 + x1 + 1) / 2 - mid) <= 1);
        ASSERT_TRUE(abs((y0 + y1 + 1) / 2 - mid) <= 1);
    }
}

TEST(badge_draw_clips) {
    uint8_t px[256];
    memset(px, 3, 256);
    UiCanvas c;
    canvas(&c, 1.0f);
    badge_draw(&c, -30, -30, px, GAME_REGION_HOENN, 0, true);
    badge_draw(&c, 150, 150, px, GAME_REGION_HOENN, 0, true);
    ui_canvas_clip(&c, (UiRect){50, 50, 10, 10});
    badge_draw(&c, 40, 40, px, GAME_REGION_HOENN, 0, true);
    ASSERT_EQ_HEX(s_px[49 * W + 55], BG);
    ASSERT_EQ_HEX(s_px[55 * W + 55], 0xFFA8B0C4u);
}

TEST(badge_draw_rainbow_is_coloured) {
    uint8_t px[256];
    memset(px, 3, 256);
    UiCanvas c;
    canvas(&c, 1.0f);
    badge_draw(&c, 0, 0, px, GAME_REGION_KANTO, 3, true);
    int hues = 0;
    for (int sy = 0; sy < 16; sy++)
        for (int sx = 0; sx < 16; sx++) {
            uint32_t p = s_px[(sy * 2) * W + sx * 2];
            int r = (int)(p >> 16 & 0xFF), g = (int)(p >> 8 & 0xFF), b = (int)(p & 0xFF);
            int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            if (mx - mn > 40) hues++;
        }
    ASSERT_TRUE(hues > 250);
    /* the same badge missing stays flat */
    canvas(&c, 1.0f);
    badge_draw(&c, 0, 0, px, GAME_REGION_KANTO, 3, false);
    ASSERT_EQ_HEX(s_px[0], MISSING);
    ASSERT_EQ_HEX(s_px[31 * W + 31], MISSING);
}

void run_badge_draw_tests(void) {
    TEST_SUITE("badge_draw");
    RUN_TEST(badge_color_tables);
    RUN_TEST(badge_draw_tint_and_missing);
    RUN_TEST(badge_draw_square_size_follows_scale);
    RUN_TEST(badge_draw_clips);
    RUN_TEST(badge_draw_rainbow_is_coloured);
}
