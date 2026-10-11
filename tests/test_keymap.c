#include "test_harness.h"

#include <stdio.h>
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

TEST(keymap_defaults_pad_and_turbo) {
    keymap_reset_defaults();
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_A), KEY_A);
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_BACK), KEY_SELECT);
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_X), 0);     /* no longer a duplicate A */
    ASSERT_EQ(keymap_pad_lookup(KEYMAP_PAD_LT), 0);
    ASSERT_EQ(keymap_key_for(KEYMAP_TURBO_A), SDL_SCANCODE_UNKNOWN);
    ASSERT_EQ(keymap_button_from_name("turbo_b"), KEYMAP_TURBO_B);
}

TEST(keymap_bind_moves_on_conflict) {
    keymap_reset_defaults();
    keymap_bind_key(KEY_B, SDL_SCANCODE_Z);           /* Z was A */
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_Z), KEY_B);
    ASSERT_EQ(keymap_key_for(KEY_A), SDL_SCANCODE_UNKNOWN);
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_X), 0);       /* B's old key is freed */
    keymap_bind_pad(KEY_L, KEYMAP_PAD_LT);
    ASSERT_EQ(keymap_pad_lookup(KEYMAP_PAD_LT), KEY_L);
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), 0);
    keymap_unbind(KEY_L, true);
    ASSERT_EQ(keymap_pad_for(KEY_L), KEYMAP_PAD_NONE);
}

TEST(keymap_save_load_round_trip) {
    keymap_reset_defaults();
    keymap_bind_key(KEYMAP_TURBO_A, SDL_SCANCODE_C);
    keymap_bind_pad(KEYMAP_TURBO_B, SDL_CONTROLLER_BUTTON_Y);
    keymap_bind_pad(KEY_R, KEYMAP_PAD_RT);
    ASSERT_TRUE(keymap_save("gba_keymap_test.ini"));
    keymap_reset_defaults();
    ASSERT_TRUE(keymap_load("gba_keymap_test.ini"));
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_C), KEYMAP_TURBO_A);
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_Y), KEYMAP_TURBO_B);
    ASSERT_EQ(keymap_pad_lookup(KEYMAP_PAD_RT), KEY_R);
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_Z), KEY_A);
    remove("gba_keymap_test.ini");
}

TEST(keymap_load_skips_bad_lines) {
    FILE* f = fopen("gba_keymap_bad.ini", "w");
    ASSERT_TRUE(f != NULL);
    fputs("A=Q\nNOPE=Z\nB=NotAKey\nPAD_A=notapad\nPAD_B=x\n=\nSTART\n", f);
    fclose(f);
    keymap_reset_defaults();
    ASSERT_TRUE(keymap_load("gba_keymap_bad.ini"));
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_Q), KEY_A);
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_X), KEY_B);              /* bad B line left the default */
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_A), KEY_A); /* bad PAD_A left the default */
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_X), KEY_B);
    remove("gba_keymap_bad.ini");
}

TEST(keymap_pad_names) {
    ASSERT_EQ(keymap_pad_from_name("lefttrigger"), KEYMAP_PAD_LT);
    ASSERT_EQ(keymap_pad_from_name("a"), SDL_CONTROLLER_BUTTON_A);
    ASSERT_EQ(keymap_pad_from_name("bogus"), KEYMAP_PAD_NONE);
    ASSERT_STR_EQ(keymap_pad_name(KEYMAP_PAD_RT), "righttrigger");
}

TEST(keymap_unbind_persists) {
    keymap_reset_defaults();
    keymap_unbind(KEY_R, false);
    keymap_unbind(KEY_A, true);
    ASSERT_TRUE(keymap_save("gba_keymap_unbind.ini"));
    keymap_reset_defaults();
    ASSERT_TRUE(keymap_load("gba_keymap_unbind.ini"));
    ASSERT_EQ(keymap_key_for(KEY_R), SDL_SCANCODE_UNKNOWN);
    ASSERT_EQ(keymap_pad_for(KEY_A), KEYMAP_PAD_NONE);
    ASSERT_EQ(keymap_lookup(SDL_SCANCODE_Z), KEY_A);
    ASSERT_EQ(keymap_pad_lookup(SDL_CONTROLLER_BUTTON_B), KEY_B);
    remove("gba_keymap_unbind.ini");
}

TEST(keymap_key_bindable_checks) {
    ASSERT_TRUE(keymap_key_bindable(SDL_SCANCODE_Z));
    ASSERT_TRUE(!keymap_key_bindable(SDL_SCANCODE_KP_HASH));
    ASSERT_TRUE(!keymap_key_bindable(SDL_SCANCODE_UNKNOWN));
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
    RUN_TEST(keymap_defaults_pad_and_turbo);
    RUN_TEST(keymap_bind_moves_on_conflict);
    RUN_TEST(keymap_save_load_round_trip);
    RUN_TEST(keymap_load_skips_bad_lines);
    RUN_TEST(keymap_pad_names);
    RUN_TEST(keymap_unbind_persists);
    RUN_TEST(keymap_key_bindable_checks);
}
