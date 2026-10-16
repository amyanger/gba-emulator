#include "test_harness.h"
#include "game/badge_pic.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x1000];

#define ROM_BASE 0x08000000u
#define GFX 0x08000100u

static const GameProfile k_profile = { .name = "test", .rules = &g_emerald_rules, .badge_gfx = GFX };

static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* All-literal LZ77 stream of the 0x400-byte sheet at addr. */
static void put_sheet(uint32_t addr, const uint8_t* data) {
    uint8_t* p = s_rom + (addr - ROM_BASE);
    put32(p, 0x10u | (0x400u << 8));
    uint32_t o = 4;
    for (uint32_t i = 0; i < 0x400; i += 8) {
        p[o++] = 0;
        for (uint32_t j = i; j < i + 8; j++) p[o++] = data[j];
    }
}

static void reset(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
}

static uint8_t g_tiles[0x400];

TEST(decode_pixels_nibbles_and_tiles) {
    reset();
    memset(g_tiles, 0, sizeof(g_tiles));
    /* badge 0 (x 0,1 / y 0): tile 0 byte 0 -> even x low nibble 3, odd x high nibble 9 */
    g_tiles[0] = 0x93;
    /* badge 0, y 7, x 7: tile 0 row 7 byte 3, high nibble 4 */
    g_tiles[7 * 4 + 3] = 0x40;
    /* badge 0, y 8, x 0: tile 16 row 0 */
    g_tiles[16 * 32] = 0x0F;
    /* badge 0, x 8, y 0: tile 1 */
    g_tiles[1 * 32] = 0x02;
    /* badge 7, x 15, y 15: tile 16 + 14 + 1 = 31, row 7 byte 3 high nibble */
    g_tiles[31 * 32 + 7 * 4 + 3] = 0xF0;
    /* badge 7, x 0, y 0: tile 14 */
    g_tiles[14 * 32] = 0x01;
    put_sheet(GFX, g_tiles);

    GameMem m = mem();
    BadgeArt art;
    memset(&art, 0xAA, sizeof(art));
    badge_pic_load(&m, &k_profile, &art);
    ASSERT_TRUE(art.ok);
    ASSERT_EQ(art.px[0][0], 3);
    ASSERT_EQ(art.px[0][1], 9);
    ASSERT_EQ(art.px[0][7 * 16 + 7], 4);
    ASSERT_EQ(art.px[0][8 * 16 + 0], 15);
    ASSERT_EQ(art.px[0][8], 2);
    ASSERT_EQ(art.px[7][15 * 16 + 15], 15);
    ASSERT_EQ(art.px[7][0], 1);
    ASSERT_EQ(art.px[1][0], 0);
}

TEST(no_art_without_address_or_profile) {
    reset();
    memset(g_tiles, 1, sizeof(g_tiles));
    put_sheet(GFX, g_tiles);
    GameMem m = mem();
    BadgeArt art;
    GameProfile none = k_profile;
    none.badge_gfx = 0;
    memset(&art, 0xAA, sizeof(art));
    badge_pic_load(&m, &none, &art);
    ASSERT_TRUE(!art.ok);
    ASSERT_EQ(art.px[3][5], 0);
    memset(&art, 0xAA, sizeof(art));
    badge_pic_load(&m, NULL, &art);
    ASSERT_TRUE(!art.ok);
    ASSERT_EQ(art.px[7][255], 0);
}

TEST(bad_header_fails) {
    reset();
    memset(g_tiles, 1, sizeof(g_tiles));
    put_sheet(GFX, g_tiles);
    s_rom[GFX - ROM_BASE] = 0x30;
    GameMem m = mem();
    BadgeArt art;
    badge_pic_load(&m, &k_profile, &art);
    ASSERT_TRUE(!art.ok);
    ASSERT_EQ(art.px[0][0], 0);
}

TEST(truncated_stream_fails) {
    reset();
    memset(g_tiles, 1, sizeof(g_tiles));
    uint32_t addr = ROM_BASE + (uint32_t)sizeof(s_rom) - 0x100;
    put_sheet(ROM_BASE + 0x200, g_tiles);
    uint8_t* p = s_rom + (addr - ROM_BASE);
    put32(p, 0x10u | (0x400u << 8));
    GameProfile prof = k_profile;
    prof.badge_gfx = addr;
    GameMem m = mem();
    BadgeArt art;
    badge_pic_load(&m, &prof, &art);
    ASSERT_TRUE(!art.ok);
}

void run_badge_pic_tests(void) {
    TEST_SUITE("badge_pic");
    RUN_TEST(decode_pixels_nibbles_and_tiles);
    RUN_TEST(no_art_without_address_or_profile);
    RUN_TEST(bad_header_fails);
    RUN_TEST(truncated_stream_fails);
}
