#include "test_harness.h"

#include <string.h>

#include "input/input.h"
#include "input/keymap.h"

TEST(keymap_button_from_name_known_buttons) {
    ASSERT_EQ(keymap_button_from_name("A"),      (uint16_t)KEY_A);
    ASSERT_EQ(keymap_button_from_name("B"),      (uint16_t)KEY_B);
    ASSERT_EQ(keymap_button_from_name("START"),  (uint16_t)KEY_START);
    ASSERT_EQ(keymap_button_from_name("SELECT"), (uint16_t)KEY_SELECT);
    ASSERT_EQ(keymap_button_from_name("UP"),     (uint16_t)KEY_UP);
    ASSERT_EQ(keymap_button_from_name("DOWN"),   (uint16_t)KEY_DOWN);
    ASSERT_EQ(keymap_button_from_name("LEFT"),   (uint16_t)KEY_LEFT);
    ASSERT_EQ(keymap_button_from_name("RIGHT"),  (uint16_t)KEY_RIGHT);
    ASSERT_EQ(keymap_button_from_name("L"),      (uint16_t)KEY_L);
    ASSERT_EQ(keymap_button_from_name("R"),      (uint16_t)KEY_R);
}

TEST(keymap_button_from_name_case_insensitive) {
    ASSERT_EQ(keymap_button_from_name("start"), (uint16_t)KEY_START);
    ASSERT_EQ(keymap_button_from_name("Start"), (uint16_t)KEY_START);
    ASSERT_EQ(keymap_button_from_name("sTaRt"), (uint16_t)KEY_START);
}

TEST(keymap_button_from_name_unknown_returns_zero) {
    ASSERT_EQ(keymap_button_from_name("FOO"), (uint16_t)0);
    ASSERT_EQ(keymap_button_from_name(""),    (uint16_t)0);
    ASSERT_EQ(keymap_button_from_name(NULL),  (uint16_t)0);
}

TEST(keymap_parse_line_basic) {
    char line[] = "A=Z";
    char* button = NULL;
    char* key = NULL;
    ASSERT_TRUE(keymap_parse_line(line, &button, &key));
    ASSERT_STR_EQ(button, "A");
    ASSERT_STR_EQ(key, "Z");
}

TEST(keymap_parse_line_strips_whitespace) {
    char line[] = "  START   =   Return  ";
    char* button = NULL;
    char* key = NULL;
    ASSERT_TRUE(keymap_parse_line(line, &button, &key));
    ASSERT_STR_EQ(button, "START");
    ASSERT_STR_EQ(key, "Return");
}

TEST(keymap_parse_line_strips_trailing_comment) {
    char line[] = "L=A  # left shoulder";
    char* button = NULL;
    char* key = NULL;
    ASSERT_TRUE(keymap_parse_line(line, &button, &key));
    ASSERT_STR_EQ(button, "L");
    ASSERT_STR_EQ(key, "A");
}

TEST(keymap_parse_line_blank_returns_false) {
    char a[] = "";
    char b[] = "   ";
    char c[] = "# only a comment";
    char d[] = "no_equals_sign";
    char* button = NULL;
    char* key = NULL;
    ASSERT_TRUE(!keymap_parse_line(a, &button, &key));
    ASSERT_TRUE(!keymap_parse_line(b, &button, &key));
    ASSERT_TRUE(!keymap_parse_line(c, &button, &key));
    ASSERT_TRUE(!keymap_parse_line(d, &button, &key));
}

TEST(keymap_parse_line_empty_lhs_or_rhs_returns_false) {
    char a[] = "=Z";
    char b[] = "A=";
    char c[] = "  =  ";
    char* button = NULL;
    char* key = NULL;
    ASSERT_TRUE(!keymap_parse_line(a, &button, &key));
    ASSERT_TRUE(!keymap_parse_line(b, &button, &key));
    ASSERT_TRUE(!keymap_parse_line(c, &button, &key));
}

void run_keymap_tests(void) {
    TEST_SUITE("keymap");
    RUN_TEST(keymap_button_from_name_known_buttons);
    RUN_TEST(keymap_button_from_name_case_insensitive);
    RUN_TEST(keymap_button_from_name_unknown_returns_zero);
    RUN_TEST(keymap_parse_line_basic);
    RUN_TEST(keymap_parse_line_strips_whitespace);
    RUN_TEST(keymap_parse_line_strips_trailing_comment);
    RUN_TEST(keymap_parse_line_blank_returns_false);
    RUN_TEST(keymap_parse_line_empty_lhs_or_rhs_returns_false);
}
