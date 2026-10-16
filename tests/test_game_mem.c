#include "test_harness.h"
#include "game/game_mem.h"
#include "game/game_profile.h"
#include "game/runlog_read.h"
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
    set_header("BPRE", 0);   /* FireRed code and revision, other contents */
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &support) == NULL);
    ASSERT_EQ(support, GAME_SUPPORT_MODIFIED);
    set_header("AXVE", 0);   /* Ruby */
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &support) == NULL);
    ASSERT_EQ(support, GAME_SUPPORT_OTHER_GAME);
}

TEST(profile_match_checks_the_version_byte) {
    set_header("BPEE", 1);   /* Emerald header, other revision byte */
    GameProfile p = g_emerald_profile;
    p.rom_size = sizeof(s_rom);
    p.rom_crc32 = game_crc32(s_rom, sizeof(s_rom));
    ASSERT_EQ(game_profile_match(&p, s_rom, sizeof(s_rom)), GAME_SUPPORT_OTHER_REVISION);
    ASSERT_EQ(game_profile_match(&p, s_rom, 0xBC), GAME_SUPPORT_OTHER_GAME); /* no version byte */
}

/* Detection picks the profile by game code first, then checks version and CRC once. */
TEST(profile_detect_in_picks_by_code_then_version) {
    static GameProfile a, b;
    a = g_emerald_profile;
    memcpy(a.game_code, "AAAA", 4);
    a.version = 0;
    b = g_emerald_profile;
    memcpy(b.game_code, "BBBB", 4);
    b.version = 1;
    set_header("BBBB", 1);
    b.rom_size = sizeof(s_rom);
    b.rom_crc32 = game_crc32(s_rom, sizeof(s_rom));
    const GameProfile* list[2] = {&a, &b};
    GameSupport s = GAME_SUPPORT_OK;
    ASSERT_TRUE(game_profile_detect_in(list, 2, s_rom, sizeof(s_rom), &s) == &b);
    ASSERT_EQ(s, GAME_SUPPORT_OK);
    set_header("BBBB", 0);
    ASSERT_TRUE(game_profile_detect_in(list, 2, s_rom, sizeof(s_rom), &s) == NULL);
    ASSERT_EQ(s, GAME_SUPPORT_OTHER_REVISION);
    set_header("AAAA", 0);   /* a still has Emerald's size and CRC */
    ASSERT_TRUE(game_profile_detect_in(list, 2, s_rom, sizeof(s_rom), &s) == NULL);
    ASSERT_EQ(s, GAME_SUPPORT_MODIFIED);
    set_header("CCCC", 0);
    ASSERT_TRUE(game_profile_detect_in(list, 2, s_rom, sizeof(s_rom), &s) == NULL);
    ASSERT_EQ(s, GAME_SUPPORT_OTHER_GAME);
}

TEST(emerald_profile_has_rules_and_identity) {
    ASSERT_TRUE(g_emerald_profile.rules == &g_emerald_rules);
    ASSERT_EQ(g_emerald_profile.version, 0);
    ASSERT_STR_EQ(g_emerald_profile.game_name, "Pok\xC3\xA9mon Emerald");
    ASSERT_TRUE(game_profile_find("BPEE") == &g_emerald_profile);
    ASSERT_TRUE(game_profile_find("BPRE") == &g_firered_profile);
    ASSERT_TRUE(game_profile_find("BPGE") == &g_leafgreen_profile);
    ASSERT_TRUE(game_profile_find("AXVE") == NULL);
    ASSERT_EQ(g_emerald_rules.features, FEAT_BERRIES | FEAT_HUNT | FEAT_MIRAGE | FEAT_FEEBAS |
                                            FEAT_OVERWORLD | FEAT_FOLLOWER);
    ASSERT_EQ(g_emerald_rules.story_count, 12);
    ASSERT_EQ(g_emerald_rules.story_hof, 10);
    ASSERT_TRUE(game_has(&g_emerald_profile, FEAT_HUNT));
    ASSERT_TRUE(!game_has(NULL, FEAT_HUNT));
}

TEST(frlg_revisions_and_shared_rules) {
    GameSupport s = GAME_SUPPORT_OK;
    set_header("BPRE", 1);   /* FireRed 1.1 */
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &s) == NULL);
    ASSERT_EQ(s, GAME_SUPPORT_OTHER_REVISION);
    set_header("BPGE", 0);   /* LeafGreen 1.0 */
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &s) == NULL);
    ASSERT_EQ(s, GAME_SUPPORT_OTHER_REVISION);
    set_header("BPGE", 1);
    ASSERT_TRUE(game_profile_detect(s_rom, sizeof(s_rom), &s) == NULL);
    ASSERT_EQ(s, GAME_SUPPORT_MODIFIED);
    ASSERT_TRUE(g_firered_profile.rules == &g_frlg_rules);
    ASSERT_TRUE(g_leafgreen_profile.rules == &g_frlg_rules);
    ASSERT_EQ(g_firered_profile.version, 0);
    ASSERT_EQ(g_leafgreen_profile.version, 1);
    ASSERT_EQ_HEX(g_firered_profile.rom_crc32, 0xDD88761Cu);
    ASSERT_EQ_HEX(g_leafgreen_profile.rom_crc32, 0xDAFFECECu);
    ASSERT_EQ_HEX(g_firered_profile.save_block1_ptr, g_leafgreen_profile.save_block1_ptr);
    ASSERT_EQ_HEX(g_firered_profile.player_party, 0x02024284u);
    ASSERT_EQ(g_frlg_rules.features, 0);
    ASSERT_EQ(g_frlg_rules.story[g_frlg_rules.story_hof].id, 0x82C);
    ASSERT_EQ(g_firered_profile.low_tide_mapsec, 0xFF);
    ASSERT_EQ(g_firered_profile.wild_feebas, 0);
}

TEST(every_profile_fits_the_readers) {
    for (uint8_t i = 0; i < g_game_profile_count; i++) {
        const GameRules* r = g_game_profiles[i]->rules;
        ASSERT_TRUE(r != NULL);
        ASSERT_TRUE(g_game_profiles[i]->game_name != NULL);
        ASSERT_TRUE(r->story_count <= RUN_STORY_COUNT);
        ASSERT_TRUE(r->story_hof < r->story_count);
        ASSERT_TRUE(r->sb1_flag_bytes <= 300);
        ASSERT_TRUE(r->sb2_encryption_key + 4u <= 0xF24);
        ASSERT_TRUE(r->league_count <= 16);
        ASSERT_TRUE(r->daycare_offspring_size == 2 || r->daycare_offspring_size == 4);
        for (int b = 0; b < 8; b++) ASSERT_TRUE(r->badge_names[b] != NULL);
    }
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
    RUN_TEST(profile_match_checks_the_version_byte);
    RUN_TEST(profile_detect_in_picks_by_code_then_version);
    RUN_TEST(emerald_profile_has_rules_and_identity);
    RUN_TEST(frlg_revisions_and_shared_rules);
    RUN_TEST(every_profile_fits_the_readers);
}
