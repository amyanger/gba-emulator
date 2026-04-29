#include "test_harness.h"
#include "cheat/cheat.h"
#include <string.h>

TEST(cheat_init_zeros_engine) {
    CheatEngine eng;
    /* Pre-poison so init must clear. */
    memset(&eng, 0xFF, sizeof(eng));

    cheat_init(&eng);
    ASSERT_EQ(eng.cheat_count, 0);
}

TEST(cheat_add_returns_index_and_appends) {
    CheatEngine eng;
    cheat_init(&eng);

    CheatCode code = {0x12345678, 0x9ABCDEF0};
    int32_t idx = cheat_add(&eng, "Test cheat", CHEAT_FORMAT_GAMESHARK, &code, 1);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(eng.cheat_count, 1);
    ASSERT_STR_EQ(eng.cheats[0].name, "Test cheat");
    ASSERT_EQ(eng.cheats[0].code_count, 1);
    ASSERT_EQ_HEX(eng.cheats[0].codes[0].address, 0x12345678);
    ASSERT_EQ_HEX(eng.cheats[0].codes[0].value, 0x9ABCDEF0);
    /* New cheats start enabled by default — matches cheat-file load. */
    ASSERT_EQ(eng.cheats[0].enabled, true);

    /* Adding another cheat appends, doesn't overwrite. */
    int32_t idx2 = cheat_add(&eng, "Cheat 2", CHEAT_FORMAT_CODEBREAKER, &code, 1);
    ASSERT_EQ(idx2, 1);
    ASSERT_EQ(eng.cheat_count, 2);
}

TEST(cheat_add_returns_minus_one_when_full) {
    CheatEngine eng;
    cheat_init(&eng);
    CheatCode code = {0, 0};

    /* Fill the table. */
    for (int i = 0; i < CHEAT_MAX_CHEATS; i++) {
        ASSERT_EQ(cheat_add(&eng, "x", CHEAT_FORMAT_GAMESHARK, &code, 1), i);
    }
    /* One more must fail. */
    ASSERT_EQ(cheat_add(&eng, "overflow", CHEAT_FORMAT_GAMESHARK, &code, 1), -1);
    ASSERT_EQ(eng.cheat_count, CHEAT_MAX_CHEATS);
}

TEST(cheat_toggle_flips_enabled) {
    CheatEngine eng;
    cheat_init(&eng);
    CheatCode code = {0, 0};
    cheat_add(&eng, "x", CHEAT_FORMAT_GAMESHARK, &code, 1);

    ASSERT_EQ(eng.cheats[0].enabled, true);
    cheat_toggle(&eng, 0);
    ASSERT_EQ(eng.cheats[0].enabled, false);
    cheat_toggle(&eng, 0);
    ASSERT_EQ(eng.cheats[0].enabled, true);
}

TEST(cheat_remove_shifts_subsequent_entries_down) {
    CheatEngine eng;
    cheat_init(&eng);
    CheatCode c0 = {0xAAA, 0};
    CheatCode c1 = {0xBBB, 0};
    CheatCode c2 = {0xCCC, 0};
    cheat_add(&eng, "A", CHEAT_FORMAT_GAMESHARK, &c0, 1);
    cheat_add(&eng, "B", CHEAT_FORMAT_GAMESHARK, &c1, 1);
    cheat_add(&eng, "C", CHEAT_FORMAT_GAMESHARK, &c2, 1);

    cheat_remove(&eng, 1); /* drop "B" */

    ASSERT_EQ(eng.cheat_count, 2);
    ASSERT_STR_EQ(eng.cheats[0].name, "A");
    ASSERT_STR_EQ(eng.cheats[1].name, "C");
}

TEST(cheat_clear_all_empties_engine) {
    CheatEngine eng;
    cheat_init(&eng);
    CheatCode code = {0, 0};
    cheat_add(&eng, "x", CHEAT_FORMAT_GAMESHARK, &code, 1);
    cheat_add(&eng, "y", CHEAT_FORMAT_GAMESHARK, &code, 1);

    cheat_clear_all(&eng);
    ASSERT_EQ(eng.cheat_count, 0);
}

void run_cheat_tests(void) {
    TEST_SUITE("cheat");
    RUN_TEST(cheat_init_zeros_engine);
    RUN_TEST(cheat_add_returns_index_and_appends);
    RUN_TEST(cheat_add_returns_minus_one_when_full);
    RUN_TEST(cheat_toggle_flips_enabled);
    RUN_TEST(cheat_remove_shifts_subsequent_entries_down);
    RUN_TEST(cheat_clear_all_empties_engine);
}
