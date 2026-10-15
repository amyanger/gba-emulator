#include "test_harness.h"
#include "game/mon_pic.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x8000];

#define ROM_BASE 0x08000000u
#define PICS 0x08000100u     /* still front pic table */
#define PALS 0x08000900u     /* palette table */
#define SHINY 0x08001100u    /* shiny palette table */
#define STREAMS 0x08002000u

static const GameProfile k_profile = {
    .name = "test",
    .mon_still_front_pics = PICS, .mon_palettes = PALS, .mon_shiny_palettes = SHINY,
};

static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

static uint8_t* at(uint32_t addr) { return s_rom + (addr - ROM_BASE); }

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* All-literal LZ77 stream; returns its length. */
static uint32_t put_lz(uint32_t addr, const uint8_t* data, uint32_t n) {
    uint8_t* p = at(addr);
    put32(p, 0x10u | (n << 8));
    uint32_t o = 4;
    for (uint32_t i = 0; i < n; i += 8) {
        p[o++] = 0;
        for (uint32_t j = i; j < i + 8 && j < n; j++) p[o++] = data[j];
    }
    return o;
}

static void reset(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
}

TEST(lz77_literals) {
    reset();
    const uint8_t d[4] = {'A', 'B', 'C', 'D'};
    put_lz(STREAMS, d, 4);
    GameMem m = mem();
    uint8_t out[4];
    ASSERT_TRUE(mon_pic_lz77(&m, STREAMS, out, 4));
    ASSERT_MEM_EQ(out, d, 4);
}

TEST(lz77_backref_overlap) {
    reset();
    uint8_t* p = at(STREAMS);
    put32(p, 0x10u | (8u << 8));
    p[4] = 0x20;                 /* literal, literal, ref */
    p[5] = 'A'; p[6] = 'B';
    p[7] = 0x30; p[8] = 0x01;    /* len 6, dist 2 */
    GameMem m = mem();
    uint8_t out[8];
    ASSERT_TRUE(mon_pic_lz77(&m, STREAMS, out, 8));
    ASSERT_MEM_EQ(out, "ABABABAB", 8);
}

TEST(lz77_truncates_to_need) {
    reset();
    uint8_t d[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    put_lz(STREAMS, d, 8);
    put32(at(STREAMS), 0x10u | (0x2000u << 8));
    GameMem m = mem();
    uint8_t out[4];
    ASSERT_TRUE(mon_pic_lz77(&m, STREAMS, out, 4));
    ASSERT_MEM_EQ(out, d, 4);
}

TEST(lz77_rejects_small_header) {
    reset();
    const uint8_t d[4] = {1, 2, 3, 4};
    put_lz(STREAMS, d, 4);
    put32(at(STREAMS), 0x10u | (2u << 8));
    GameMem m = mem();
    uint8_t out[4];
    ASSERT_TRUE(!mon_pic_lz77(&m, STREAMS, out, 4));
}

TEST(lz77_rejects_bad_type) {
    reset();
    const uint8_t d[4] = {1, 2, 3, 4};
    put_lz(STREAMS, d, 4);
    at(STREAMS)[0] = 0x11;
    GameMem m = mem();
    uint8_t out[4];
    ASSERT_TRUE(!mon_pic_lz77(&m, STREAMS, out, 4));
}

TEST(lz77_rejects_ref_before_start) {
    reset();
    uint8_t* p = at(STREAMS);
    put32(p, 0x10u | (4u << 8));
    p[4] = 0x80; p[5] = 0x00; p[6] = 0x00;
    GameMem m = mem();
    uint8_t out[4];
    ASSERT_TRUE(!mon_pic_lz77(&m, STREAMS, out, 4));
}

TEST(lz77_rejects_past_rom_end) {
    reset();
    uint32_t addr = ROM_BASE + (uint32_t)sizeof(s_rom) - 6;
    uint8_t* p = at(addr);
    put32(p, 0x10u | (8u << 8));
    p[4] = 0; p[5] = 'A';        /* stream ends after one literal */
    GameMem m = mem();
    uint8_t out[8];
    ASSERT_TRUE(!mon_pic_lz77(&m, addr, out, 8));
}

TEST(decode_species_bounds) {
    reset();
    GameMem m = mem();
    static uint32_t px[MON_PIC_PIXELS];
    ASSERT_TRUE(!mon_pic_decode(&m, &k_profile, 0, false, false, px));
    ASSERT_TRUE(!mon_pic_decode(&m, &k_profile, 412, false, false, px));
    ASSERT_TRUE(!mon_pic_decode(&m, &k_profile, 9999, false, false, px));
}

static void build_species(uint16_t species, uint32_t pic_addr, uint32_t pal_addr,
                          uint32_t shiny_addr, uint16_t c1, uint16_t c2, uint16_t s1) {
    static uint8_t tiles[0x800];
    memset(tiles, 0, sizeof(tiles));
    tiles[0] = 0x21;             /* tile 0: (0,0)=1, (1,0)=2 */
    tiles[32] = 0x01;            /* tile 1: (0,0)=1 */
    tiles[8 * 32] = 0x02;        /* tile 8: (0,0)=2 */
    put_lz(pic_addr, tiles, sizeof(tiles));
    uint8_t pal[32] = {0};
    pal[2] = (uint8_t)c1; pal[3] = (uint8_t)(c1 >> 8);
    pal[4] = (uint8_t)c2; pal[5] = (uint8_t)(c2 >> 8);
    put_lz(pal_addr, pal, 32);
    pal[2] = (uint8_t)s1; pal[3] = (uint8_t)(s1 >> 8);
    put_lz(shiny_addr, pal, 32);
    put32(at(PICS + 8u * species), pic_addr);
    put32(at(PALS + 8u * species), pal_addr);
    put32(at(SHINY + 8u * species), shiny_addr);
}

TEST(decode_tiles_and_palette) {
    reset();
    build_species(1, STREAMS, STREAMS + 0x1000, STREAMS + 0x1100, 0x001F, 0x7C00, 0x03E0);
    GameMem m = mem();
    static uint32_t px[MON_PIC_PIXELS];
    ASSERT_TRUE(mon_pic_decode(&m, &k_profile, 1, false, false, px));
    ASSERT_EQ(px[0], 0xFFFF0000u);
    ASSERT_EQ(px[1], 0xFF0000FFu);
    ASSERT_EQ((px[2] >> 24), 0);
    ASSERT_EQ(px[8], 0xFFFF0000u);                  /* tile 1 */
    ASSERT_EQ(px[8 * MON_PIC_W], 0xFF0000FFu);      /* tile 8 */
}

TEST(decode_shiny_uses_shiny_table) {
    reset();
    build_species(1, STREAMS, STREAMS + 0x1000, STREAMS + 0x1100, 0x001F, 0x7C00, 0x03E0);
    GameMem m = mem();
    static uint32_t px[MON_PIC_PIXELS];
    ASSERT_TRUE(mon_pic_decode(&m, &k_profile, 1, false, true, px));
    ASSERT_EQ(px[0], 0xFF00FF00u);
}

TEST(decode_egg_uses_412) {
    reset();
    build_species(412, STREAMS, STREAMS + 0x1000, STREAMS + 0x1100, 0x001F, 0x7C00, 0x03E0);
    GameMem m = mem();
    static uint32_t px[MON_PIC_PIXELS];
    ASSERT_TRUE(mon_pic_decode(&m, &k_profile, 25, true, false, px));
    ASSERT_EQ(px[0], 0xFFFF0000u);
}

void run_mon_pic_tests(void) {
    TEST_SUITE("mon_pic");
    RUN_TEST(lz77_literals);
    RUN_TEST(lz77_backref_overlap);
    RUN_TEST(lz77_truncates_to_need);
    RUN_TEST(lz77_rejects_small_header);
    RUN_TEST(lz77_rejects_bad_type);
    RUN_TEST(lz77_rejects_ref_before_start);
    RUN_TEST(lz77_rejects_past_rom_end);
    RUN_TEST(decode_species_bounds);
    RUN_TEST(decode_tiles_and_palette);
    RUN_TEST(decode_shiny_uses_shiny_table);
    RUN_TEST(decode_egg_uses_412);
}
