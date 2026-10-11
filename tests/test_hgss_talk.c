#include "test_harness.h"
#include "hgss/hgss_talk.h"

static uint8_t s_rules[HGSS_RULES][HGSS_RULE_SIZE];

static void set_result(uint8_t* e, uint16_t id) { uint16_t v = (uint16_t)(id << 6); e[0x0A] = (uint8_t)v; e[0x0B] = (uint8_t)(v >> 8); }
static uint32_t rand_zero(void* ud) { (void)ud; return 0; }
static uint32_t rand_99(void* ud) { (void)ud; return 99; }
static bool ok_all(uint16_t r, void* ud) { (void)r; (void)ud; return true; }
static bool ok_not_7(uint16_t r, void* ud) { (void)ud; return r != 7; }

static void reset(void) {
    memset(s_rules, 0, sizeof(s_rules));
    set_result(s_rules[99], 254);
    s_rules[99][0x11] = 100;
}

TEST(rule_fallback_is_99) {
    reset();
    HgssTalkCtx c;
    memset(&c, 0, sizeof(c));
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
}

TEST(rule_scan_order_global_head_then_local) {
    reset();
    HgssTalkCtx c;
    memset(&c, 0, sizeof(c));
    set_result(s_rules[0], 1);  s_rules[0][0x11] = 100;   /* local */
    set_result(s_rules[42], 2); s_rules[42][0x11] = 100;  /* global tail */
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 0);
    set_result(s_rules[41], 3); s_rules[41][0x11] = 100;  /* global head beats local */
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 41);
}

TEST(rule_chance_and_filter) {
    reset();
    HgssTalkCtx c;
    memset(&c, 0, sizeof(c));
    set_result(s_rules[30], 7); s_rules[30][0x11] = 50;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 30);
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_99, ok_all, NULL), 99);      /* 99 % 100 >= 50 */
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_not_7, NULL), 99);  /* filtered script */
}

TEST(rule_conditions) {
    reset();
    HgssTalkCtx c;
    memset(&c, 0, sizeof(c));
    uint8_t* e = s_rules[30];
    set_result(e, 5); e[0x11] = 100;
    e[0x12] = 1;                                            /* event flag: never set in Emerald */
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
    e[0x12] = 0;
    e[1] = 0x20;                                            /* friendship band 2: 200..254 */
    c.c[HC_FRIEND] = 199;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
    c.c[HC_FRIEND] = 200;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 30);
    e[4] = 10;                                              /* type 10 */
    c.c[HC_TYPE1] = 3; c.c[HC_TYPE2] = 10;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 30);
    c.c[HC_TYPE2] = 3;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
    e[4] = 0;
    e[0x0C] = 5; e[0x0D] = 0;                               /* map id 4 */
    c.map_id = 0xFFFF;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
    e[0x0C] = 0;
    e[0x10] = 0x08;                                         /* dead bit rejects */
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
    e[0x10] = 0;
    e[6] = 0xFB;                                            /* species class <= 130 */
    c.c[HC_SPECIES_CLASS] = 130;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 30);
    c.c[HC_SPECIES_CLASS] = 131;
    ASSERT_EQ(hgss_rule_pick(&s_rules[0][0], &c, rand_zero, ok_all, NULL), 99);
}

TEST(bands) {
    ASSERT_TRUE(hgss_mood_band(1, 127));
    ASSERT_TRUE(!hgss_mood_band(1, 126));
    ASSERT_TRUE(hgss_mood_band(5, -29));
    ASSERT_TRUE(hgss_mood_band(8, -127));
    ASSERT_TRUE(hgss_mood_band(10, -1));
    ASSERT_TRUE(!hgss_mood_band(9, -1));
    ASSERT_TRUE(hgss_friend_band(1, 255));
    ASSERT_TRUE(hgss_friend_band(7, 1));
    ASSERT_TRUE(hgss_friend_band(8, 0));
    ASSERT_TRUE(hgss_friend_band(10, 59));
    ASSERT_TRUE(!hgss_friend_band(10, 60));
}

TEST(script_and_anim_parse) {
    uint8_t s[HGSS_SCRIPT_SIZE];
    memset(s, 0, sizeof(s));
    s[0] = 2; s[2] = 197; s[4] = 0x4B; s[5] = 0x09; s[6] = 13; s[7] = 3;   /* anim 2, msg 197, sound 2379, emote 13, wait 3 */
    s[8] = 0xFF; s[9] = 0xFF;                                              /* end */
    s[0x31] = (uint8_t)10;
    HgssTalkScript t;
    ASSERT_TRUE(hgss_script_parse(s, sizeof(s), &t));
    ASSERT_EQ(t.step_count, 1);
    ASSERT_EQ(t.step[0].msg, 197);
    ASSERT_EQ(t.step[0].sound, HGSS_SOUND_CRY);
    ASSERT_EQ(t.mood_delta, 10);
    ASSERT_TRUE(!t.yes_no);
    s[0x28] = 1;
    ASSERT_TRUE(hgss_script_parse(s, sizeof(s), &t));
    ASSERT_TRUE(t.yes_no);
    ASSERT_TRUE(!hgss_script_parse(s, 40, &t));
    uint8_t a[HGSS_ANIM_SIZE];
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = 2; a[3] = 5; a[5] = 1;      /* frame 0: keep facing, 2 ticks, dy 5, sound */
    a[8] = 0xFF;
    HgssAnimFrame f[HGSS_ANIM_FRAMES];
    ASSERT_EQ(hgss_anim_parse(a, sizeof(a), f), 1);
    ASSERT_EQ(f[0].dy, 5);
    ASSERT_TRUE(f[0].sound);
}

void run_hgss_talk_tests(void) {
    RUN_TEST(rule_fallback_is_99);
    RUN_TEST(rule_scan_order_global_head_then_local);
    RUN_TEST(rule_chance_and_filter);
    RUN_TEST(rule_conditions);
    RUN_TEST(bands);
    RUN_TEST(script_and_anim_parse);
}
