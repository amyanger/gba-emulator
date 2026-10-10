#include "test_harness.h"
#include "cheat/cheat_parse.h"

static CheatRawList raw;
static char err[160];

static bool parse(const char* text) {
    err[0] = '\0';
    return cheat_parse_text(text, &raw, err, sizeof(err));
}

TEST(cheat_parse_codebreaker_lines) {
    ASSERT_TRUE(parse("82025838 03E7\n82025840 0063\n"));
    ASSERT_EQ(raw.count, 2);
    ASSERT_EQ_HEX(raw.lines[0].a, 0x82025838);
    ASSERT_EQ_HEX(raw.lines[0].b, 0x03E7);
    ASSERT_EQ(raw.lines[0].digits, 4);
    ASSERT_EQ_HEX(raw.lines[1].a, 0x82025840);
}

TEST(cheat_parse_gameshark_lines_with_label_and_crlf) {
    ASSERT_TRUE(parse("Infinite Money:\r\nD8BAE4D9 4864DCE5\r\nA86CDBA5 19BB3D7A\r\n"));
    ASSERT_EQ(raw.count, 2);
    ASSERT_EQ(raw.lines[0].digits, 8);
    ASSERT_EQ_HEX(raw.lines[1].b, 0x19BB3D7A);
    ASSERT_STR_EQ(raw.name, "Infinite Money");
}

TEST(cheat_parse_dashes_lowercase_and_run_together) {
    ASSERT_TRUE(parse("d8bae4d9-4864dce5\nA86CDBA519BB3D7A\n"));
    ASSERT_EQ(raw.count, 2);
    ASSERT_EQ_HEX(raw.lines[0].a, 0xD8BAE4D9);
    ASSERT_EQ_HEX(raw.lines[1].a, 0xA86CDBA5);
    ASSERT_EQ_HEX(raw.lines[1].b, 0x19BB3D7A);
}

TEST(cheat_parse_label_on_same_line_and_must_be_on) {
    ASSERT_TRUE(parse("Master Code (must be on)\nCode 1: 82025838 03E7\n"));
    ASSERT_EQ(raw.count, 1);
    ASSERT_STR_EQ(raw.name, "Master Code (must be on)");
}

TEST(cheat_parse_hexlike_words_are_labels) {
    ASSERT_TRUE(parse("Feebas Fix\nAdd Face Bead\n82005274 0044\n"));
    ASSERT_EQ(raw.count, 1);
    ASSERT_STR_EQ(raw.name, "Feebas Fix");
}

/* Pokemon money cheats are often titled with a long number. */
TEST(cheat_parse_label_with_long_number_is_a_label) {
    ASSERT_TRUE(parse("Max Money 999999\n82025838 03E7\n"));
    ASSERT_STR_EQ(raw.name, "Max Money 999999");
    ASSERT_TRUE(parse("Have 100000 coins\n82025838 03E7\n"));
    ASSERT_EQ(raw.count, 1);
}

TEST(cheat_parse_malformed_line_rejects_whole_paste) {
    ASSERT_TRUE(!parse("82025838 03E7\n82025838 3E7\n"));
    ASSERT_TRUE(strstr(err, "Line 2") != NULL);
    ASSERT_TRUE(strstr(err, "82025838 3E7") != NULL);
}

TEST(cheat_parse_mixed_shapes_rejected) {
    ASSERT_TRUE(!parse("82025838 03E7\nD8BAE4D9 4864DCE5\n"));
    ASSERT_TRUE(strstr(err, "mix") != NULL || strstr(err, "Mix") != NULL);
}

TEST(cheat_parse_no_codes_rejected) {
    ASSERT_TRUE(!parse("just some words\n"));
    ASSERT_TRUE(strstr(err, "No codes") != NULL);
}

TEST(cheat_parse_too_many_lines_rejected) {
    static char big[CHEAT_MAX_LINES * 20 + 64];
    big[0] = '\0';
    for (int i = 0; i <= CHEAT_MAX_LINES; i++) strcat(big, "82025838 03E7\n");
    ASSERT_TRUE(!parse(big));
    ASSERT_TRUE(strstr(err, "Too many") != NULL);
}

void run_cheat_parse_tests(void) {
    TEST_SUITE("cheat_parse");
    RUN_TEST(cheat_parse_codebreaker_lines);
    RUN_TEST(cheat_parse_gameshark_lines_with_label_and_crlf);
    RUN_TEST(cheat_parse_dashes_lowercase_and_run_together);
    RUN_TEST(cheat_parse_label_on_same_line_and_must_be_on);
    RUN_TEST(cheat_parse_hexlike_words_are_labels);
    RUN_TEST(cheat_parse_label_with_long_number_is_a_label);
    RUN_TEST(cheat_parse_malformed_line_rejects_whole_paste);
    RUN_TEST(cheat_parse_mixed_shapes_rejected);
    RUN_TEST(cheat_parse_no_codes_rejected);
    RUN_TEST(cheat_parse_too_many_lines_rejected);
}
