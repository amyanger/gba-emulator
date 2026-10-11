#include <string.h>
#include "game/follower_ctx.h"
#include "test_harness.h"

TEST(ctx_time_and_weather) {
    ASSERT_EQ(follower_ctx_time(3), 5);
    ASSERT_EQ(follower_ctx_time(4), 1);
    ASSERT_EQ(follower_ctx_time(16), 2);
    ASSERT_EQ(follower_ctx_time(17), 3);
    ASSERT_EQ(follower_ctx_time(23), 4);
    ASSERT_EQ(follower_ctx_weather(2), 1);
    ASSERT_EQ(follower_ctx_weather(13), 3);
    ASSERT_EQ(follower_ctx_weather(8), 0);
}

TEST(ctx_types) {
    ASSERT_EQ(follower_ctx_type(0), 1);     /* Normal */
    ASSERT_EQ(follower_ctx_type(8), 9);     /* Steel */
    ASSERT_EQ(follower_ctx_type(9), 0);     /* ??? */
    ASSERT_EQ(follower_ctx_type(10), 10);   /* Fire */
    ASSERT_EQ(follower_ctx_type(17), 17);   /* Dark */
}

TEST(ctx_build_fields) {
    FollowerInput in;
    memset(&in, 0, sizeof(in));
    in.lead.hp = 9; in.lead.max_hp = 40;
    in.lead.status = 1u << 4;               /* burn */
    in.lead.level = 50;
    in.lead.friendship = 220;
    in.lead.held_item = 13;
    in.lead.personality = 4;                /* nature 4 -> class 1 */
    in.weather = 3;
    FollowerCtxExtra x = {.type1 = 10, .type2 = 10, .gender_ratio = 0, .item_pocket = 4,
                          .species_class = 130, .adjacent_objects = 2, .hidden_items = 1,
                          .encounter_tile = true, .hour = 12, .mood = -40, .follower_facing = 2};
    HgssTalkCtx c;
    follower_ctx_build(&in, &x, &c);
    ASSERT_EQ(c.c[HC_HP], 5);
    ASSERT_EQ(c.c[HC_STATUS], 2);
    ASSERT_EQ(c.c[HC_LEVEL], 4);
    ASSERT_EQ(c.c[HC_FRIEND], 220);
    ASSERT_EQ(c.c[HC_HAS_ITEM], 1);
    ASSERT_EQ(c.c[HC_ITEM_CLASS], 7);
    ASSERT_EQ(c.c[HC_NATURE], 1);
    ASSERT_EQ(c.c[HC_GENDER], 1);
    ASSERT_EQ(c.c[HC_WEATHER], 3);
    ASSERT_EQ(c.c[HC_ENCOUNTER], 1);
    ASSERT_EQ(c.c[HC_TIME], 2);
    ASSERT_EQ((int8_t)c.c[HC_MOOD], -40);
    ASSERT_EQ(c.c[HC_FACING], 3);
    ASSERT_EQ(c.map_id, 0xFFFF);
}

void run_follower_ctx_tests(void) {
    RUN_TEST(ctx_time_and_weather);
    RUN_TEST(ctx_types);
    RUN_TEST(ctx_build_fields);
}
