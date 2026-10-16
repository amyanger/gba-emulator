#include "test_harness.h"
#include "game_fixture.h"
#include "game/berry.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x1000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define MAP_HEADER 0x02030000u
#define TREES (SB1 + 0x169Cu)

/* ROM: one map group with two maps (sections 10 and 11), gBerries at 0x800. */
static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .main_in_battle = 0x03000010,
    .map_header = MAP_HEADER,
    .map_groups = 0x08000100, .map_group_count = 1,
    .region_map_entries = 0x08000600, .region_map_count = 12,
    .berries = 0x08000800,
};

static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

static uint8_t* at(uint32_t a) {
    if (a >= 0x08000000) return s_rom + (a - 0x08000000);
    if (a >= 0x03000000) return s_iwram + (a - 0x03000000);
    return s_ewram + (a - 0x02000000);
}

/* Gen 3 text: 'A' = 0xBB, EOS = 0xFF. */
static void put_text(uint32_t a, const char* s) {
    uint8_t* p = at(a);
    while (*s) *p++ = (uint8_t)(0xBB + (*s++ - 'A'));
    *p = 0xFF;
}

static void put_template(uint32_t a, uint8_t movement, uint16_t tree) {
    at(a)[0x09] = movement;
    fixture_put16(at(a + 0x0E), tree);
}

/* Berry b: min/max yield and stage hours. */
static void put_berry(uint8_t b, const char* name, uint8_t min, uint8_t max, uint8_t hours) {
    uint32_t a = 0x08000800u + (uint32_t)(b - 1) * 28u;
    put_text(a, name);
    at(a)[0x0A] = max;
    at(a)[0x0B] = min;
    at(a)[0x14] = hours;
}

static void reset(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    /* Group 0's map list sits right before gMapGroups, so it holds two maps. */
    fixture_put32(at(0x08000100), 0x080000F8);
    fixture_put32(at(0x080000F8), 0x08000200);
    fixture_put32(at(0x080000FC), 0x08000220);
    fixture_put32(at(0x08000204), 0x08000300);   /* events */
    at(0x08000214)[0] = 10;
    fixture_put32(at(0x08000224), 0x08000340);
    at(0x08000234)[0] = 11;
    at(0x08000300)[0] = 2;                       /* object count */
    fixture_put32(at(0x08000304), 0x08000400);
    put_template(0x08000400, 0x0C, 3);           /* berry tree 3 */
    put_template(0x08000418, 0x01, 4);           /* an NPC whose range field happens to be 4 */
    at(0x08000340)[0] = 1;
    fixture_put32(at(0x08000344), 0x08000430);
    put_template(0x08000430, 0x0C, 7);
    fixture_put32(at(0x08000600 + 10 * 8 + 4), 0x08000700);
    put_text(0x08000700, "ROUTE");
    fixture_put32(at(0x08000600 + 11 * 8 + 4), 0x08000710);
    put_text(0x08000710, "TOWN");
    put_berry(1, "CHERI", 2, 3, 3);
    put_berry(7, "ORAN", 2, 5, 4);
}

/* FLAG_SYS_CLOCK_SET (0x895) in SaveBlock1 flags at 0x1270. */
static void set_clock_flag(void) { at(SB1 + 0x1270 + (0x895 >> 3))[0] |= (uint8_t)(1u << (0x895 & 7)); }

static void put_tree(uint8_t id, uint8_t berry, uint8_t stage, uint16_t next, uint8_t yield, uint8_t b5) {
    uint8_t* t = at(TREES + 8u * id);
    t[0] = berry;
    t[1] = stage;
    fixture_put16(t + 2, next);
    t[4] = yield;
    t[5] = b5;
}

static const BerryTree* find(const BerryView* v, uint8_t id) {
    for (uint8_t i = 0; i < v->count; i++)
        if (v->tree[i].id == id) return &v->tree[i];
    return NULL;
}

/* CalcBerryYieldInternal with a chosen rand, as pret/pokeemerald src/berry.c computes it. */
static uint8_t game_yield(uint8_t min, uint8_t max, uint8_t water, uint32_t rand) {
    if (water == 0) return min;
    uint32_t extra = rand / 4 + ((rand % 4) >= 2 ? 1 : 0);
    return (uint8_t)(extra + min);
}

TEST(berry_yield_band_matches_game) {
    uint8_t lo, hi;
    berry_yield_band(2, 5, 0, &lo, &hi);
    ASSERT_EQ(lo, 2); ASSERT_EQ(hi, 2);
    berry_yield_band(2, 5, 1, &lo, &hi);
    ASSERT_EQ(lo, 2); ASSERT_EQ(hi, 3);
    berry_yield_band(2, 5, 2, &lo, &hi);
    ASSERT_EQ(lo, 3); ASSERT_EQ(hi, 4);
    berry_yield_band(2, 5, 4, &lo, &hi);
    ASSERT_EQ(lo, 4); ASSERT_EQ(hi, 5);
    for (uint8_t min = 1; min <= 6; min++)
        for (uint8_t max = min; max <= 20; max++)
            for (uint8_t w = 1; w <= 4; w++) {
                uint32_t r0 = (uint32_t)(max - min) * (w - 1u), r1 = (uint32_t)(max - min) * w;
                berry_yield_band(min, max, w, &lo, &hi);
                ASSERT_EQ(lo, game_yield(min, max, w, r0));
                ASSERT_EQ(hi, game_yield(min, max, w, r1));
                for (uint32_t r = r0; r <= r1; r++) {
                    uint8_t y = game_yield(min, max, w, r);
                    ASSERT_TRUE(y >= lo && y <= hi);
                }
            }
}

TEST(berry_projection_advances_and_x4) {
    uint8_t t[8] = {7, 4, 10, 0, 0, 0, 0, 0};
    berry_project(t, 60, 70);
    ASSERT_EQ(t[1], 5);
    ASSERT_EQ(t[2] | (t[3] << 8), 180);
}

TEST(berry_projection_regrows_and_wipes_at_10) {
    uint8_t t[8] = {7, 5, 5, 0, 4, 0xF0 | 3, 0, 0};
    berry_project(t, 60, 5);
    ASSERT_EQ(t[1], 2);           /* back to sprouted */
    ASSERT_EQ(t[4], 0);           /* yield cleared */
    ASSERT_EQ(t[5], 4);           /* watered bits cleared, regrowth 3 -> 4 */
    ASSERT_EQ(t[2] | (t[3] << 8), 60);
    uint8_t u[8] = {7, 5, 5, 0, 4, 9, 0, 0};
    berry_project(u, 60, 5);
    ASSERT_EQ(u[0], 0);
    ASSERT_EQ(u[1], 0);
}

TEST(berry_projection_71x_wipes) {
    uint8_t t[8] = {7, 2, 30, 0, 0, 0, 0, 0};
    berry_project(t, 60, 71 * 60);
    ASSERT_EQ(t[0], 0);
    uint8_t p[8] = {7, 2 | 0x80, 30, 0, 0, 0, 0, 0};   /* stopGrowth */
    berry_project(p, 60, 100000);
    ASSERT_EQ(p[0], 7);
    ASSERT_EQ(p[2], 30);
}

TEST(berry_local_minutes_borrows) {
    MirageClock c = { true, 0, 1, 2, 0, 0, 30 };
    MirageTime off = { 0, 0, 1, 0 };
    ASSERT_EQ(berry_local_minutes(&c, &off), 1 * 1440 + 23 * 60 + 59);
}

TEST(berry_places_from_fixture_map) {
    static BerryPlaces bp;
    reset();
    GameMem m = mem();
    berry_places_build(&bp, &k_profile, &m);
    ASSERT_EQ(bp.count, 2);
    ASSERT_EQ(bp.place_of[3], 0);
    ASSERT_EQ(bp.place_of[7], 1);
    ASSERT_EQ(bp.place_of[4], BERRY_PLACE_NONE);
    ASSERT_EQ(bp.place_of[1], BERRY_PLACE_NONE);
    ASSERT_EQ(bp.place[0].sec, 10);
    ASSERT_EQ(bp.place[0].spots, 1);
    ASSERT_STR_EQ(bp.place[0].name, "ROUTE");
    ASSERT_STR_EQ(bp.place[1].name, "TOWN");
}

TEST(berry_read_statuses) {
    static BerryPlaces bp;
    static BerryView v;
    reset();
    GameMem m = mem();
    berry_places_build(&bp, &k_profile, &m);
    set_clock_flag();
    put_tree(3, 7, 5, 30, 4, 0xF0);           /* ripe, 30 min left: falling */
    put_tree(4, 7, 5, 600, 3, 0x30);          /* ripe */
    put_tree(5, 7, 2, 100, 0, 0x10);          /* sprouted, stage 2 not watered */
    put_tree(6, 7, 2, 100, 0, 0x30);          /* sprouted and watered */
    put_tree(7, 7, 3 | 0x80, 100, 0, 0);      /* not seen yet */
    at(MAP_HEADER + 0x14)[0] = 11;
    MirageClock none = {0};
    berry_read(&bp, &k_profile, &m, &none, &v);
    ASSERT_TRUE(v.valid);
    ASSERT_TRUE(v.clock_set);
    ASSERT_TRUE(!v.projected);
    ASSERT_EQ(v.count, 5);
    ASSERT_EQ(v.here, 1);
    ASSERT_EQ(find(&v, 3)->status, BERRY_FALLING);
    ASSERT_EQ(find(&v, 3)->yield_lo, 4);
    ASSERT_EQ(find(&v, 3)->yield_hi, 4);
    ASSERT_EQ(find(&v, 4)->status, BERRY_RIPE);
    ASSERT_EQ(find(&v, 5)->status, BERRY_WATER);
    ASSERT_EQ(find(&v, 6)->status, BERRY_GROWING);
    ASSERT_EQ(find(&v, 7)->status, BERRY_PAUSED);
    ASSERT_STR_EQ(find(&v, 6)->name, "ORAN");
    /* 100 min left in stage 2, then two 4 h stages. */
    ASSERT_EQ(find(&v, 6)->ripe_min, 100u + 2u * 240u);
    /* Watered 2 so far, stages 3 and 4 still ahead: best band is for 4 waterings. */
    ASSERT_EQ(find(&v, 6)->yield_lo, 4);
    ASSERT_EQ(find(&v, 6)->yield_hi, 5);
    /* Stage 2 missed (current, unwatered) still counts: it can be watered now. */
    ASSERT_EQ(find(&v, 5)->yield_hi, 5);
    ASSERT_EQ(v.growing[0], 1);   /* tree 3 at place 0; 4-6 have no place */
    ASSERT_EQ(v.growing[1], 1);
    ASSERT_EQ(v.place_count, 2);
    ASSERT_STR_EQ(v.place[1].name, "TOWN");
}

TEST(berry_read_projects_with_clock) {
    static BerryPlaces bp;
    static BerryView v;
    reset();
    GameMem m = mem();
    berry_places_build(&bp, &k_profile, &m);
    set_clock_flag();
    put_tree(3, 7, 4, 10, 0, 0xF0);
    /* Last update at local day 5 10:00; clock 2000-01-05 11:10:00, offset 0: 70 min later. */
    fixture_put16(at(SB2 + 0xA0), 5);
    at(SB2 + 0xA2)[0] = 10;
    MirageClock c = { true, 0, 1, 5, 11, 10, 0 };
    berry_read(&bp, &k_profile, &m, &c, &v);
    ASSERT_TRUE(v.projected);
    ASSERT_EQ(find(&v, 3)->stage, 5);
    ASSERT_EQ(find(&v, 3)->next_min, 4 * 240 - 60);
    ASSERT_EQ(find(&v, 3)->status, BERRY_RIPE);
    ASSERT_EQ(find(&v, 3)->yield_lo, 4);   /* rolled later by the game: band for 4 waterings */
    ASSERT_EQ(find(&v, 3)->yield_hi, 5);
}

TEST(berry_projection_without_clock_uses_raw) {
    static BerryPlaces bp;
    static BerryView v;
    reset();
    GameMem m = mem();
    berry_places_build(&bp, &k_profile, &m);
    put_tree(3, 7, 2, 77, 0, 0);
    fixture_put16(at(SB2 + 0xA0), 5);
    MirageClock c = { true, 0, 1, 9, 0, 0, 0 };
    berry_read(&bp, &k_profile, &m, &c, &v);   /* clock flag not set */
    ASSERT_TRUE(!v.clock_set);
    ASSERT_TRUE(!v.projected);
    ASSERT_EQ(find(&v, 3)->next_min, 77);
    /* A clock behind the last update never moves timers backwards. */
    set_clock_flag();
    fixture_put16(at(SB2 + 0xA0), 20);
    berry_read(&bp, &k_profile, &m, &c, &v);
    ASSERT_TRUE(!v.projected);
    ASSERT_EQ(find(&v, 3)->next_min, 77);
}

TEST(berry_bad_ids_skipped) {
    static BerryPlaces bp;
    static BerryView v;
    reset();
    GameMem m = mem();
    berry_places_build(&bp, &k_profile, &m);
    put_tree(3, 44, 2, 10, 0, 0);
    put_tree(4, 7, 0, 10, 0, 0);    /* berry without a stage */
    put_tree(5, 7, 6, 10, 0, 0);    /* stage out of range */
    berry_read(&bp, &k_profile, &m, NULL, &v);
    ASSERT_TRUE(v.valid);
    ASSERT_EQ(v.count, 0);
    fixture_put32(at(0x03000000), 0x08000000);
    berry_read(&bp, &k_profile, &m, NULL, &v);
    ASSERT_TRUE(!v.valid);
}

void run_berry_tests(void) {
    RUN_TEST(berry_yield_band_matches_game);
    RUN_TEST(berry_projection_advances_and_x4);
    RUN_TEST(berry_projection_regrows_and_wipes_at_10);
    RUN_TEST(berry_projection_71x_wipes);
    RUN_TEST(berry_local_minutes_borrows);
    RUN_TEST(berry_places_from_fixture_map);
    RUN_TEST(berry_read_statuses);
    RUN_TEST(berry_read_projects_with_clock);
    RUN_TEST(berry_projection_without_clock_uses_raw);
    RUN_TEST(berry_bad_ids_skipped);
}
