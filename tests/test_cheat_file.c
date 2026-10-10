#include "test_harness.h"
#include "cheat/cheat_crypto.h"
#include "cheat/cheat_file.h"

#include <stdio.h>

static CheatEngine eng;
static uint32_t skipped;

static int32_t load(const char* content) {
    const char* path = "gba_cheat_file_test.cht";
    FILE* f = fopen(path, "w");
    if (!f) return -100;
    fputs(content, f);
    fclose(f);
    cheat_init(&eng);
    cheat_set_game_code(&eng, 0x45455042u);
    int32_t n = cheat_file_load(&eng, path, &skipped);
    remove(path);
    return n;
}

TEST(cheat_file_load_documented_bare_name_format) {
    ASSERT_EQ(load("# comment\n[CodeBreaker]\nInfinite Rare Candies\n82005274 0044\n82005276 0063\n"), 1);
    ASSERT_STR_EQ(eng.cheats[0].name, "Infinite Rare Candies");
    ASSERT_EQ(eng.cheats[0].line_count, 2);
    ASSERT_EQ(eng.cheats[0].device, CHEAT_DEV_CODEBREAKER);
    ASSERT_TRUE(eng.cheats[0].enabled);
    ASSERT_TRUE(!eng.cheats[0].broken);
}

TEST(cheat_file_load_hexlike_name_not_eaten_as_code) {
    ASSERT_EQ(load("[CodeBreaker]\nFeebas Fix\n82005274 0044\n"), 1);
    ASSERT_STR_EQ(eng.cheats[0].name, "Feebas Fix");
    ASSERT_EQ(eng.cheats[0].line_count, 1);
}

TEST(cheat_file_load_name_eq_and_enabled_false) {
    ASSERT_EQ(load("[CodeBreaker]\nname=Walk Through Walls\nenabled=false\n82005274 0044\n"), 1);
    ASSERT_STR_EQ(eng.cheats[0].name, "Walk Through Walls");
    ASSERT_TRUE(!eng.cheats[0].enabled);
}

TEST(cheat_file_device_override_kept) {
    ASSERT_EQ(load("[ActionReplayRaw]\nRaw\nenabled=true\ndevice=override\n02224ABC 0000270F\n"), 1);
    ASSERT_EQ(eng.cheats[0].device, CHEAT_DEV_PAR3_RAW);
    ASSERT_TRUE(eng.cheats[0].device_override);
}

TEST(cheat_file_legacy_block_is_auto_detected) {
    uint32_t a = 0x1202BC24, b = 0x0000FFFF;
    cheat_tea_encrypt(&a, &b, CHEAT_SEEDS_GSA);
    char text[128];
    snprintf(text, sizeof(text), "[GameShark]\nOld\n%08X %08X\n", (unsigned)a, (unsigned)b);
    ASSERT_EQ(load(text), 1);
    ASSERT_EQ(eng.cheats[0].device, CHEAT_DEV_GSA);
    ASSERT_TRUE(!eng.cheats[0].broken);
}

TEST(cheat_file_undecodable_block_kept_broken) {
    ASSERT_EQ(load("[CodeBreaker]\nBad\ndevice=detected\n88000000 0001\n"), 1);
    ASSERT_TRUE(eng.cheats[0].broken);
    ASSERT_TRUE(eng.cheats[0].error[0] != '\0');
    ASSERT_EQ(eng.cheats[0].line_count, 1);
}

/* Without device= and without a confident detection, never run on a guess. */
TEST(cheat_file_unsure_legacy_block_kept_broken) {
    ASSERT_EQ(load("[GameShark]\nUnsure\nFFFFFFFF FFFFFFFF\n"), 1);
    ASSERT_TRUE(eng.cheats[0].broken);
    ASSERT_TRUE(!cheat_any_enabled(&eng));
}

TEST(cheat_file_malformed_block_skipped_and_counted) {
    ASSERT_EQ(load("[CodeBreaker]\nTypo\n82025838 3E7\n[CodeBreaker]\nGood\n82025838 03E7\n"), 1);
    ASSERT_EQ(skipped, 1);
    ASSERT_STR_EQ(eng.cheats[0].name, "Good");
}

TEST(cheat_file_missing_file_returns_minus_one) {
    cheat_init(&eng);
    ASSERT_EQ(cheat_file_load(&eng, "no_such_dir_cheat/none.cht", &skipped), -1);
}

TEST(cheat_file_save_load_round_trip) {
    static CheatEngine src;
    char err[128];
    cheat_init(&src);
    cheat_set_game_code(&src, 0x45455042u);
    ASSERT_EQ(cheat_add_text(&src, "Money\n82025838 03E7\n", NULL, CHEAT_DEV_AUTO, err, sizeof(err)), 0);
    ASSERT_EQ(cheat_add_text(&src, "02224ABC 0000270F\n", "Raw AR", CHEAT_DEV_PAR3_RAW, err, sizeof(err)), 1);
    ASSERT_TRUE(cheat_set_device(&src, 1, CHEAT_DEV_PAR3_RAW));
    cheat_toggle(&src, 0);
    const char* path = "gba_cheat_round_trip.cht";
    ASSERT_TRUE(cheat_file_save(&src, path));
    cheat_init(&eng);
    cheat_set_game_code(&eng, 0x45455042u);
    ASSERT_EQ(cheat_file_load(&eng, path, &skipped), 2);
    remove(path);
    ASSERT_STR_EQ(eng.cheats[0].name, "Money");
    ASSERT_TRUE(!eng.cheats[0].enabled);
    ASSERT_EQ(eng.cheats[0].device, CHEAT_DEV_CODEBREAKER);
    ASSERT_EQ_HEX(eng.cheats[0].lines[0].b, 0x03E7);
    ASSERT_EQ(eng.cheats[0].lines[0].digits, 4);
    ASSERT_STR_EQ(eng.cheats[1].name, "Raw AR");
    ASSERT_EQ(eng.cheats[1].device, CHEAT_DEV_PAR3_RAW);
    ASSERT_TRUE(eng.cheats[1].device_override);
    ASSERT_TRUE(eng.cheats[1].enabled);
}

TEST(cheat_file_save_is_atomic_over_existing) {
    static CheatEngine src;
    char err[128];
    cheat_init(&src);
    cheat_add_text(&src, "82025838 03E7\n", "A", CHEAT_DEV_AUTO, err, sizeof(err));
    const char* path = "gba_cheat_atomic.cht";
    ASSERT_TRUE(cheat_file_save(&src, path));
    cheat_add_text(&src, "82025840 0001\n", "B", CHEAT_DEV_AUTO, err, sizeof(err));
    ASSERT_TRUE(cheat_file_save(&src, path));
    FILE* f = fopen("gba_cheat_atomic.cht.tmp", "r");
    ASSERT_TRUE(f == NULL);   /* temp file renamed away */
    cheat_init(&eng);
    ASSERT_EQ(cheat_file_load(&eng, path, &skipped), 2);
    remove(path);
}

void run_cheat_file_tests(void) {
    TEST_SUITE("cheat_file");
    RUN_TEST(cheat_file_load_documented_bare_name_format);
    RUN_TEST(cheat_file_load_hexlike_name_not_eaten_as_code);
    RUN_TEST(cheat_file_load_name_eq_and_enabled_false);
    RUN_TEST(cheat_file_device_override_kept);
    RUN_TEST(cheat_file_legacy_block_is_auto_detected);
    RUN_TEST(cheat_file_undecodable_block_kept_broken);
    RUN_TEST(cheat_file_unsure_legacy_block_kept_broken);
    RUN_TEST(cheat_file_malformed_block_skipped_and_counted);
    RUN_TEST(cheat_file_missing_file_returns_minus_one);
    RUN_TEST(cheat_file_save_load_round_trip);
    RUN_TEST(cheat_file_save_is_atomic_over_existing);
}
