#include "test_harness.h"
#include "game/follower_places.h"

TEST(behavior_translation) {
    ASSERT_EQ(follower_place_behavior(0x02), 2);
    ASSERT_EQ(follower_place_behavior(0x03), 3);
    ASSERT_EQ(follower_place_behavior(0x06), 33);
    ASSERT_EQ(follower_place_behavior(0x16), 22);
    ASSERT_EQ(follower_place_behavior(0x17), 23);
    ASSERT_EQ(follower_place_behavior(0x20), 32);
    ASSERT_EQ(follower_place_behavior(0x21), 33);
    ASSERT_EQ(follower_place_behavior(0x10), 0);
    ASSERT_EQ(follower_place_behavior(0x12), 0);
    ASSERT_EQ(follower_place_behavior(0x15), 0);
    ASSERT_EQ(follower_place_behavior(0x00), 0);
}

TEST(deny_list) {
    ASSERT_TRUE(follower_place_msg_denied(43));
    ASSERT_TRUE(follower_place_msg_denied(753));
    ASSERT_TRUE(follower_place_msg_denied(410));
    ASSERT_TRUE(!follower_place_msg_denied(42));
    ASSERT_TRUE(!follower_place_msg_denied(54));
    ASSERT_TRUE(!follower_place_msg_denied(754));
    ASSERT_TRUE(!follower_place_msg_denied(0));
}

TEST(place_mapsec) {
    ASSERT_EQ(follower_place_mapsec(4, 55, 0), 200);   /* cave */
    ASSERT_EQ(follower_place_mapsec(6, 40, 0), 167);   /* ocean route */
    ASSERT_EQ(follower_place_mapsec(3, 17, 0x21), 167);/* beach on a route */
    ASSERT_EQ(follower_place_mapsec(3, 17, 0x00), 0);  /* plain route */
    ASSERT_EQ(follower_place_mapsec(3, 59, 0), 224);   /* Petalburg Woods */
    ASSERT_EQ(follower_place_mapsec(3, 56, 0), 137);   /* mountain */
    ASSERT_EQ(follower_place_mapsec(1, 0, 0), 126);    /* town */
    ASSERT_EQ(follower_place_mapsec(2, 7, 0), 127);    /* city */
    ASSERT_EQ(follower_place_mapsec(4, 67, 0x20), 217);/* ice */
    ASSERT_EQ(follower_place_mapsec(8, 0, 0), 0);      /* indoor */
}

void run_follower_places_tests(void) {
    RUN_TEST(behavior_translation);
    RUN_TEST(deny_list);
    RUN_TEST(place_mapsec);
}
