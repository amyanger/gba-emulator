#include "test_harness.h"
#include "game/game_mem.h"
#include "game/emerald_profile.h"
#include <string.h>

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x200];

static GameMem make_mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

TEST(game_mem_reads_little_endian_from_each_region) {
    GameMem m = make_mem();
    s_ewram[0x10] = 0x78; s_ewram[0x11] = 0x56; s_ewram[0x12] = 0x34; s_ewram[0x13] = 0x12;
    s_iwram[0x7FFE] = 0xCD; s_iwram[0x7FFF] = 0xAB;
    s_rom[0x1FF] = 0x42;
    uint32_t v32; uint16_t v16; uint8_t v8;
    ASSERT_TRUE(game_mem_read32(&m, 0x02000010, &v32));
    ASSERT_EQ_HEX(v32, 0x12345678);
    ASSERT_TRUE(game_mem_read16(&m, 0x03007FFE, &v16));
    ASSERT_EQ_HEX(v16, 0xABCD);
    ASSERT_TRUE(game_mem_read8(&m, 0x080001FF, &v8));
    ASSERT_EQ_HEX(v8, 0x42);
}

TEST(game_mem_rejects_out_of_range_and_straddling_reads) {
    GameMem m = make_mem();
    uint32_t v32; uint16_t v16;
    ASSERT_TRUE(!game_mem_read32(&m, 0x00000000, &v32));   /* BIOS: not readable */
    ASSERT_TRUE(!game_mem_read32(&m, 0x0203FFFE, &v32));   /* straddles EWRAM end */
    ASSERT_TRUE(!game_mem_read16(&m, 0x03008000, &v16));   /* past IWRAM */
    ASSERT_TRUE(!game_mem_read16(&m, 0x080001FF, &v16));   /* straddles ROM end */
    ASSERT_TRUE(!game_mem_read16(&m, 0x08000200, &v16));   /* past ROM */
    ASSERT_TRUE(!game_mem_ok(&m, 0x02000000, 0));          /* empty span */
    ASSERT_TRUE(!game_mem_ok(&m, 0x02000000, 0xFFFFFFFF)); /* huge span */
}

TEST(game_mem_null_region_is_unreadable) {
    GameMem m = { NULL, NULL, s_rom, sizeof(s_rom) };
    uint8_t v8;
    ASSERT_TRUE(!game_mem_read8(&m, 0x02000000, &v8));
    ASSERT_TRUE(game_mem_read8(&m, 0x08000000, &v8));
}

TEST(game_mem_in_ewram_bounds) {
    ASSERT_TRUE(game_mem_in_ewram(0x02000000, 0x40000));
    ASSERT_TRUE(!game_mem_in_ewram(0x02000000, 0x40001));
    ASSERT_TRUE(!game_mem_in_ewram(0x03000000, 4));
    ASSERT_TRUE(!game_mem_in_ewram(0x01FFFFFC, 8));
}

TEST(game_strcpy_truncates_and_terminates) {
    char buf[4];
    game_strcpy(buf, sizeof(buf), "ABCDEF");
    ASSERT_STR_EQ(buf, "ABC");
    game_strcpy(buf, sizeof(buf), "");
    ASSERT_STR_EQ(buf, "");
}

static void set_header(const char* code, uint8_t version) {
    memset(s_rom, 0, sizeof(s_rom));
    memcpy(s_rom + 0xAC, code, 4);
    s_rom[0xBC] = version;
}

TEST(crc32_matches_zlib) {
    ASSERT_EQ(game_crc32((const uint8_t*)"123456789", 9), 0xCBF43926u);
    ASSERT_EQ(game_crc32(NULL, 0), 0u);
}

TEST(profile_match_requires_the_exact_rom) {
    set_header("BPEE", 0);
    GameProfile p = g_emerald_profile;
    p.rom_size = sizeof(s_rom);
    p.rom_crc32 = game_crc32(s_rom, sizeof(s_rom));
    ASSERT_EQ(game_profile_match(&p, s_rom, sizeof(s_rom)), GAME_SUPPORT_OK);
    s_rom[0x100] ^= 1;       /* one changed byte: a hack */
    ASSERT_EQ(game_profile_match(&p, s_rom, sizeof(s_rom)), GAME_SUPPORT_MODIFIED);
    s_rom[0x100] ^= 1;
    ASSERT_EQ(game_profile_match(&p, s_rom, sizeof(s_rom) - 1), GAME_SUPPORT_MODIFIED);
    set_header("BPRE", 0);   /* FireRed */
    ASSERT_EQ(game_profile_match(&p, s_rom, sizeof(s_rom)), GAME_SUPPORT_OTHER_GAME);
    ASSERT_EQ(game_profile_match(&p, s_rom, 0xAF), GAME_SUPPORT_OTHER_GAME); /* too short */
    ASSERT_EQ(game_profile_match(&p, NULL, 0), GAME_SUPPORT_OTHER_GAME);
}

TEST(profile_detect_rejects_a_hack_with_the_emerald_header) {
    GameSupport support = GAME_SUPPORT_OK;
    set_header("BPEE", 0);   /* right header, wrong size and contents */
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &support) == NULL);
    ASSERT_EQ(support, GAME_SUPPORT_MODIFIED);
    set_header("BPRE", 0);
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &support) == NULL);
    ASSERT_EQ(support, GAME_SUPPORT_OTHER_GAME);
}

void run_game_mem_tests(void) {
    TEST_SUITE("game_mem");
    RUN_TEST(game_mem_reads_little_endian_from_each_region);
    RUN_TEST(game_mem_rejects_out_of_range_and_straddling_reads);
    RUN_TEST(game_mem_null_region_is_unreadable);
    RUN_TEST(game_mem_in_ewram_bounds);
    RUN_TEST(game_strcpy_truncates_and_terminates);
    RUN_TEST(crc32_matches_zlib);
    RUN_TEST(profile_match_requires_the_exact_rom);
    RUN_TEST(profile_detect_rejects_a_hack_with_the_emerald_header);
}
