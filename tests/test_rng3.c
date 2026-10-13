#include "game/rng3.h"
#include "test_harness.h"

TEST(rng3_next_matches_lcg) {
    ASSERT_EQ_HEX(rng3_next(0), 0x00006073u);
    ASSERT_EQ_HEX(rng3_next(0x6073), 0x41C64E6Du * 0x6073u + 0x6073u);
}

TEST(rng3_jump_equals_loop) {
    uint32_t s = 0x12345678, t = s;
    for (int i = 0; i < 1000; i++)
        t = rng3_next(t);
    ASSERT_EQ_HEX(rng3_jump(s, 1000), t);
    ASSERT_EQ_HEX(rng3_jump(s, 0), s);
}

/* factcheck F1: 2349 advances from seed 0 gave 0x6226B90F live */
TEST(rng3_index_live_boot) {
    ASSERT_EQ(rng3_index(0x6226B90Fu), 2349);
    ASSERT_EQ_HEX(rng3_jump(0, 2349), 0x6226B90Fu);
}

TEST(rng3_distance_roundtrip) {
    uint32_t a = 0xDEADBEEF;
    ASSERT_EQ(rng3_distance(a, rng3_jump(a, 123456)), 123456);
    ASSERT_EQ(rng3_distance(a, a), 0);
}

void run_rng3_tests(void) {
    RUN_TEST(rng3_next_matches_lcg);
    RUN_TEST(rng3_jump_equals_loop);
    RUN_TEST(rng3_index_live_boot);
    RUN_TEST(rng3_distance_roundtrip);
}
