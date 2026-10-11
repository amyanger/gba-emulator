#include <stdio.h>
#include <string.h>
#include "frontend/follower_draw.h"
#include "test_harness.h"

static void put_oam(uint8_t* oam, int i, uint8_t y, uint8_t shape, uint8_t size, uint8_t prio) {
    uint16_t a0 = (uint16_t)(y | (shape << 14));
    uint16_t a1 = (uint16_t)(size << 14);
    uint16_t a2 = (uint16_t)(prio << 10);
    oam[8 * i] = (uint8_t)a0; oam[8 * i + 1] = (uint8_t)(a0 >> 8);
    oam[8 * i + 2] = (uint8_t)a1; oam[8 * i + 3] = (uint8_t)(a1 >> 8);
    oam[8 * i + 4] = (uint8_t)a2; oam[8 * i + 5] = (uint8_t)(a2 >> 8);
}

static void clear_oam(uint8_t* oam) {
    memset(oam, 0, 1024);
    for (int i = 0; i < 128; i++) oam[8 * i + 1] = 0x02;   /* disabled */
}

TEST(slot_sorts_by_feet) {
    static uint8_t oam[1024];
    clear_oam(oam);
    put_oam(oam, 3, 40, 2, 2, 2);    /* 16x32 tall: feet 72 */
    put_oam(oam, 7, 20, 2, 2, 2);    /* feet 52 */
    ASSERT_EQ(follower_oam_slot(oam, 2, 60), 3);    /* entry 3 lower on screen: in front */
    ASSERT_EQ(follower_oam_slot(oam, 2, 80), -1);
    ASSERT_EQ(follower_oam_slot(oam, 2, 50), 7);
    ASSERT_EQ(follower_oam_slot(oam, 1, 50), -1);   /* other priority ignored */
}

TEST(fade_palette) {
    FollowerInput fi;
    memset(&fi, 0, sizeof(fi));
    uint16_t in[16], out[16];
    for (int i = 0; i < 16; i++) in[i] = 0x7FFF;
    fi.fade_mask = 0xFFFF0000u; fi.fade_y = 16; fi.fade_color = 0;
    follower_fade_palette(in, &fi, out);
    ASSERT_EQ_HEX(out[3], 0x0000);
    fi.fade_y = 8;
    follower_fade_palette(in, &fi, out);
    ASSERT_EQ_HEX(out[3], (15 << 10) | (15 << 5) | 15);
    fi.fade_mask = 0x0000FFFFu;                     /* BG only */
    follower_fade_palette(in, &fi, out);
    ASSERT_EQ_HEX(out[3], 0x7FFF);
}

TEST(fade_weather_fade_in) {
    FollowerInput fi;
    memset(&fi, 0, sizeof(fi));
    uint16_t in[16], out[16];
    for (int i = 0; i < 16; i++) in[i] = 0;
    fi.weather_state = 1; fi.weather_counter = 8; fi.weather_color = 0x7FFF;
    follower_fade_palette(in, &fi, out);            /* coefficient 16 - 8 toward white */
    ASSERT_EQ_HEX(out[1], (15 << 10) | (15 << 5) | 15);
    fi.weather_counter = 16;
    follower_fade_palette(in, &fi, out);
    ASSERT_EQ_HEX(out[1], 0x0000);
}

TEST(mood_clamp_and_lru) {
    static FollowerView v;
    memset(&v, 0, sizeof(v));
    follower_mood_add(&v, 1, 200);
    ASSERT_EQ(follower_mood_get(&v, 1), 127);
    follower_mood_add(&v, 1, -300);
    ASSERT_EQ(follower_mood_get(&v, 1), -127);
    for (uint32_t p = 2; p < 2 + FOLLOW_MOOD_SLOTS; p++) follower_mood_add(&v, p, 5);
    ASSERT_EQ(follower_mood_get(&v, 1), 0);         /* evicted */
}

TEST(mood_sidecar_round_trip) {
    static FollowerView v, w;
    memset(&v, 0, sizeof(v));
    memset(&w, 0, sizeof(w));
    snprintf(v.mood_path, sizeof(v.mood_path), "test_follow_tmp.follow");
    memcpy(w.mood_path, v.mood_path, sizeof(v.mood_path));
    follower_mood_add(&v, 0xABCDEF01u, -42);
    ASSERT_TRUE(follower_mood_save(&v));
    ASSERT_TRUE(follower_mood_load(&w));
    ASSERT_EQ(follower_mood_get(&w, 0xABCDEF01u), -42);
    remove(v.mood_path);
}

TEST(mood_sidecar_missing_or_malformed) {
    static FollowerView v;
    memset(&v, 0, sizeof(v));
    snprintf(v.mood_path, sizeof(v.mood_path), "test_follow_missing.follow");
    remove(v.mood_path);
    ASSERT_TRUE(!follower_mood_load(&v));
    FILE* f = fopen(v.mood_path, "w");
    ASSERT_TRUE(f != NULL);
    fputs("0000abcd 7\ngarbage\n", f);
    fclose(f);
    ASSERT_TRUE(!follower_mood_load(&v));
    ASSERT_EQ(follower_mood_get(&v, 0xABCDu), 0);   /* table left empty */
    remove(v.mood_path);
}

TEST(lead_change_reloads_gfx) {
    static FollowerView v;
    memset(&v, 0, sizeof(v));
    v.enabled = true;
    v.gfx_pid = 5;
    FollowerFeed fd;
    memset(&fd, 0, sizeof(fd));
    fd.sim.lead_pid = 6;
    fd.sim.vis = FOL_VISIBLE;
    PpuExtraObj objs[PPU_EXTRA_OBJ_MAX];
    static uint8_t oam[1024];
    clear_oam(oam);
    follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(!objs[0].on);                       /* stale sprite never drawn */
}

/* Visible 32x32 follower at VMap tile (5, 4) with screen origin (0, 0). */
static void visible_setup(FollowerView* v, FollowerFeed* fd) {
    memset(v, 0, sizeof(*v));
    v->enabled = true;
    v->gfx_pid = 6;
    v->gfx.size = 32;
    for (int i = 0; i < 16; i++) v->gfx.pal[0][i] = (uint16_t)(0x0100 + i);
    for (int i = 0; i < 32 * 32; i++) v->gfx.px[2][i] = (uint8_t)(i / 32 + 1);   /* row r holds r+1 */
    v->elev_prio[3] = 1;
    memset(fd, 0, sizeof(*fd));
    fd->sim.lead_pid = 6;
    fd->sim.vis = FOL_VISIBLE;
    fd->sim.facing = 1;                             /* south: frame 2 */
    fd->sim.tx = 5; fd->sim.ty = 4;
    fd->in.overworld = true;
    fd->sim.elevation = 3;
    fd->in.fol_elevation = 15;                     /* priority uses the kept elevation */
}

TEST(visible_follower_obj) {
    static FollowerView v;
    static FollowerFeed fd;
    static uint8_t oam[1024];
    PpuExtraObj objs[PPU_EXTRA_OBJ_MAX];
    visible_setup(&v, &fd);
    clear_oam(oam);
    put_oam(oam, 9, 70, 2, 2, 1);                   /* feet 102, lower than the follower's 80 */
    follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(objs[0].on);
    ASSERT_EQ(objs[0].x, 88 - 16);
    ASSERT_EQ(objs[0].y, 80 - 32 + 4);
    ASSERT_EQ(objs[0].w, 32);
    ASSERT_EQ(objs[0].h, 32);
    ASSERT_EQ(objs[0].priority, 1);
    ASSERT_EQ(objs[0].slot, 9);
    ASSERT_TRUE(objs[0].pixels == v.gfx.px[2]);
    ASSERT_EQ_HEX(objs[0].pal[4], 0x0104);
    ASSERT_TRUE(!objs[1].on);

    fd.in.in_transition = true;
    follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(!objs[0].on);
}

TEST(long_grass_splits_halves) {
    static FollowerView v;
    static FollowerFeed fd;
    static uint8_t oam[1024];
    PpuExtraObj objs[PPU_EXTRA_OBJ_MAX];
    visible_setup(&v, &fd);
    clear_oam(oam);
    fd.in.fol_behavior = 0x03;
    follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(objs[0].on && objs[1].on);
    ASSERT_EQ(objs[0].h, 16);
    ASSERT_EQ(objs[0].priority, 1);
    ASSERT_EQ(objs[0].pixels[0], 1);
    ASSERT_EQ(objs[1].y, objs[0].y + 16);
    ASSERT_EQ(objs[1].h, 16);
    ASSERT_EQ(objs[1].priority, 3);
    ASSERT_EQ(objs[1].pixels[0], 17);
}

TEST(tall_grass_obj_rustles) {
    static FollowerView v;
    static FollowerFeed fd;
    static uint8_t oam[1024];
    PpuExtraObj objs[PPU_EXTRA_OBJ_MAX];
    visible_setup(&v, &fd);
    clear_oam(oam);
    fd.in.fol_behavior = 0x02;
    follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(!objs[1].on);                       /* no palette: grass skipped */

    v.grass_ok = true;
    v.grass_pal[1] = 0x1234;
    fd.sim.tx = 6;                                  /* entering a new grass tile */
    follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(objs[1].on);
    ASSERT_EQ(objs[1].x, 96);
    ASSERT_EQ(objs[1].y, 64);
    ASSERT_EQ(objs[1].w, 16);
    ASSERT_EQ(objs[1].slot, objs[0].slot);
    ASSERT_EQ(objs[1].priority, objs[0].priority);
    ASSERT_TRUE(objs[1].pixels == v.grass_px[1]);
    ASSERT_EQ_HEX(objs[1].pal[1], 0x1234);
    for (int i = 0; i < 10; i++) follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(objs[1].pixels == v.grass_px[2]);
    for (int i = 0; i < 40; i++) follower_view_objs(&v, &fd, oam, objs);
    ASSERT_TRUE(objs[1].pixels == v.grass_px[0]);
}

void run_follower_draw_tests(void) {
    RUN_TEST(slot_sorts_by_feet);
    RUN_TEST(fade_palette);
    RUN_TEST(fade_weather_fade_in);
    RUN_TEST(mood_clamp_and_lru);
    RUN_TEST(mood_sidecar_round_trip);
    RUN_TEST(mood_sidecar_missing_or_malformed);
    RUN_TEST(lead_change_reloads_gfx);
    RUN_TEST(visible_follower_obj);
    RUN_TEST(long_grass_splits_halves);
    RUN_TEST(tall_grass_obj_rustles);
}
