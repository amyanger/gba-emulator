#include "test_harness.h"
#include "game/battle_info.h"

/* Water->Fire x2, Fire->Water x0.5, Normal->Rock x0.5, Electric->Ground x0,
 * Foresight separator, Normal->Ghost x0, end. */
static const uint8_t k_table[] = {
    11, 10, 20,
    10, 11, 5,
    0, 5, 5,
    13, 4, 0,
    10, 12, 20, 10, 6, 20,
    0xFE, 0xFE, 0,
    0, 7, 0,
    0xFF, 0xFF, 0,
};

TEST(type_quarters_single_and_dual_types) {
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 11, 10, 10, false), 8);  /* x2 */
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 10, 11, 11, false), 2);  /* x0.5 */
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 10, 12, 6, false), 16);  /* Grass/Bug x4 */
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 13, 4, 2, false), 0);    /* immune */
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 3, 0, 0, false), 4);     /* unlisted x1 */
}

TEST(type_quarters_same_second_type_counts_once) {
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 11, 10, 10, false), 8);
}

TEST(type_quarters_foresight_skips_ghost_rows) {
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 0, 7, 7, false), 0);
    ASSERT_EQ(battle_type_quarters(k_table, sizeof(k_table), 0, 7, 7, true), 4);
}

TEST(type_quarters_stops_at_table_length_without_terminator) {
    ASSERT_EQ(battle_type_quarters(k_table, 3, 10, 11, 11, false), 4);
}

TEST(ability_levitate_and_wonder_guard) {
    ASSERT_EQ(battle_apply_ability(8, TYPE_GROUND, ABILITY_LEVITATE), 0);
    ASSERT_EQ(battle_apply_ability(8, 10, ABILITY_LEVITATE), 8);
    ASSERT_EQ(battle_apply_ability(4, 0, ABILITY_WONDER_GUARD), 0);
    ASSERT_EQ(battle_apply_ability(8, 0, ABILITY_WONDER_GUARD), 8);
}

static CatchInput base(uint8_t ball, uint8_t rate, uint16_t hp, uint16_t max_hp) {
    CatchInput in = {0};
    in.ball = ball; in.catch_rate = rate; in.hp = hp; in.max_hp = max_hp; in.level = 5;
    return in;
}

TEST(catch_reference_values) {
    CatchInput in = base(BALL_POKE, 45, 30, 30);
    ASSERT_EQ(battle_catch_permille(&in), 62);
    in = base(BALL_POKE, 255, 30, 30);
    ASSERT_EQ(battle_catch_permille(&in), 337);
    in = base(BALL_GREAT, 45, 15, 30);
    ASSERT_EQ(battle_catch_permille(&in), 198);
    in = base(BALL_POKE, 3, 30, 30);
    ASSERT_EQ(battle_catch_permille(&in), 4);
    in = base(BALL_POKE, 45, 10, 20);
    ASSERT_EQ(battle_catch_permille(&in), 123);
}

TEST(catch_status_multipliers) {
    CatchInput in = base(BALL_POKE, 45, 1, 30);
    in.status1 = STATUS1_PARALYSIS;
    ASSERT_EQ(battle_catch_permille(&in), 280);
    in = base(BALL_ULTRA, 255, 1, 30);
    in.status1 = 2;  /* asleep, 2 turns */
    ASSERT_EQ(battle_catch_permille(&in), 1000);
}

TEST(catch_master_and_zero_odds) {
    CatchInput in = base(BALL_MASTER, 3, 30, 30);
    ASSERT_EQ(battle_catch_permille(&in), 1000);
    in = base(BALL_POKE, 1, 30, 30);
    ASSERT_EQ(battle_catch_permille(&in), 0);
    in = base(BALL_POKE, 45, 0, 0);
    ASSERT_EQ(battle_catch_permille(&in), 0);
}

TEST(catch_hp_above_max_clamps) {
    CatchInput in = base(BALL_POKE, 45, 45, 30);
    ASSERT_EQ(battle_catch_permille(&in), 62);
}

TEST(ball_multipliers_follow_emerald_rules) {
    CatchInput in = base(BALL_NET, 45, 30, 30);
    ASSERT_EQ(battle_ball_multiplier(&in), 10);
    in.type2 = TYPE_WATER;
    ASSERT_EQ(battle_ball_multiplier(&in), 30);
    in = base(BALL_DIVE, 45, 30, 30);
    ASSERT_EQ(battle_ball_multiplier(&in), 10);
    in.underwater = true;
    ASSERT_EQ(battle_ball_multiplier(&in), 35);
    in = base(BALL_NEST, 45, 30, 30);
    in.level = 5;  ASSERT_EQ(battle_ball_multiplier(&in), 35);
    in.level = 35; ASSERT_EQ(battle_ball_multiplier(&in), 10);
    in.level = 40; ASSERT_EQ(battle_ball_multiplier(&in), 10);
    in = base(BALL_REPEAT, 45, 30, 30);
    ASSERT_EQ(battle_ball_multiplier(&in), 10);
    in.owned = true;
    ASSERT_EQ(battle_ball_multiplier(&in), 30);
    in = base(BALL_TIMER, 45, 30, 30);
    in.turn_counter = 0;   ASSERT_EQ(battle_ball_multiplier(&in), 10);
    in.turn_counter = 25;  ASSERT_EQ(battle_ball_multiplier(&in), 35);
    in.turn_counter = 40;  ASSERT_EQ(battle_ball_multiplier(&in), 40);
    in.turn_counter = 250; ASSERT_EQ(battle_ball_multiplier(&in), 4);  /* u8 wrap, as in the game */
    in = base(BALL_PREMIER, 45, 30, 30);
    ASSERT_EQ(battle_ball_multiplier(&in), 10);
}

TEST(isqrt_floors) {
    ASSERT_EQ(battle_isqrt(0), 0);
    ASSERT_EQ(battle_isqrt(1114112), 1055);
    ASSERT_EQ(battle_isqrt(1055), 32);
    ASSERT_EQ(battle_isqrt(16711680), 4087);
}

void run_game_battle_tests(void) {
    TEST_SUITE("game_battle");
    RUN_TEST(type_quarters_single_and_dual_types);
    RUN_TEST(type_quarters_same_second_type_counts_once);
    RUN_TEST(type_quarters_foresight_skips_ghost_rows);
    RUN_TEST(type_quarters_stops_at_table_length_without_terminator);
    RUN_TEST(ability_levitate_and_wonder_guard);
    RUN_TEST(catch_reference_values);
    RUN_TEST(catch_status_multipliers);
    RUN_TEST(catch_master_and_zero_odds);
    RUN_TEST(catch_hp_above_max_clamps);
    RUN_TEST(ball_multipliers_follow_emerald_rules);
    RUN_TEST(isqrt_floors);
}
