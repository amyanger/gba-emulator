#include "test_harness.h"
#include "game/learnset.h"

#define E(lv, mv) (uint16_t)((lv) << 9 | (mv))
#define END 0xFFFF

/* Move ids (pret include/constants/moves.h) */
enum { TAKE_DOWN = 36, HYPER_BEAM = 63, EARTHQUAKE = 89, CONFUSION = 93, PSYCHIC = 94,
       AGILITY = 97, SKETCH = 166, SCARY_FACE = 184, PURSUIT = 228, METAL_CLAW = 232,
       SHADOW_BALL = 247, BRICK_BREAK = 280, METEOR_MASH = 309, IRON_DEFENSE = 334 };

/* Metagross: Lv1 moves repeat at Lv20/26, like the real learnset. */
static const uint16_t k_metagross[] = {
    E(1, TAKE_DOWN), E(1, CONFUSION), E(1, METAL_CLAW), E(1, SCARY_FACE), E(20, CONFUSION),
    E(20, METAL_CLAW), E(26, SCARY_FACE), E(32, PURSUIT), E(38, PSYCHIC), E(44, IRON_DEFENSE),
    E(55, METEOR_MASH), E(66, AGILITY), E(77, HYPER_BEAM), END};
static const uint16_t k_known[4] = {METEOR_MASH, EARTHQUAKE, SHADOW_BALL, BRICK_BREAK};

TEST(learnset_metagross_matches_the_game) {
    static const uint16_t want[8] = {TAKE_DOWN, CONFUSION, METAL_CLAW, SCARY_FACE,
                                     PURSUIT, PSYCHIC, IRON_DEFENSE, AGILITY};
    LearnsetView v;
    learnset_scan(k_metagross, 14, 70, k_known, 355, &v);
    ASSERT_EQ(v.relearn_count, 8);
    for (int i = 0; i < 8; i++) ASSERT_EQ(v.relearn[i], want[i]);
    ASSERT_EQ(v.next_level, 77);
    ASSERT_EQ(v.next_count, 1);
    ASSERT_EQ(v.next[0], HYPER_BEAM);
}

TEST(learnset_entry_at_own_level_is_relearn_not_next) {
    LearnsetView v;
    learnset_scan(k_metagross, 14, 66, k_known, 355, &v);
    ASSERT_EQ(v.relearn[v.relearn_count - 1], AGILITY);
    ASSERT_EQ(v.next_level, 77);
}

TEST(learnset_same_level_next_moves_listed_together) {
    static const uint16_t e[] = {E(1, TAKE_DOWN), E(40, PSYCHIC), E(40, AGILITY), E(40, PSYCHIC),
                                 E(50, HYPER_BEAM), END};
    static const uint16_t none[4] = {0, 0, 0, 0};
    LearnsetView v;
    learnset_scan(e, 6, 30, none, 355, &v);
    ASSERT_EQ(v.next_level, 40);
    ASSERT_EQ(v.next_count, 2);
    ASSERT_EQ(v.next[0], PSYCHIC);
    ASSERT_EQ(v.next[1], AGILITY);
}

TEST(learnset_level_100_has_nothing_next) {
    LearnsetView v;
    learnset_scan(k_metagross, 14, 100, k_known, 355, &v);
    ASSERT_EQ(v.next_level, 0);
    ASSERT_EQ(v.next_count, 0);
    ASSERT_EQ(v.relearn_count, 9); /* the 8 plus Hyper Beam */
}

TEST(learnset_smeargle_sketch_once) {
    uint16_t e[11];
    for (int i = 0; i < 10; i++) e[i] = E(1 + 10 * i, SKETCH);
    e[10] = END;
    const uint16_t known[4] = {SKETCH, 0, 0, 0};
    LearnsetView v;
    learnset_scan(e, 11, 50, known, 355, &v);
    ASSERT_EQ(v.relearn_count, 0);
    ASSERT_EQ(v.next_level, 0); /* it already knows the only move it would learn */
    ASSERT_EQ(v.next_count, 0);
    const uint16_t blank[4] = {0, 0, 0, 0};
    learnset_scan(e, 11, 50, blank, 355, &v);
    ASSERT_EQ(v.relearn_count, 1);
}

TEST(learnset_skips_bad_move_ids) {
    static const uint16_t e[] = {E(1, 0), E(1, 400), E(1, PSYCHIC), E(9, 511), E(10, AGILITY), END};
    static const uint16_t none[4] = {0, 0, 0, 0};
    LearnsetView v;
    learnset_scan(e, 6, 5, none, 355, &v);
    ASSERT_EQ(v.relearn_count, 1);
    ASSERT_EQ(v.relearn[0], PSYCHIC);
    ASSERT_EQ(v.next_level, 10);
    ASSERT_EQ(v.next[0], AGILITY);
}

TEST(learnset_stops_at_20_without_terminator) {
    uint16_t e[25];
    for (int i = 0; i < 25; i++) e[i] = E(1, 1 + i);
    static const uint16_t none[4] = {0, 0, 0, 0};
    LearnsetView v;
    learnset_scan(e, 25, 50, none, 355, &v);
    ASSERT_EQ(v.relearn_count, LEARNSET_MAX);
    ASSERT_EQ(v.relearn[LEARNSET_MAX - 1], 20);
}

TEST(learnset_respects_available_count) {
    LearnsetView v;
    learnset_scan(k_metagross, 2, 70, k_known, 355, &v);
    ASSERT_EQ(v.relearn_count, 2);
    ASSERT_EQ(v.next_level, 0);
}

void run_learnset_tests(void) {
    RUN_TEST(learnset_metagross_matches_the_game);
    RUN_TEST(learnset_entry_at_own_level_is_relearn_not_next);
    RUN_TEST(learnset_same_level_next_moves_listed_together);
    RUN_TEST(learnset_level_100_has_nothing_next);
    RUN_TEST(learnset_smeargle_sketch_once);
    RUN_TEST(learnset_skips_bad_move_ids);
    RUN_TEST(learnset_stops_at_20_without_terminator);
    RUN_TEST(learnset_respects_available_count);
}
