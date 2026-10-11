#include "test_harness.h"
#include "frontend/overworld_draw.h"
#include "ui/ui_font.h"

#define W 480
#define H 320
static uint32_t s_px[W * H];

static void canvas(UiCanvas* c) {
    memset(s_px, 0, sizeof(s_px));
    ui_font_init();
    ui_canvas_init(c, s_px, W, H, 1.0f);
    c->keep_alpha = true;
}

static OwTrainer youngster(void) {
    OwTrainer t;
    memset(&t, 0, sizeof(t));
    t.x = 64; t.y = 64; t.range = 4; t.known = true; t.facing = OW_DIR_DOWN;
    t.clear[OW_DIR_DOWN] = 2;
    memcpy(t.class_name, "YOUNGSTER", 10);
    return t;
}

static uint32_t px_at_gba(int gx, int gy) { return s_px[(gy * 2) * W + gx * 2]; }

TEST(owd_pill_text) {
    char buf[40];
    OwTrainer t = youngster();
    overworld_pill_text(&t, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Youngster \xC2\xB7 4 tiles");
    t.range = 1; t.class_name[0] = '\0';
    overworld_pill_text(&t, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Trainer \xC2\xB7 1 tile");
    memcpy(t.class_name, "TWINS & CO", 11);
    overworld_pill_text(&t, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Twins & Co \xC2\xB7 1 tile");
    t.beaten = true;
    overworld_pill_text(&t, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Beaten");
}

TEST(owd_invisible_draws_nothing) {
    UiCanvas c;
    static OverworldSnapshot s;
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.trainer_count = 1;
    s.trainers[0] = youngster();
    overworld_draw(&c, &s, 2.0f);
    for (int i = 0; i < W * H; i++) ASSERT_EQ(s_px[i] >> 24, 0);
}

TEST(owd_cone_covers_clear_tiles_only) {
    UiCanvas c;
    static OverworldSnapshot s;
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.trainer_count = 1;
    s.trainers[0] = youngster();
    overworld_draw(&c, &s, 2.0f);
    uint32_t near = px_at_gba(72, 64 + 16 + 8), far = px_at_gba(72, 64 + 48 + 8);
    ASSERT_TRUE((near >> 24) > 0);
    ASSERT_TRUE(((near >> 16) & 0xFF) > (near & 0xFF));   /* red, not grey */
    ASSERT_EQ(far >> 24, 0);                              /* 3rd tile is beyond clear = 2 */
}

TEST(owd_beaten_cone_is_grey) {
    UiCanvas c;
    static OverworldSnapshot s;
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.trainer_count = 1;
    s.trainers[0] = youngster();
    s.trainers[0].beaten = true;
    overworld_draw(&c, &s, 2.0f);
    uint32_t near = px_at_gba(72, 64 + 16 + 8);
    int r = (near >> 16) & 0xFF, b = near & 0xFF;
    ASSERT_TRUE((near >> 24) > 0);
    ASSERT_TRUE(r - b < 40 && b - r < 40);
}

TEST(owd_long_cone_terminates) {
    UiCanvas c;
    static OverworldSnapshot s;
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.trainer_count = 1;
    s.trainers[0] = youngster();
    s.trainers[0].range = 255;
    s.trainers[0].clear[OW_DIR_DOWN] = 255;   /* a u8 tile counter would wrap forever */
    overworld_draw(&c, &s, 2.0f);
    ASSERT_TRUE((px_at_gba(72, 64 + 16 + 8) >> 24) > 0);
}

TEST(owd_pill_stays_inside_canvas) {
    UiCanvas c;
    canvas(&c);
    OwTrainer t = youngster();
    const int16_t xs[] = {-12, 0, 120, 230};
    const int16_t ys[] = {-12, 0, 72, 150};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            t.x = xs[i]; t.y = ys[j];
            UiRect r = overworld_pill_rect(&c, &t, 2.0f);
            ASSERT_TRUE(r.x >= 0.0f && r.y >= 0.0f);
            ASSERT_TRUE(r.x + r.w <= (float)W && r.y + r.h <= (float)H);
        }
    }
    t.x = 120; t.y = 100;
    UiRect above = overworld_pill_rect(&c, &t, 2.0f);
    ASSERT_TRUE(above.y + above.h <= (float)(t.y - 16) * 2.0f);   /* above the 32 px sprite */
    t.y = 4;
    UiRect below = overworld_pill_rect(&c, &t, 2.0f);
    ASSERT_TRUE(below.y >= (float)(t.y + 16) * 2.0f);             /* no room: under the feet */
}

TEST(owd_hidden_item_marked) {
    UiCanvas c;
    static OverworldSnapshot s;
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.marker_count = 1;
    s.markers[0].kind = OW_MARK_HIDDEN;
    s.markers[0].x = 32; s.markers[0].y = 48;
    overworld_draw(&c, &s, 2.0f);
    ASSERT_TRUE((px_at_gba(40, 56) >> 24) > 0);
    ASSERT_EQ(px_at_gba(20, 56) >> 24, 0);
}

void run_overworld_draw_tests(void) {
    RUN_TEST(owd_pill_text);
    RUN_TEST(owd_invisible_draws_nothing);
    RUN_TEST(owd_cone_covers_clear_tiles_only);
    RUN_TEST(owd_beaten_cone_is_grey);
    RUN_TEST(owd_long_cone_terminates);
    RUN_TEST(owd_pill_stays_inside_canvas);
    RUN_TEST(owd_hidden_item_marked);
}
