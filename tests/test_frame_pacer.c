#include "test_harness.h"
#include "frontend/frame_pacer.h"

TEST(pacer_period_scales_with_speed) {
    uint64_t p1 = frame_pacer_period(1000000, 1), p3 = frame_pacer_period(1000000, 3);
    ASSERT_EQ(p1, 16743);            /* 1e6 / 59.7275, rounded */
    ASSERT_EQ(p3, 5581);
}

TEST(pacer_first_call_waits_one_period) {
    FramePacer p; frame_pacer_reset(&p);
    ASSERT_EQ(frame_pacer_wait(&p, 1000, 100), 100);
    ASSERT_EQ(p.next, 1100);
}

TEST(pacer_on_time_and_late) {
    FramePacer p; frame_pacer_reset(&p);
    frame_pacer_wait(&p, 0, 100);            /* next = 100 */
    ASSERT_EQ(frame_pacer_wait(&p, 60, 100), 140);   /* next = 200 */
    ASSERT_EQ(frame_pacer_wait(&p, 250, 100), 50);   /* next = 300, slightly late frame catches up */
    ASSERT_EQ(frame_pacer_wait(&p, 390, 100), 10);
}

TEST(pacer_resyncs_after_stall) {
    FramePacer p; frame_pacer_reset(&p);
    frame_pacer_wait(&p, 0, 100);            /* next = 100 */
    ASSERT_EQ(frame_pacer_wait(&p, 10000, 100), 100);   /* way behind: next = now + period */
    ASSERT_EQ(p.next, 10100);
}

TEST(pacer_never_drifts_over_many_frames) {
    FramePacer p; frame_pacer_reset(&p);
    uint64_t now = 0;
    for (int i = 0; i < 600; i++) now += frame_pacer_wait(&p, now, 16743) + 3;  /* 3 ticks of work */
    ASSERT_TRUE(now >= 600ull * 16743 && now <= 600ull * 16743 + 3);
}

void run_frame_pacer_tests(void) {
    TEST_SUITE("frame_pacer");
    RUN_TEST(pacer_period_scales_with_speed);
    RUN_TEST(pacer_first_call_waits_one_period);
    RUN_TEST(pacer_on_time_and_late);
    RUN_TEST(pacer_resyncs_after_stall);
    RUN_TEST(pacer_never_drifts_over_many_frames);
}
