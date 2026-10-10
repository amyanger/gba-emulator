#include "test_harness.h"
#include "game/gen3_text.h"

TEST(gen3_decodes_uppercase_name_with_terminator) {
    const uint8_t bulbasaur[] = {0xBC, 0xCF, 0xC6, 0xBC, 0xBB, 0xCD, 0xBB, 0xCF, 0xCC, 0xFF, 0x00};
    char out[16];
    ASSERT_EQ(gen3_decode(bulbasaur, sizeof(bulbasaur), out, sizeof(out)), 9);
    ASSERT_STR_EQ(out, "BULBASAUR");
}

TEST(gen3_decodes_mixed_case_space_and_punctuation) {
    const uint8_t mr_mime[] = {0xC7, 0xE6, 0xAD, 0x00, 0xC7, 0xDD, 0xE1, 0xD9, 0xFF};
    char out[16];
    gen3_decode(mr_mime, sizeof(mr_mime), out, sizeof(out));
    ASSERT_STR_EQ(out, "Mr. Mime");
}

TEST(gen3_full_length_name_has_no_terminator) {
    const uint8_t ten[] = {0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4};
    char out[16];
    gen3_decode(ten, sizeof(ten), out, sizeof(out));
    ASSERT_STR_EQ(out, "ABCDEFGHIJ");
}

TEST(gen3_maps_digits_symbols_and_unknowns) {
    const uint8_t s[] = {0xA1, 0xAA, 0xB5, 0xB6, 0x53, 0x54, 0x1B, 0x01, 0xFF};
    char out[16];
    gen3_decode(s, sizeof(s), out, sizeof(out));
    ASSERT_STR_EQ(out, "09MFPkMne?");
}

TEST(gen3_truncates_to_output_size) {
    const uint8_t bulbasaur[] = {0xBC, 0xCF, 0xC6, 0xBC, 0xFF};
    char out[4];
    ASSERT_EQ(gen3_decode(bulbasaur, sizeof(bulbasaur), out, sizeof(out)), 3);
    ASSERT_STR_EQ(out, "BUL");
}

void run_game_text_tests(void) {
    TEST_SUITE("game_text");
    RUN_TEST(gen3_decodes_uppercase_name_with_terminator);
    RUN_TEST(gen3_decodes_mixed_case_space_and_punctuation);
    RUN_TEST(gen3_full_length_name_has_no_terminator);
    RUN_TEST(gen3_maps_digits_symbols_and_unknowns);
    RUN_TEST(gen3_truncates_to_output_size);
}
