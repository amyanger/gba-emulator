#include "test_harness.h"
#include "headless/input_script.h"

/* Tests write a temp script to the current directory (tests already run
   from a scratch CWD because other suites write saves there). */
#define TMP_SCRIPT "test_input_script.tmp"

static InputScript s_script;

TEST(input_script_parses_valid_lines) {
    input_script_init(&s_script);
    ASSERT_TRUE(input_script_parse_line(&s_script, "0 press A\n", 1));
    ASSERT_TRUE(input_script_parse_line(&s_script, "12\trelease   START", 2));
    ASSERT_TRUE(input_script_parse_line(&s_script, "12 press L # trailing comment", 3));
    ASSERT_EQ(s_script.count, 3);
    ASSERT_EQ(s_script.events[0].frame, 0);
    ASSERT_EQ(s_script.events[0].key, KEY_A);
    ASSERT_TRUE(s_script.events[0].press);
    ASSERT_EQ(s_script.events[1].frame, 12);
    ASSERT_EQ(s_script.events[1].key, KEY_START);
    ASSERT_TRUE(!s_script.events[1].press);
    ASSERT_EQ(s_script.events[2].key, KEY_L);
}

TEST(input_script_ignores_comments_and_blank_lines) {
    input_script_init(&s_script);
    ASSERT_TRUE(input_script_parse_line(&s_script, "# header comment\n", 1));
    ASSERT_TRUE(input_script_parse_line(&s_script, "\n", 2));
    ASSERT_TRUE(input_script_parse_line(&s_script, "   \t \r\n", 3));
    ASSERT_TRUE(input_script_parse_line(&s_script, "", 4));
    ASSERT_EQ(s_script.count, 0);
}

TEST(input_script_rejects_bad_key) {
    input_script_init(&s_script);
    ASSERT_TRUE(!input_script_parse_line(&s_script, "5 press X", 1));
    ASSERT_TRUE(!input_script_parse_line(&s_script, "5 press a", 1));  // case-sensitive
    ASSERT_EQ(s_script.count, 0);
}

TEST(input_script_rejects_bad_frame) {
    input_script_init(&s_script);
    ASSERT_TRUE(!input_script_parse_line(&s_script, "-1 press A", 1));
    ASSERT_TRUE(!input_script_parse_line(&s_script, "+3 press A", 1));
    ASSERT_TRUE(!input_script_parse_line(&s_script, "1x press A", 1));
    ASSERT_TRUE(!input_script_parse_line(&s_script, "99999999999 press A", 1));
    ASSERT_EQ(s_script.count, 0);
}

TEST(input_script_rejects_bad_shape) {
    input_script_init(&s_script);
    ASSERT_TRUE(!input_script_parse_line(&s_script, "5 hold A", 1));
    ASSERT_TRUE(!input_script_parse_line(&s_script, "5 press", 1));
    ASSERT_TRUE(!input_script_parse_line(&s_script, "5 press A B", 1));
    ASSERT_EQ(s_script.count, 0);
}

TEST(input_script_requires_ascending_frames) {
    input_script_init(&s_script);
    ASSERT_TRUE(input_script_parse_line(&s_script, "10 press A", 1));
    ASSERT_TRUE(input_script_parse_line(&s_script, "10 press B", 2));  // equal is fine
    ASSERT_TRUE(!input_script_parse_line(&s_script, "9 release A", 3));
    ASSERT_EQ(s_script.count, 2);
}

TEST(input_script_applies_events_before_their_frame) {
    InputState in;
    input_init(&in);
    input_script_init(&s_script);
    ASSERT_TRUE(input_script_parse_line(&s_script, "2 press A", 1));
    ASSERT_TRUE(input_script_parse_line(&s_script, "2 press UP", 2));
    ASSERT_TRUE(input_script_parse_line(&s_script, "4 release A", 3));

    input_script_apply(&s_script, 1, &in);
    ASSERT_EQ_HEX(in.keyinput, 0x03FF);
    input_script_apply(&s_script, 2, &in);
    ASSERT_EQ_HEX(in.keyinput, 0x03FF & ~(KEY_A | KEY_UP));
    input_script_apply(&s_script, 3, &in);
    ASSERT_EQ_HEX(in.keyinput, 0x03FF & ~(KEY_A | KEY_UP));
    input_script_apply(&s_script, 4, &in);
    ASSERT_EQ_HEX(in.keyinput, 0x03FF & ~KEY_UP);
}

TEST(input_script_load_reports_line_errors) {
    FILE* f = fopen(TMP_SCRIPT, "w");
    ASSERT_TRUE(f != NULL);
    fputs("# ok\n0 press A\n3 release A\n2 press B\n", f);
    fclose(f);
    ASSERT_TRUE(!input_script_load(&s_script, TMP_SCRIPT));

    f = fopen(TMP_SCRIPT, "w");
    ASSERT_TRUE(f != NULL);
    fputs("# ok\n0 press A\n\n3 release A", f);  // no trailing newline
    fclose(f);
    ASSERT_TRUE(input_script_load(&s_script, TMP_SCRIPT));
    ASSERT_EQ(s_script.count, 2);
    remove(TMP_SCRIPT);

    ASSERT_TRUE(!input_script_load(&s_script, "does_not_exist.input"));
}

void run_input_script_tests(void) {
    TEST_SUITE("input_script");
    RUN_TEST(input_script_parses_valid_lines);
    RUN_TEST(input_script_ignores_comments_and_blank_lines);
    RUN_TEST(input_script_rejects_bad_key);
    RUN_TEST(input_script_rejects_bad_frame);
    RUN_TEST(input_script_rejects_bad_shape);
    RUN_TEST(input_script_requires_ascending_frames);
    RUN_TEST(input_script_applies_events_before_their_frame);
    RUN_TEST(input_script_load_reports_line_errors);
}
