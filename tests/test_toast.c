#include "test_harness.h"
#include "frontend/toast.h"
#include "savestate/savestate.h"

static ToastQueue q;

static void reset(void) { toast_init(&q); }

TEST(toast_push_appends_and_sets_changed) {
    reset();
    ASSERT_TRUE(!q.changed);
    toast_push(&q, 1000, TOAST_OK, "State saved", "Slot 3");
    ASSERT_EQ(q.count, 1);
    ASSERT_TRUE(q.changed);
    ASSERT_STR_EQ(q.items[0].title, "State saved");
    ASSERT_STR_EQ(q.items[0].detail, "Slot 3");
    toast_push(&q, 1000, TOAST_INFO, "Paused", NULL);
    ASSERT_STR_EQ(q.items[1].detail, "");
}

TEST(toast_alpha_phases) {
    reset();
    toast_push(&q, 1000, TOAST_OK, "A", NULL);
    const Toast* t = &q.items[0];
    ASSERT_TRUE(toast_alpha(t, 1000) < 0.01f);
    ASSERT_TRUE(toast_alpha(t, 1075) > 0.45f && toast_alpha(t, 1075) < 0.55f);
    ASSERT_TRUE(toast_alpha(t, 1150) > 0.99f);
    ASSERT_TRUE(toast_alpha(t, 1150 + TOAST_HOLD_MS - 1) > 0.99f);
    ASSERT_TRUE(toast_alpha(t, 1150 + TOAST_HOLD_MS + 150) > 0.45f &&
                toast_alpha(t, 1150 + TOAST_HOLD_MS + 150) < 0.55f);
    ASSERT_TRUE(toast_alpha(t, 1150 + TOAST_HOLD_MS + TOAST_OUT_MS) < 0.01f);
    ASSERT_TRUE(toast_slide(t, 1000) < 0.01f);
    ASSERT_TRUE(toast_slide(t, 1150) > 0.99f);
}

TEST(toast_errors_hold_longer) {
    reset();
    toast_push(&q, 0, TOAST_ERROR, "Slot 4 is empty", NULL);
    toast_push(&q, 0, TOAST_INFO, "Paused", NULL);
    uint32_t t = TOAST_IN_MS + TOAST_HOLD_MS + TOAST_OUT_MS;   /* info gone */
    toast_tick(&q, t);
    ASSERT_EQ(q.count, 1);
    ASSERT_STR_EQ(q.items[0].title, "Slot 4 is empty");
    toast_tick(&q, TOAST_IN_MS + TOAST_HOLD_LONG_MS + TOAST_OUT_MS);
    ASSERT_EQ(q.count, 0);
}

TEST(toast_merge_restarts_and_keeps_position) {
    reset();
    toast_push(&q, 0, TOAST_INFO, "Slot selected", "Slot 1");
    toast_push(&q, 0, TOAST_OK, "State saved", "Slot 1");
    toast_push(&q, 2000, TOAST_INFO, "Slot selected", "Slot 5");
    ASSERT_EQ(q.count, 2);
    ASSERT_STR_EQ(q.items[0].detail, "Slot 5");
    ASSERT_TRUE(toast_alpha(&q.items[0], 2000) > 0.99f);         /* no replayed fade-in */
    toast_tick(&q, TOAST_IN_MS + TOAST_HOLD_MS + TOAST_OUT_MS);  /* original expiry */
    ASSERT_EQ(q.count, 1);
    ASSERT_STR_EQ(q.items[0].title, "Slot selected");
}

TEST(toast_keyed_toggle_merges) {
    reset();
    for (uint32_t i = 0; i < 10; i++)
        toast_push_keyed(&q, i * 50, "pause", TOAST_INFO, (i & 1) ? "Resumed" : "Paused", NULL);
    ASSERT_EQ(q.count, 1);
    ASSERT_STR_EQ(q.items[0].title, "Resumed");
    for (uint32_t i = 0; i < 30; i++) toast_push(&q, 600, TOAST_INFO, "Frame advance", NULL);
    ASSERT_EQ(q.count, 2);
}

TEST(toast_merge_revives_fading_toast) {
    reset();
    toast_push(&q, 0, TOAST_INFO, "Slot selected", "Slot 1");
    uint32_t fading = TOAST_IN_MS + TOAST_HOLD_MS + 100;
    ASSERT_TRUE(toast_alpha(&q.items[0], fading) < 0.99f);
    toast_push(&q, fading, TOAST_INFO, "Slot selected", "Slot 2");
    ASSERT_TRUE(toast_alpha(&q.items[0], fading) > 0.99f);
}

TEST(toast_fifth_starts_oldest_fading) {
    reset();
    toast_push(&q, 0, TOAST_INFO, "a", NULL);
    toast_push(&q, 0, TOAST_INFO, "b", NULL);
    toast_push(&q, 0, TOAST_INFO, "c", NULL);
    toast_push(&q, 0, TOAST_INFO, "d", NULL);
    toast_push(&q, 1000, TOAST_INFO, "e", NULL);
    ASSERT_EQ(q.count, 5);
    ASSERT_TRUE(toast_alpha(&q.items[0], 1000 + TOAST_OUT_MS / 2) < 0.99f);   /* "a" fading */
    ASSERT_TRUE(toast_alpha(&q.items[1], 1000 + TOAST_OUT_MS / 2) > 0.99f);
    toast_tick(&q, 1000 + TOAST_OUT_MS);
    ASSERT_EQ(q.count, 4);
    ASSERT_STR_EQ(q.items[0].title, "b");
    ASSERT_STR_EQ(q.items[3].title, "e");
}

TEST(toast_overflow_never_exceeds_cap) {
    reset();
    char name[8];
    for (uint32_t i = 0; i < 40; i++) {
        snprintf(name, sizeof(name), "t%u", (unsigned)i);
        toast_push(&q, i, TOAST_INFO, name, NULL);
        ASSERT_TRUE(q.count <= TOAST_CAP);
    }
    ASSERT_STR_EQ(q.items[q.count - 1].title, "t39");
}

TEST(toast_long_strings_truncate) {
    reset();
    char big[200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    toast_push(&q, 0, TOAST_INFO, big, big);
    ASSERT_EQ(strlen(q.items[0].title), TOAST_TITLE_LEN - 1);
    ASSERT_EQ(strlen(q.items[0].detail), TOAST_DETAIL_LEN - 1);
}

TEST(toast_animating_only_in_fades) {
    reset();
    ASSERT_TRUE(!toast_animating(&q, 0));
    toast_push(&q, 0, TOAST_OK, "A", NULL);
    ASSERT_TRUE(toast_animating(&q, 50));
    ASSERT_TRUE(!toast_animating(&q, 1000));
    ASSERT_TRUE(toast_animating(&q, TOAST_IN_MS + TOAST_HOLD_MS + 10));
}

TEST(toast_tick_sets_changed_on_expiry) {
    reset();
    toast_push(&q, 0, TOAST_OK, "A", NULL);
    q.changed = false;
    toast_tick(&q, 1000);
    ASSERT_TRUE(!q.changed);
    toast_tick(&q, TOAST_IN_MS + TOAST_HOLD_MS + TOAST_OUT_MS);
    ASSERT_TRUE(q.changed);
    ASSERT_EQ(q.count, 0);
}

TEST(toast_badges_change_flag) {
    reset();
    toast_set_badge(&q, BADGE_MUTED, true);
    ASSERT_TRUE(q.changed && q.badges[BADGE_MUTED]);
    q.changed = false;
    toast_set_badge(&q, BADGE_MUTED, true);
    ASSERT_TRUE(!q.changed);
    toast_set_badge(&q, BADGE_MUTED, false);
    ASSERT_TRUE(q.changed && !q.badges[BADGE_MUTED]);
}

TEST(toast_savestate_error_covers_all) {
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_FILE_OPEN), "Can't open the file");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_FILE_WRITE), "Can't write the file");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_FILE_READ), "Can't read the file");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_BAD_MAGIC), "Not a save state");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_BAD_VERSION), "Made by an older version");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_ROM_MISMATCH), "Made with another ROM");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_CORRUPT), "File is damaged");
    ASSERT_STR_EQ(toast_savestate_error(SS_ERR_TRUNCATED), "File is incomplete");
    ASSERT_STR_EQ(toast_savestate_error(999), "Unknown error");
}

TEST(toast_failure_edge_once_per_streak) {
    bool reported = false;
    ASSERT_TRUE(!toast_failure_edge(&reported, false, false));   /* no attempt */
    ASSERT_TRUE(toast_failure_edge(&reported, true, false));     /* first failure */
    ASSERT_TRUE(!toast_failure_edge(&reported, true, false));    /* retry */
    ASSERT_TRUE(!toast_failure_edge(&reported, false, false));
    ASSERT_TRUE(!toast_failure_edge(&reported, true, true));     /* recovered */
    ASSERT_TRUE(toast_failure_edge(&reported, true, false));     /* new streak */
}

void run_toast_tests(void) {
    TEST_SUITE("toast");
    RUN_TEST(toast_push_appends_and_sets_changed);
    RUN_TEST(toast_alpha_phases);
    RUN_TEST(toast_errors_hold_longer);
    RUN_TEST(toast_merge_restarts_and_keeps_position);
    RUN_TEST(toast_keyed_toggle_merges);
    RUN_TEST(toast_merge_revives_fading_toast);
    RUN_TEST(toast_fifth_starts_oldest_fading);
    RUN_TEST(toast_overflow_never_exceeds_cap);
    RUN_TEST(toast_long_strings_truncate);
    RUN_TEST(toast_animating_only_in_fades);
    RUN_TEST(toast_tick_sets_changed_on_expiry);
    RUN_TEST(toast_badges_change_flag);
    RUN_TEST(toast_savestate_error_covers_all);
    RUN_TEST(toast_failure_edge_once_per_streak);
}
