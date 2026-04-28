#include "test_harness.h"
#include "frontend/frame_advance.h"
#include "frontend/frontend.h"
#include "frontend/input_display.h"
#include <string.h>

static Frontend make_fe(void) {
    Frontend fe;
    memset(&fe, 0, sizeof(fe));
    return fe;
}

TEST(frame_advance_press_while_running_pauses_and_steps) {
    Frontend fe = make_fe();
    ASSERT_EQ(frame_advance_request(&fe), FRAME_ADVANCE_STEPPED);
    ASSERT_EQ(fe.paused, true);
    ASSERT_EQ(fe.step_pending, true);
}

TEST(frame_advance_press_while_paused_only_sets_step) {
    Frontend fe = make_fe();
    fe.paused = true;
    ASSERT_EQ(frame_advance_request(&fe), FRAME_ADVANCE_STEPPED);
    ASSERT_EQ(fe.paused, true);
    ASSERT_EQ(fe.step_pending, true);
}

TEST(frame_advance_ignored_during_fast_forward_hold) {
    Frontend fe = make_fe();
    fe.ff_hold = true;
    ASSERT_EQ(frame_advance_request(&fe), FRAME_ADVANCE_NOOP);
    ASSERT_EQ(fe.paused, false);
    ASSERT_EQ(fe.step_pending, false);
}

TEST(frame_advance_ignored_during_fast_forward_toggle) {
    Frontend fe = make_fe();
    fe.ff_toggle = true;
    ASSERT_EQ(frame_advance_request(&fe), FRAME_ADVANCE_NOOP);
    ASSERT_EQ(fe.step_pending, false);
}

#ifdef ENABLE_REWIND
TEST(frame_advance_ignored_during_rewind) {
    Frontend fe = make_fe();
    fe.rewind_hold = true;
    ASSERT_EQ(frame_advance_request(&fe), FRAME_ADVANCE_NOOP);
    ASSERT_EQ(fe.step_pending, false);
}
#endif

TEST(input_display_held_mask_inverts_keyinput) {
    /* KEYINPUT is active-low. */
    /* All released => 0x03FF, held mask 0. */
    ASSERT_EQ(input_display_held_mask(0x03FF), 0x0000);
    /* All held => 0x0000, held mask 0x03FF. */
    ASSERT_EQ(input_display_held_mask(0x0000), 0x03FF);
    /* Just A held: bit 0 cleared in KEYINPUT -> 0x03FE, held mask 0x0001. */
    ASSERT_EQ(input_display_held_mask(0x03FE), 0x0001);
}

TEST(input_display_anchor_clamps_for_small_screens) {
    int x, y;
    input_display_anchor(20, 20, 64, 40, &x, &y);
    ASSERT_EQ(x, 0); ASSERT_EQ(y, 0);

    input_display_anchor(240, 160, 64, 40, &x, &y);
    /* Anchored bottom-right with 4px margin. */
    ASSERT_EQ(x, 240 - 64 - 4);
    ASSERT_EQ(y, 160 - 40 - 4);
}

void run_practice_tests(void) {
    TEST_SUITE("practice");
    RUN_TEST(frame_advance_press_while_running_pauses_and_steps);
    RUN_TEST(frame_advance_press_while_paused_only_sets_step);
    RUN_TEST(frame_advance_ignored_during_fast_forward_hold);
    RUN_TEST(frame_advance_ignored_during_fast_forward_toggle);
#ifdef ENABLE_REWIND
    RUN_TEST(frame_advance_ignored_during_rewind);
#endif
    RUN_TEST(input_display_held_mask_inverts_keyinput);
    RUN_TEST(input_display_anchor_clamps_for_small_screens);
}
