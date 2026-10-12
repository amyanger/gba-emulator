#include "test_harness.h"
#include "frontend/berry_alerts.h"
#include <stdio.h>
#include <string.h>

static BerryAlerts s_a;
static BerryView s_v;
static BerryToast s_out[BERRY_TOAST_MAX];

static void view_reset(void) {
    memset(&s_v, 0, sizeof(s_v));
    s_v.valid = s_v.clock_set = true;
    s_v.here = BERRY_PLACE_NONE;
    s_v.place_count = 4;
    for (uint8_t i = 0; i < 4; i++) {
        snprintf(s_v.place[i].name, sizeof(s_v.place[i].name), "ROUTE 10%u", i);
        s_v.place[i].spots = 4;
    }
}

/* Sets tree `id` (adding it if new); status follows the stage like berry_read does. */
static void tree(uint8_t id, uint8_t place, uint8_t stage, uint8_t watered, uint16_t next) {
    BerryTree* t = NULL;
    for (uint8_t i = 0; i < s_v.count; i++)
        if (s_v.tree[i].id == id) t = &s_v.tree[i];
    if (!t) t = &s_v.tree[s_v.count++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->place = place;
    t->berry = 7;
    t->stage = stage;
    t->watered = watered;
    t->next_min = next;
    t->yield_lo = t->yield_hi = 4;
    strcpy(t->name, "ORAN");
    if (stage == 5) t->status = next < BERRY_FALLING_MIN ? BERRY_FALLING : BERRY_RIPE;
    else t->status = (watered >> (stage - 1)) & 1 ? BERRY_GROWING : BERRY_WATER;
}

static uint8_t update(void) { return berry_alerts_update(&s_a, &s_v, s_out, BERRY_TOAST_MAX); }

/* A baselined state: one growing tree at place 0, standing nowhere. */
static void baseline(void) {
    berry_alerts_reset(&s_a);
    view_reset();
    tree(1, 0, 3, 0x7, 200);
    ASSERT_EQ(update(), 0);
}

TEST(alerts_baseline_silent_then_summary) {
    berry_alerts_reset(&s_a);
    view_reset();
    tree(1, 0, 3, 0x7, 200);
    ASSERT_EQ(update(), 0);    /* nothing needs the player: silent */
    berry_alerts_reset(&s_a);
    tree(2, 1, 5, 0, 300);
    tree(3, 2, 2, 0x1, 100);   /* stage 2 not watered */
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].title, "Berries");
    ASSERT_STR_EQ(s_out[0].detail, "1 ripe" " \xC2\xB7 " "1 needs water");
    ASSERT_EQ(update(), 0);    /* nothing new */
}

TEST(alerts_summary_skips_untouched_trees) {
    berry_alerts_reset(&s_a);
    view_reset();
    tree(2, 1, 5, 0, 300);
    s_v.tree[0].paused = true;
    ASSERT_EQ(update(), 0);
}

TEST(alerts_ripe_once) {
    baseline();
    tree(1, 0, 4, 0xF, 10);
    update();
    tree(1, 0, 5, 0xF, 900);
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].key, "berry:ROUTE 100");
    ASSERT_STR_EQ(s_out[0].title, "Route 100");
    ASSERT_STR_EQ(s_out[0].detail, "Oran is ripe");
    tree(1, 0, 5, 0xF, 880);
    ASSERT_EQ(update(), 0);
}

TEST(alerts_water_once_per_stage) {
    baseline();
    tree(1, 0, 4, 0x7, 100);    /* stage 4, watered1-3 only */
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].detail, "Oran needs water");
    tree(1, 0, 4, 0x7, 90);
    ASSERT_EQ(update(), 0);
    tree(1, 0, 4, 0xF, 80);     /* watered: still nothing */
    ASSERT_EQ(update(), 0);
}

TEST(alerts_falling_once) {
    baseline();
    tree(1, 0, 5, 0xF, 300);
    update();
    tree(1, 0, 5, 0xF, 59);
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].detail, "Oran drops in 59m");
    tree(1, 0, 5, 0xF, 30);
    ASSERT_EQ(update(), 0);
}

TEST(alerts_ripe_and_falling_at_once_is_one_event) {
    baseline();
    tree(1, 0, 5, 0xF, 20);     /* a clock jump straight to nearly dropped */
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].detail, "Oran drops in 20m");
}

TEST(alerts_here_quiet) {
    baseline();
    s_v.here = 0;
    update();                   /* arriving: tree 1 is growing, nothing to say */
    tree(1, 0, 4, 0x7, 100);
    ASSERT_EQ(update(), 0);
    tree(1, 0, 5, 0xF, 900);
    ASSERT_EQ(update(), 0);
}

TEST(alerts_arrival_summary) {
    baseline();
    tree(2, 1, 5, 0, 300);
    s_v.tree[1].paused = true;  /* untouched trees count when you're standing there */
    tree(3, 1, 2, 0x1, 100);
    update();                   /* events elsewhere: water at place 1 */
    s_v.here = 1;
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].title, "Route 101");
    ASSERT_STR_EQ(s_out[0].detail, "1 ripe" " \xC2\xB7 " "1 needs water");
    ASSERT_EQ(update(), 0);     /* only on arrival */
}

TEST(alerts_merge_per_place) {
    baseline();
    tree(2, 1, 4, 0x7, 100);
    tree(3, 1, 4, 0x7, 100);
    tree(4, 2, 4, 0x7, 100);
    update();
    tree(2, 1, 5, 0xF, 900);
    tree(3, 1, 5, 0xF, 900);
    tree(4, 2, 5, 0xF, 900);
    ASSERT_EQ(update(), 2);
    ASSERT_STR_EQ(s_out[0].title, "Route 101");
    ASSERT_STR_EQ(s_out[0].detail, "2 ripe");
    ASSERT_STR_EQ(s_out[1].title, "Route 102");
}

TEST(alerts_three_places_fold) {
    baseline();
    for (uint8_t p = 1; p < 4; p++) tree((uint8_t)(10 + p), p, 4, 0xF, 10);
    tree(20, 3, 4, 0xF, 10);
    update();
    for (uint8_t p = 1; p < 4; p++) tree((uint8_t)(10 + p), p, 5, 0xF, 900);
    tree(20, 3, 5, 0xF, 900);
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].key, "berry:all");
    ASSERT_STR_EQ(s_out[0].title, "Berries");
    ASSERT_STR_EQ(s_out[0].detail, "4 ripe, 3 places");
}

TEST(alerts_battle_batches_then_emits) {
    baseline();
    s_v.in_battle = true;
    tree(1, 0, 4, 0x7, 100);
    ASSERT_EQ(update(), 0);
    ASSERT_EQ(update(), 0);     /* still waiting, not lost, not doubled */
    s_v.in_battle = false;
    ASSERT_EQ(update(), 1);
    ASSERT_STR_EQ(s_out[0].detail, "Oran needs water");
    ASSERT_EQ(update(), 0);
}

TEST(alerts_invalid_view_keeps_state) {
    baseline();
    s_v.valid = false;
    ASSERT_EQ(update(), 0);
    s_v.valid = true;
    tree(1, 0, 4, 0x7, 100);
    ASSERT_EQ(update(), 1);     /* not a new baseline */
}

TEST(alerts_tree_gone_then_replanted) {
    baseline();
    s_v.count = 0;              /* picked */
    ASSERT_EQ(update(), 0);
    tree(1, 0, 1, 0, 180);      /* replanted while away: no "water" for a fresh plant */
    ASSERT_EQ(update(), 0);
    tree(1, 0, 2, 0x1, 180);    /* grew into stage 2, unwatered */
    ASSERT_EQ(update(), 1);
}

TEST(alerts_wait_for_a_loaded_save) {
    berry_alerts_reset(&s_a);
    view_reset();
    ASSERT_EQ(update(), 0);     /* boot: no trees yet, so no baseline either */
    s_v.clock_set = false;
    tree(2, 1, 5, 0, 300);
    ASSERT_EQ(update(), 0);     /* clock not set: still waiting */
    s_v.clock_set = true;
    ASSERT_EQ(update(), 1);     /* the save is in: baseline with its summary */
    ASSERT_STR_EQ(s_out[0].detail, "1 ripe");
}

TEST(alerts_rebase_is_silent) {
    baseline();
    tree(2, 1, 5, 0, 300);
    update();
    berry_alerts_rebase(&s_a);  /* state load or rewind */
    ASSERT_EQ(update(), 0);
    tree(1, 0, 4, 0x7, 100);
    ASSERT_EQ(update(), 1);     /* events still work afterwards */
}

/* Every toast line fits the 47 visible characters and never ends mid-word. */
static void assert_fits(const char* s) {
    size_t n = strlen(s);
    ASSERT_TRUE(n < 48);
    ASSERT_TRUE(n == 0 || s[n - 1] != ' ');
}

TEST(alerts_busy_toasts_fit) {
    baseline();
    /* Place 1: falling x2, ripe x3, water x1; places 2, 3: more of the same. */
    for (uint8_t p = 1; p < 4; p++)
        for (uint8_t k = 0; k < 6; k++) tree((uint8_t)(10 * p + k), p, 3, 0x7, 500);
    update();
    for (uint8_t p = 1; p < 4; p++) {
        tree((uint8_t)(10 * p), p, 5, 0xF, 20);
        tree((uint8_t)(10 * p + 1), p, 5, 0xF, 30);
        for (uint8_t k = 2; k < 5; k++) tree((uint8_t)(10 * p + k), p, 5, 0xF, 900);
        tree((uint8_t)(10 * p + 5), p, 4, 0x7, 400);
    }
    ASSERT_EQ(update(), 1);
    assert_fits(s_out[0].detail);
    ASSERT_STR_EQ(s_out[0].detail, "6 dropping" " \xC2\xB7 " "9 ripe" " \xC2\xB7 " "3 need water, 3 places");
    /* Two places with every kind: per-place details also fit. */
    baseline();
    for (uint8_t p = 1; p < 3; p++)
        for (uint8_t k = 0; k < 6; k++) tree((uint8_t)(10 * p + k), p, 3, 0x7, 500);
    update();
    for (uint8_t p = 1; p < 3; p++) {
        tree((uint8_t)(10 * p), p, 5, 0xF, 20);
        tree((uint8_t)(10 * p + 1), p, 5, 0xF, 30);
        for (uint8_t k = 2; k < 5; k++) tree((uint8_t)(10 * p + k), p, 5, 0xF, 900);
        tree((uint8_t)(10 * p + 5), p, 4, 0x7, 400);
    }
    ASSERT_EQ(update(), 2);
    assert_fits(s_out[0].detail);
    ASSERT_STR_EQ(s_out[0].detail, "2 dropping" " \xC2\xB7 " "3 ripe" " \xC2\xB7 " "1 needs water");
}

void run_berry_alerts_tests(void) {
    RUN_TEST(alerts_busy_toasts_fit);
    RUN_TEST(alerts_wait_for_a_loaded_save);
    RUN_TEST(alerts_rebase_is_silent);
    RUN_TEST(alerts_baseline_silent_then_summary);
    RUN_TEST(alerts_summary_skips_untouched_trees);
    RUN_TEST(alerts_ripe_once);
    RUN_TEST(alerts_water_once_per_stage);
    RUN_TEST(alerts_falling_once);
    RUN_TEST(alerts_ripe_and_falling_at_once_is_one_event);
    RUN_TEST(alerts_here_quiet);
    RUN_TEST(alerts_arrival_summary);
    RUN_TEST(alerts_merge_per_place);
    RUN_TEST(alerts_three_places_fold);
    RUN_TEST(alerts_battle_batches_then_emits);
    RUN_TEST(alerts_invalid_view_keeps_state);
    RUN_TEST(alerts_tree_gone_then_replanted);
}
