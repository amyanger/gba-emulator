#include "frontend/follower_ambient.h"
#include "test_harness.h"
#include <string.h>

/* rand returns *ud: 0 picks next_idle 240, look around, an emote; 2 picks hop; 50 no emote. */
static uint32_t fixed_rand(void* ud) {
    return *(uint32_t*)ud;
}

typedef struct {
    FollowerAmbient a;
    Follower s;
    FollowerInput in;
    AmbientCtx c;
    uint32_t r;
} Rig;

static void rig_init(Rig* g, uint32_t r) {
    memset(g, 0, sizeof(*g));
    ambient_reset(&g->a);
    g->s.tx = 5;
    g->s.ty = 5;
    g->s.facing = 1;
    g->in.free = true;
    g->in.cur_x = 5;
    g->in.cur_y = 6;
    g->c.shown = true;
    g->c.friendship = 70;
    g->r = r;
}

static void frame(Rig* g) {
    ambient_frame(&g->a, &g->s, &g->in, &g->c, fixed_rand, &g->r);
    g->s.event = FOL_EV_NONE;
    g->s.event_arg = 0;
    g->s.steps = 0;
}

static void event(Rig* g, uint8_t ev, uint8_t arg) {
    g->s.event = ev;
    g->s.event_arg = arg;
    frame(g);
}

TEST(idle_fires_after_still) {
    Rig g;
    rig_init(&g, 0);
    frame(&g);
    uint16_t next = g.a.next_idle;
    ASSERT_EQ(next, 240);
    while (g.a.still + 1 < next) {
        frame(&g);
        ASSERT_TRUE(!ambient_active(&g.a));
    }
    frame(&g);
    ASSERT_TRUE(ambient_active(&g.a));
}

TEST(step_aborts_and_resets) {
    Rig g;
    rig_init(&g, 2);
    for (int i = 0; i < 243 && !ambient_active(&g.a); i++) frame(&g);
    ASSERT_TRUE(ambient_active(&g.a));
    ASSERT_TRUE(g.a.player.off_y != 0);     /* hop in progress */
    g.in.cur_x++;
    g.s.steps = 1;
    frame(&g);
    ASSERT_TRUE(!ambient_active(&g.a));
    ASSERT_EQ(g.a.still, 0);
    ASSERT_TRUE(g.a.player.off_x == 0 && g.a.player.off_y == 0);
}

TEST(talk_blocks_idle) {
    Rig g;
    rig_init(&g, 0);
    g.c.talking = true;
    for (int i = 0; i < 3000; i++) {
        frame(&g);
        ASSERT_TRUE(!ambient_active(&g.a));
    }
    g.c.talking = false;
    frame(&g);
    ASSERT_TRUE(!ambient_active(&g.a));
    ASSERT_EQ(g.a.still, 1);
    ASSERT_TRUE(!g.a.slept);
}

TEST(trainer_delay) {
    Rig g;
    rig_init(&g, 0);
    g.in.free = false;                      /* trainer sight locks the player */
    event(&g, FOL_EV_TRAINER, 0);
    for (int i = 1; i < 18; i++) {
        ASSERT_TRUE(g.a.player.phase != TALK_EMOTE);
        frame(&g);
    }
    ASSERT_TRUE(g.a.player.phase != TALK_EMOTE);
    frame(&g);                              /* frame 18 */
    ASSERT_EQ(g.a.player.phase, TALK_EMOTE);
    ASSERT_EQ(g.a.player.emote, 9);
    for (int i = 0; i < 65; i++) frame(&g);
    ASSERT_EQ(g.a.player.phase, TALK_EMOTE);
    frame(&g);
    ASSERT_EQ(g.a.player.phase, TALK_IDLE);
}

TEST(hidden_drops_event) {
    Rig g;
    rig_init(&g, 0);
    g.in.free = false;
    g.c.shown = false;
    event(&g, FOL_EV_TRAINER, 0);
    g.c.shown = true;
    for (int i = 0; i < 100; i++) {
        frame(&g);
        ASSERT_TRUE(!ambient_active(&g.a));
    }
}

TEST(item_ball_no_question) {
    Rig g;
    rig_init(&g, 0);
    g.in.free = false;
    event(&g, FOL_EV_NPC_TALK, OBJ_EVENT_GFX_ITEM_BALL);
    ASSERT_TRUE(!ambient_active(&g.a));
    event(&g, FOL_EV_NPC_TALK, OBJ_EVENT_GFX_BERRY_TREE);
    ASSERT_TRUE(!ambient_active(&g.a));
    event(&g, FOL_EV_ITEM, 0);
    ASSERT_TRUE(ambient_active(&g.a));
    ASSERT_EQ(g.a.player.emote, 7);
}

TEST(cooldown) {
    Rig g;
    rig_init(&g, 0);
    g.in.free = false;
    event(&g, FOL_EV_NPC_TALK, 1);
    ASSERT_TRUE(ambient_active(&g.a));
    for (int i = 1; i < 60; i++) frame(&g);
    event(&g, FOL_EV_NPC_TALK, 1);          /* frame 60: ignored */
    for (int i = 61; i < 70; i++) frame(&g);
    ASSERT_TRUE(!ambient_active(&g.a));

    rig_init(&g, 0);
    g.in.free = false;
    event(&g, FOL_EV_NPC_TALK, 1);
    for (int i = 1; i < 200; i++) frame(&g);
    ASSERT_TRUE(!ambient_active(&g.a));
    event(&g, FOL_EV_NPC_TALK, 1);
    ASSERT_TRUE(ambient_active(&g.a));
}

TEST(zz_once) {
    Rig g;
    rig_init(&g, 50);
    int zz = 0;
    for (int i = 0; i < 6000; i++) {
        frame(&g);
        if (g.a.player.phase == TALK_EMOTE && g.a.player.emote == 14 && g.a.player.emote_t == 0) zz++;
        if (i == 2700) ASSERT_EQ(zz, 1);
    }
    ASSERT_EQ(zz, 1);
}

TEST(zz_not_back_to_back) {
    Rig g;
    rig_init(&g, 0);                        /* idles at 240 + 480k: one falls due at 2640 */
    int starts = 0, last = -10000, zz = 0;
    bool was = false;
    for (int i = 0; i < 4000; i++) {
        frame(&g);
        bool on = ambient_active(&g.a);
        if (on && !was) {
            ASSERT_TRUE(i - last >= 480);
            last = i;
            starts++;
            if (g.a.player.emote == 14) zz++;
        }
        was = on;
    }
    ASSERT_EQ(zz, 1);
    ASSERT_TRUE(starts >= 7);
}

TEST(idle_keeps_last_facing) {
    Rig g;
    rig_init(&g, 1);                        /* face the player */
    g.in.cur_x = 6;
    g.in.cur_y = 5;
    int i = 0;
    while (!ambient_active(&g.a) && i++ < 300) frame(&g);
    ASSERT_TRUE(ambient_active(&g.a));
    while (ambient_active(&g.a) && i++ < 600) frame(&g);
    ASSERT_TRUE(!ambient_active(&g.a));
    ASSERT_EQ(g.s.facing, 4);
}

TEST(poison_overrides_idle_emote) {
    Rig g;
    rig_init(&g, 0);
    g.c.poisoned = true;
    g.c.mood = 100;
    for (int i = 0; i < 240; i++) frame(&g);
    ASSERT_TRUE(ambient_active(&g.a));
    ASSERT_EQ(g.a.player.emote, 12);
}

TEST(no_cry_no_mood) {
    Rig g;
    rig_init(&g, 0);
    g.c.mood = 100;
    for (int i = 0; i < 240; i++) frame(&g);
    ASSERT_TRUE(ambient_active(&g.a));
    for (int i = 0; i < 400 && ambient_active(&g.a); i++) {
        frame(&g);
        ASSERT_TRUE(!g.a.player.want_cry);
    }
    ASSERT_TRUE(!ambient_active(&g.a));
    ASSERT_TRUE(!g.a.player.want_cry);
    ASSERT_EQ(g.a.player.mood_delta, 0);
}

void run_follower_ambient_tests(void) {
    RUN_TEST(idle_fires_after_still);
    RUN_TEST(step_aborts_and_resets);
    RUN_TEST(talk_blocks_idle);
    RUN_TEST(trainer_delay);
    RUN_TEST(hidden_drops_event);
    RUN_TEST(item_ball_no_question);
    RUN_TEST(cooldown);
    RUN_TEST(zz_once);
    RUN_TEST(zz_not_back_to_back);
    RUN_TEST(idle_keeps_last_facing);
    RUN_TEST(poison_overrides_idle_emote);
    RUN_TEST(no_cry_no_mood);
}
