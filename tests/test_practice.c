#include "test_harness.h"
#include "frontend/frame_advance.h"
#include "frontend/frontend.h"
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

void run_practice_tests(void) {
    TEST_SUITE("practice");
    RUN_TEST(frame_advance_press_while_running_pauses_and_steps);
    RUN_TEST(frame_advance_press_while_paused_only_sets_step);
    RUN_TEST(frame_advance_ignored_during_fast_forward_hold);
    RUN_TEST(frame_advance_ignored_during_fast_forward_toggle);
#ifdef ENABLE_REWIND
    RUN_TEST(frame_advance_ignored_during_rewind);
#endif
}
