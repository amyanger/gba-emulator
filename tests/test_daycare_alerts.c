#include "test_harness.h"
#include "frontend/daycare_alerts.h"

static DaycareAlerts s_a;

static bool up(bool valid, uint32_t pid, uint32_t sb1, bool battle) {
    return daycare_alerts_update(&s_a, valid, pid, sb1, battle);
}

/* Two matching reads of "no egg": baselined. */
static void baseline_empty(void) {
    daycare_alerts_reset(&s_a);
    ASSERT_TRUE(!(up(true, 0, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0, 0x100, false)));
}

TEST(dcalert_waiting_egg_at_baseline_is_silent) {
    daycare_alerts_reset(&s_a);
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
}

TEST(dcalert_new_egg_fires_once_after_two_reads) {
    baseline_empty();
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(up(true, 0xAB, 0x100, false));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
}

TEST(dcalert_single_garbage_read_ignored) {
    baseline_empty();
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0, 0x100, false)));
}

TEST(dcalert_sb1_change_delays_toast) {
    baseline_empty();
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x200, false)));   /* save block moved: not a match */
    ASSERT_TRUE(up(true, 0xAB, 0x200, false));
}

TEST(dcalert_rebase_with_waiting_egg_is_silent) {
    baseline_empty();
    daycare_alerts_rebase(&s_a);
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
}

TEST(dcalert_battle_defers_toast) {
    baseline_empty();
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, true)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, true)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, true)));
    ASSERT_TRUE(up(true, 0xAB, 0x100, false));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
}

TEST(dcalert_egg_collected_then_new_egg) {
    baseline_empty();
    up(true, 0xAB, 0x100, false);
    ASSERT_TRUE(up(true, 0xAB, 0x100, false));
    up(true, 0, 0x100, false);
    ASSERT_TRUE(!(up(true, 0, 0x100, false)));
    up(true, 0xCD, 0x100, false);
    ASSERT_TRUE(up(true, 0xCD, 0x100, false));
}

TEST(dcalert_long_unreadable_rebaselines_silently) {
    baseline_empty();
    for (int i = 0; i < 60; i++) ASSERT_TRUE(!(up(false, 0, 0, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
    ASSERT_TRUE(!(up(true, 0xAB, 0x100, false)));
}

TEST(dcalert_short_unreadable_keeps_baseline) {
    baseline_empty();
    for (int i = 0; i < 10; i++) up(false, 0, 0, false);
    up(true, 0xAB, 0x100, false);
    ASSERT_TRUE(up(true, 0xAB, 0x100, false));
}

void run_daycare_alerts_tests(void) {
    RUN_TEST(dcalert_waiting_egg_at_baseline_is_silent);
    RUN_TEST(dcalert_new_egg_fires_once_after_two_reads);
    RUN_TEST(dcalert_single_garbage_read_ignored);
    RUN_TEST(dcalert_sb1_change_delays_toast);
    RUN_TEST(dcalert_rebase_with_waiting_egg_is_silent);
    RUN_TEST(dcalert_battle_defers_toast);
    RUN_TEST(dcalert_egg_collected_then_new_egg);
    RUN_TEST(dcalert_long_unreadable_rebaselines_silently);
    RUN_TEST(dcalert_short_unreadable_keeps_baseline);
}
