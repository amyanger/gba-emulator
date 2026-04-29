#include "test_harness.h"
#include "input/input.h"

/* GBA keypad is active-LOW: KEYINPUT bit = 0 means pressed,
 * bit = 1 means released. Initial state has all 10 button bits set
 * (released) — anything else would have the game thinking buttons
 * are stuck on boot. */

TEST(input_init_releases_all_keys) {
    InputState in;
    /* Pre-poison so init must clear/set correctly. */
    in.keyinput = 0;
    in.keycnt = 0xBEEF;

    input_init(&in);
    /* All 10 button bits set; bits 10-15 zero. */
    ASSERT_EQ_HEX(in.keyinput, 0x03FF);
    ASSERT_EQ(in.keycnt, 0);
}

TEST(input_press_clears_active_low_bit) {
    InputState in;
    input_init(&in);

    input_press(&in, KEY_A);
    ASSERT_EQ(in.keyinput & KEY_A, 0); /* pressed → bit 0 */
    /* Other buttons untouched. */
    ASSERT_EQ(in.keyinput & KEY_B, KEY_B);
    ASSERT_EQ(in.keyinput & KEY_START, KEY_START);
}

TEST(input_release_sets_active_low_bit) {
    InputState in;
    input_init(&in);

    input_press(&in, KEY_LEFT);
    input_press(&in, KEY_RIGHT);
    /* Both directions held → both bits clear. */
    ASSERT_EQ(in.keyinput & (KEY_LEFT | KEY_RIGHT), 0);

    input_release(&in, KEY_LEFT);
    ASSERT_EQ(in.keyinput & KEY_LEFT, KEY_LEFT);
    /* Right still held. */
    ASSERT_EQ(in.keyinput & KEY_RIGHT, 0);
}

TEST(input_press_and_release_idempotent) {
    /* Pressing an already-pressed key keeps it pressed. Releasing
     * an already-released key keeps it released. No spurious flips. */
    InputState in;
    input_init(&in);

    input_press(&in, KEY_A);
    input_press(&in, KEY_A);
    ASSERT_EQ(in.keyinput & KEY_A, 0);

    input_release(&in, KEY_A);
    input_release(&in, KEY_A);
    ASSERT_EQ(in.keyinput & KEY_A, KEY_A);
}

void run_input_tests(void) {
    TEST_SUITE("input");
    RUN_TEST(input_init_releases_all_keys);
    RUN_TEST(input_press_clears_active_low_bit);
    RUN_TEST(input_release_sets_active_low_bit);
    RUN_TEST(input_press_and_release_idempotent);
}
