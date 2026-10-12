#include "test_harness.h"
#include "game_fixture.h"
#include "game/feebas.h"
#include <stdio.h>

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x4000];

#define SB1       0x02010000u
#define MAP_HDR   0x02003200u
#define GRID      0x02020000u
#define BACKUP    0x03000300u
#define LAYOUT    0x08001000u
#define TILESET1  0x08001100u
#define TILESET2  0x08001120u
#define ATTRS1    0x08001200u   /* u16[512] */
#define ATTRS2    0x08001600u
#define SECTIONS  0x08002000u   /* u16[3][3] {yMin, yMax, base} */
#define TILEBITS  0x08002100u   /* u8[256] */

static const GameProfile k_profile = {
    .name = "test",
    .save_block1_ptr = 0x03000000,
    .map_header = MAP_HDR,
    .backup_map_layout = BACKUP, .backup_map_data = GRID,
    .feebas_map_group = 0, .feebas_map_num = 34,
    .feebas_water_tiles = SECTIONS, .tile_bit_attributes = TILEBITS,
};

/* Layout 10x12: sections are rows 0-3, 4-7, 8-11. VMap is 25x26. */
#define LW 10
#define LH 12
#define GW (LW + 15)
#define GH (LH + 14)
/* Metatile ids: 1 land, 2 ocean water, 3 waterfall, 4 bridge over ocean. */
#define MT_LAND   1
#define MT_WATER  2
#define MT_FALL   3
#define MT_BRIDGE 4

static uint8_t* at(uint32_t a) {
    if (a >= 0x08000000u) return s_rom + (a - 0x08000000u);
    if (a >= 0x03000000u) return s_iwram + (a - 0x03000000u);
    return s_ewram + (a - 0x02000000u);
}
static void put16(uint32_t a, uint16_t v) { fixture_put16(at(a), v); }
static void put32(uint32_t a, uint32_t v) { fixture_put32(at(a), v); }
static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

/* Map coords; collision 0..3, elevation 0..15. */
static void cell(int x, int y, uint16_t mt, uint16_t collision, uint16_t elev) {
    put16(GRID + 2u * (uint32_t)((y + 7) * GW + (x + 7)), (uint16_t)(mt | (collision << 10) | (elev << 12)));
}
static void water(int x, int y) { cell(x, y, MT_WATER, 0, 1); }

static void sections(uint16_t b0, uint16_t b1, uint16_t b2) {
    const uint16_t t[9] = {0, 3, b0, 4, 7, b1, 8, 11, b2};
    for (int i = 0; i < 9; i++) put16(SECTIONS + 2u * (uint32_t)i, t[i]);
}

static void scene(uint16_t seed) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    put32(0x03000000, SB1);
    at(SB1 + 0x04)[0] = 0;
    at(SB1 + 0x05)[0] = 34;
    put16(SB1 + 0x2E6A, seed);
    put32(BACKUP + 0, GW); put32(BACKUP + 4, GH); put32(BACKUP + 8, GRID);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) put16(GRID + 2u * (uint32_t)(y * GW + x), 0x3000 | MT_LAND);
    put32(MAP_HDR, LAYOUT);
    put32(LAYOUT + 0x00, LW); put32(LAYOUT + 0x04, LH);
    put32(LAYOUT + 0x10, TILESET1); put32(LAYOUT + 0x14, TILESET2);
    put32(TILESET1 + 0x10, ATTRS1); put32(TILESET2 + 0x10, ATTRS2);
    put16(ATTRS1 + 2 * MT_LAND, 0x00);
    put16(ATTRS1 + 2 * MT_WATER, 0x15);
    put16(ATTRS1 + 2 * MT_FALL, 0x13);
    put16(ATTRS1 + 2 * MT_BRIDGE, 0x70);
    at(TILEBITS + 0x15)[0] = 0x03;   /* encounters + surfable */
    at(TILEBITS + 0x13)[0] = 0x02;   /* surfable */
    sections(0, 200, 400);
}

static const FeebasTile* find(const FeebasState* s, int16_t x, int16_t y) {
    for (uint8_t i = 0; i < s->count; i++)
        if (s->tile[i].x == x && s->tile[i].y == y) return &s->tile[i];
    return NULL;
}

static void check_ids(uint16_t seed, const uint16_t want[FEEBAS_SPOT_COUNT]) {
    uint16_t got[FEEBAS_SPOT_COUNT];
    feebas_spot_ids(seed, got);
    for (int i = 0; i < FEEBAS_SPOT_COUNT; i++) ASSERT_EQ(got[i], want[i]);
}

/* Expected lists from the reference model, matched by the ROM's own code in the oracle. */
TEST(fb_spot_ids_known_seeds) {
    static const uint16_t a[] = {447, 149, 291, 351, 369, 78};   /* first draw is 0 -> 447 */
    static const uint16_t b[] = {247, 306, 425, 132, 230, 377};
    static const uint16_t c[] = {109, 132, 61, 106, 386, 99};
    check_ids(0x0000, a);
    check_ids(0x1234, b);
    check_ids(0xFFFF, c);
}

TEST(fb_spot_ids_reject_1_to_3) {
    /* Raw draws 385, 151, 96, 3, 9, 165, 383: the 3 is redrawn. */
    static const uint16_t want[] = {385, 151, 96, 9, 165, 383};
    check_ids(0x0022, want);
}

TEST(fb_counts_in_game_order) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(146, 200, 400);
    water(2, 1);                       /* 147 */
    cell(3, 1, MT_FALL, 0, 1);         /* waterfall: not counted */
    water(5, 1);                       /* 148 */
    water(1, 2);                       /* 149, a spot for seed 0 */
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_TRUE(s.active);
    ASSERT_EQ(s.count, 1);
    ASSERT_EQ(s.tile[0].x, 1);
    ASSERT_EQ(s.tile[0].y, 2);
    ASSERT_EQ(s.tile[0].spot, 149);
    ASSERT_TRUE(s.tile[0].fishable);
    ASSERT_TRUE(!s.tile[0].bridge);
}

TEST(fb_section_bases) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(0, 290, 350);
    water(0, 4);                       /* 291 */
    water(4, 5);                       /* 292 */
    water(9, 8);                       /* 351 */
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_EQ(s.count, 2);
    ASSERT_TRUE(find(&s, 0, 4) && find(&s, 0, 4)->spot == 291);
    ASSERT_TRUE(find(&s, 9, 8) && find(&s, 9, 8)->spot == 351);
}

TEST(fb_collision_blocked) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(148, 200, 400);
    cell(2, 1, MT_WATER, 1, 0);        /* 149, collision bit set */
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_EQ(s.count, 1);
    ASSERT_TRUE(!s.tile[0].fishable);
}

TEST(fb_duplicate_spots_listed_once) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(161, 200, 400);           /* seed 0xA4F9: 162, 92, 206, 296, 162, 92 */
    water(2, 1);                       /* 162 */
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0xA4F9, &s));
    ASSERT_EQ(s.count, 1);
}

TEST(fb_bridge_fallthrough_needs_surf_neighbour) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(76, 200, 400);            /* one water tile = 77, fall-through 78 (a spot for seed 0) */
    water(2, 2);
    cell(2, 1, MT_BRIDGE, 0, 15);
    cell(7, 1, MT_BRIDGE, 0, 15);      /* no water next to it */
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_EQ(s.count, 1);
    ASSERT_EQ(s.tile[0].x, 2);
    ASSERT_EQ(s.tile[0].y, 1);
    ASSERT_EQ(s.tile[0].spot, 78);
    ASSERT_TRUE(s.tile[0].bridge);
    ASSERT_TRUE(s.tile[0].fishable);
}

TEST(fb_bridge_walled_in_omitted) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(76, 200, 400);
    cell(2, 2, MT_WATER, 1, 0);        /* blocked water: counted, but no surfer can sit there */
    cell(2, 1, MT_BRIDGE, 0, 15);
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_EQ(s.count, 0);
}

TEST(fb_inactive_off_route) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(148, 200, 400);
    water(1, 2);                       /* 149, but the player is on another map */
    at(SB1 + 0x05)[0] = 33;
    ASSERT_TRUE(!feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_TRUE(!s.active);
    ASSERT_EQ(s.count, 0);
}

TEST(fb_inactive_dims_mismatch) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    put32(BACKUP + 0, GW + 1);          /* VMap of another layout, mid transition */
    ASSERT_TRUE(!feebas_compute(&k_profile, &m, 0, &s));
    ASSERT_TRUE(!s.active);
}

TEST(fb_inactive_bad_section_table) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    put16(SECTIONS + 2 * 7, LH);        /* section 2 yMax past the layout */
    ASSERT_TRUE(!feebas_compute(&k_profile, &m, 0, &s));
}

TEST(fb_inactive_null_profile) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    ASSERT_TRUE(!feebas_read(NULL, &m, &s));
    ASSERT_TRUE(!s.active);
}

TEST(fb_seed_change_moves_tiles) {
    FeebasState s;
    GameMem m = mem();
    scene(0);
    sections(148, 200, 400);
    water(1, 2);                       /* 149: a spot for seed 0, not for 0x1234 */
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0x0000, &s));
    ASSERT_EQ(s.count, 1);
    ASSERT_TRUE(feebas_compute(&k_profile, &m, 0x1234, &s));
    ASSERT_EQ(s.count, 0);
    ASSERT_EQ(s.spots[0], 247);
}

TEST(fb_read_uses_trend_seed) {
    FeebasState s;
    GameMem m = mem();
    scene(0x1234);
    ASSERT_TRUE(feebas_read(&k_profile, &m, &s));
    ASSERT_EQ(s.seed, 0x1234);
    ASSERT_EQ(s.spots[0], 247);
}

TEST(fb_dump_format) {
    FeebasState s;
    GameMem m = mem();
    char buf[512];
    scene(0);
    sections(148, 200, 400);
    water(1, 2);                       /* 149 */
    feebas_read(&k_profile, &m, &s);
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(feebas_dump(&s, f));
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_TRUE(strstr(buf, "[feebas]\nactive 1\nseed 0000\nspots 447,149,291,351,369,78\n"
                            "tile x=1 y=2 spot=149 fishable=1 bridge=0\n") != NULL);
}

void run_feebas_tests(void) {
    RUN_TEST(fb_spot_ids_known_seeds);
    RUN_TEST(fb_spot_ids_reject_1_to_3);
    RUN_TEST(fb_counts_in_game_order);
    RUN_TEST(fb_section_bases);
    RUN_TEST(fb_collision_blocked);
    RUN_TEST(fb_duplicate_spots_listed_once);
    RUN_TEST(fb_bridge_fallthrough_needs_surf_neighbour);
    RUN_TEST(fb_bridge_walled_in_omitted);
    RUN_TEST(fb_inactive_off_route);
    RUN_TEST(fb_inactive_dims_mismatch);
    RUN_TEST(fb_inactive_bad_section_table);
    RUN_TEST(fb_inactive_null_profile);
    RUN_TEST(fb_seed_change_moves_tiles);
    RUN_TEST(fb_read_uses_trend_seed);
    RUN_TEST(fb_dump_format);
}
