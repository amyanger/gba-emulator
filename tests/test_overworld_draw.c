#include "test_harness.h"
#include "frontend/overworld_draw.h"
#include "ui/ui_font.h"
#include <stdio.h>

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

static OwMarker marker(uint8_t kind, int16_t x, int16_t y, const char* a, const char* b) {
    OwMarker mk;
    memset(&mk, 0, sizeof(mk));
    mk.kind = kind; mk.x = x; mk.y = y; mk.name_count = b ? 2 : 1;
    snprintf(mk.name[0], sizeof(mk.name[0]), "%s", a);
    if (b) snprintf(mk.name[1], sizeof(mk.name[1]), "%s", b);
    return mk;
}

static bool overlap(UiRect a, UiRect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

TEST(owd_marker_text) {
    char buf[48];
    OwMarker mk = marker(OW_MARK_ITEM_BALL, 0, 0, "RARE CANDY", NULL);
    overworld_marker_text(&mk, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Rare Candy");
    mk = marker(OW_MARK_GIFT, 0, 0, "TM36", NULL);
    overworld_marker_text(&mk, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Gives TM36");
    mk = marker(OW_MARK_GIFT, 0, 0, "MACH BIKE", "ACRO BIKE");
    overworld_marker_text(&mk, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Gives Mach Bike / Acro Bike");
}

TEST(owd_pill_tints_by_kind) {
    UiCanvas c;
    static OverworldSnapshot s;
    OwLabel l[OW_MAX_LABELS];
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.marker_count = 2;
    s.markers[0] = marker(OW_MARK_ITEM_BALL, 40, 100, "RARE CANDY", NULL);
    s.markers[1] = marker(OW_MARK_GIFT, 160, 100, "TM36", NULL);
    overworld_draw(&c, &s, 2.0f);
    ASSERT_EQ(overworld_label_rects(&c, &s, 2.0f, l), 2);
    uint32_t item = s_px[(int)(l[1].rect.y + 2) * W + (int)(l[1].rect.x + 3)];
    uint32_t gift = s_px[(int)(l[0].rect.y + 2) * W + (int)(l[0].rect.x + 3)];
    ASSERT_TRUE(((item >> 16) & 0xFF) > (item & 0xFF));            /* gold: red over blue */
    ASSERT_TRUE(((gift >> 8) & 0xFF) > ((gift >> 16) & 0xFF));     /* green over red */
}

TEST(owd_adjacent_labels_do_not_overlap) {
    UiCanvas c;
    static OverworldSnapshot s;
    OwLabel l[OW_MAX_LABELS];
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.marker_count = 2;
    s.markers[0] = marker(OW_MARK_ITEM_BALL, 96, 80, "RARE CANDY", NULL);
    s.markers[1] = marker(OW_MARK_ITEM_BALL, 112, 80, "HEART SCALE", NULL);
    ASSERT_EQ(overworld_label_rects(&c, &s, 2.0f, l), 2);
    ASSERT_TRUE(!overlap(l[0].rect, l[1].rect));
}

TEST(owd_trainer_and_gift_labels_do_not_overlap) {
    UiCanvas c;
    static OverworldSnapshot s;
    OwLabel l[OW_MAX_LABELS];
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.trainer_count = 1;
    s.trainers[0] = youngster();
    s.marker_count = 1;
    s.markers[0] = marker(OW_MARK_GIFT, 64, 64, "TM39", NULL);   /* same object */
    ASSERT_EQ(overworld_label_rects(&c, &s, 2.0f, l), 2);
    ASSERT_TRUE(!overlap(l[0].rect, l[1].rect));
}

TEST(owd_label_avoids_cone) {
    UiCanvas c;
    static OverworldSnapshot s;
    OwLabel l[OW_MAX_LABELS];
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.trainer_count = 1;
    s.trainers[0] = youngster();                 /* cone: x 64..80, y 80..112 (2 tiles) */
    s.trainers[0].clear[OW_DIR_DOWN] = 3;        /* to y 128 */
    s.marker_count = 1;
    s.markers[0] = marker(OW_MARK_ITEM_BALL, 64, 112, "POTION", NULL);   /* on the cone's 3rd tile */
    uint32_t n = overworld_label_rects(&c, &s, 2.0f, l);
    UiRect cone = {64 * 2.0f, 80 * 2.0f, 16 * 2.0f, 48 * 2.0f};
    for (uint32_t i = 0; i < n; i++)
        if (l[i].source == OW_LABEL_MARKER) ASSERT_TRUE(!overlap(l[i].rect, cone));
}

TEST(owd_labels_stay_inside_canvas) {
    UiCanvas c;
    static OverworldSnapshot s;
    OwLabel l[OW_MAX_LABELS];
    canvas(&c);
    const int16_t xs[] = {-12, 0, 120, 230};
    const int16_t ys[] = {-12, 0, 72, 150};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            memset(&s, 0, sizeof(s));
            s.visible = true;
            s.trainer_count = 1;
            s.trainers[0] = youngster();
            s.trainers[0].x = xs[i]; s.trainers[0].y = ys[j];
            s.marker_count = 1;
            s.markers[0] = marker(OW_MARK_ITEM_BALL, xs[i], ys[j], "RARE CANDY", NULL);
            uint32_t n = overworld_label_rects(&c, &s, 2.0f, l);
            for (uint32_t k = 0; k < n; k++) {
                ASSERT_TRUE(l[k].rect.x >= 0.0f && l[k].rect.y >= 0.0f);
                ASSERT_TRUE(l[k].rect.x + l[k].rect.w <= (float)W && l[k].rect.y + l[k].rect.h <= (float)H);
            }
        }
    }
}

TEST(owd_many_labels_stay_inside_canvas) {
    UiCanvas c;
    static OverworldSnapshot s;
    OwLabel l[OW_MAX_LABELS];
    canvas(&c);
    memset(&s, 0, sizeof(s));
    s.visible = true;
    s.marker_count = OW_MAX_MARKERS;
    for (int i = 0; i < OW_MAX_MARKERS; i++)
        s.markers[i] = marker(OW_MARK_ITEM_BALL, (int16_t)(16 * (i % 8)), (int16_t)(16 * (i / 8)), "HEART SCALE", NULL);
    uint32_t n = overworld_label_rects(&c, &s, 2.0f, l);
    ASSERT_EQ(n, OW_MAX_MARKERS);
    for (uint32_t k = 0; k < n; k++) ASSERT_TRUE(l[k].rect.x >= 0.0f && l[k].rect.x + l[k].rect.w <= (float)W);
    overworld_draw(&c, &s, 2.0f);   /* must not crash */
}

void run_overworld_draw_tests(void) {
    RUN_TEST(owd_marker_text);
    RUN_TEST(owd_pill_tints_by_kind);
    RUN_TEST(owd_adjacent_labels_do_not_overlap);
    RUN_TEST(owd_trainer_and_gift_labels_do_not_overlap);
    RUN_TEST(owd_label_avoids_cone);
    RUN_TEST(owd_labels_stay_inside_canvas);
    RUN_TEST(owd_many_labels_stay_inside_canvas);
    RUN_TEST(owd_pill_text);
    RUN_TEST(owd_invisible_draws_nothing);
    RUN_TEST(owd_cone_covers_clear_tiles_only);
    RUN_TEST(owd_beaten_cone_is_grey);
    RUN_TEST(owd_long_cone_terminates);
    RUN_TEST(owd_hidden_item_marked);
}
