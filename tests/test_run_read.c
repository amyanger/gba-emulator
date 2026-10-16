#include "test_harness.h"
#include "game_fixture.h"
#include "game/runlog_read.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x8000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define KEY 0x28B22ED4u
#define TRAINERS 0x08003000u
#define BATTLE_MONS 0x02001000u

static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules,
    .battle_type_flags = 0x02000E10, .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08, .battler_positions = 0x02000E04,
    .battle_mons = BATTLE_MONS, .battler_attacker = 0x02000E40,
    .trainer_battle_opponent_a = 0x02000E44,
    .main_in_battle = 0x03000010, .main_callback2 = 0x03000020, .cb2_whiteout = 0x08001235,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .species_count = 412, .species_to_national = 0x08000400,
    .trainers = TRAINERS, .trainer_count = 400,
};

static uint8_t* at(uint32_t addr) {
    if (addr >= 0x08000000) return s_rom + (addr - 0x08000000);
    if (addr >= 0x03000000) return s_iwram + (addr - 0x03000000);
    return s_ewram + (addr - 0x02000000);
}

static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

static void set_flag(uint16_t id) { at(SB1 + 0x1270 + (id >> 3))[0] |= (uint8_t)(1u << (id & 7)); }
static void set_var(uint16_t id, uint16_t v) { fixture_put16(at(SB1 + 0x139C + 2u * (id - 0x4000u)), v); }
static void set_stat(int i, uint32_t v) { fixture_put32(at(SB1 + 0x159C + 4u * (uint32_t)i), v ^ KEY); }

static void build(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    fixture_put32(at(SB2 + 0xAC), KEY);
    /* every stat stored as 0 ^ key, as the game does */
    for (int i = 0; i < 64; i++) set_stat(i, 0);
}

static void battle(uint32_t flags, uint8_t count) {
    at(0x03000010)[0] = 0x02;
    fixture_put32(at(0x02000E10), flags);
    at(0x02000E00)[0] = count;
}

TEST(run_read_stats_xor) {
    build();
    set_stat(RUN_STAT_STEPS, 3050);
    set_stat(RUN_STAT_TOTAL_BATTLES, 11);
    at(SB2)[0] = 0xBB;
    at(SB2 + 7)[0] = 0xFF;
    GameMem m = mem();
    static RunFacts f;
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(f.valid);
    ASSERT_EQ_HEX(f.sb1, SB1);
    ASSERT_EQ_HEX(f.sb2, SB2);
    ASSERT_EQ(f.stats[RUN_STAT_STEPS], 3050);
    ASSERT_EQ(f.stats[RUN_STAT_TOTAL_BATTLES], 11);
    ASSERT_EQ(f.stats[RUN_STAT_CAPTURES], 0);
    ASSERT_EQ(f.player_name[0], 0xBB);
    ASSERT_EQ(f.player_name[7], 0xFF);
}

TEST(run_read_badges_story) {
    build();
    set_flag(0x867);
    set_flag(0x86A);
    set_flag(0x7A);
    set_var(0x40CA, 2);
    set_var(0x4023, 1);
    GameMem m = mem();
    static RunFacts f;
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ_HEX(f.badges, 0x09);
    ASSERT_TRUE(f.story & (1u << RUN_STORY_SURF));
    ASSERT_TRUE(f.story & (1u << RUN_STORY_SOOTOPOLIS));
    ASSERT_TRUE(!(f.story & (1u << RUN_STORY_CHIMNEY)));
    ASSERT_TRUE(!(f.story & (1u << RUN_STORY_WEATHER)));
    ASSERT_EQ(f.starter, 1);

    set_var(0x40CA, 1);   /* Rayquaza not yet at Sootopolis */
    set_var(0x40B3, 1);
    set_flag(0x8B);
    set_flag(0x860);
    set_flag(0x861);
    set_flag(0x6F);
    set_flag(0x81);
    set_flag(0x107);
    set_flag(0x864);
    set_flag(0x896);
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(!(f.story & (1u << RUN_STORY_SOOTOPOLIS)));
    ASSERT_EQ_HEX(f.story, (1u << RUN_STORY_COUNT) - 1 - (1u << RUN_STORY_SOOTOPOLIS) -
                               (1u << RUN_STORY_CHAMPION));
}

TEST(run_read_champion_trainer_flag) {
    build();
    set_flag(0x500 + 335);
    GameMem m = mem();
    static RunFacts f;
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ_HEX(f.story, 1u << RUN_STORY_CHAMPION);
    ASSERT_TRUE(run_is_leader(&g_emerald_rules, 335));
    ASSERT_TRUE(run_is_leader(&g_emerald_rules, 261));
    ASSERT_TRUE(run_is_leader(&g_emerald_rules, 272));
    ASSERT_TRUE(!run_is_leader(&g_emerald_rules, 260));
    ASSERT_TRUE(!run_is_leader(&g_emerald_rules, 273));
    ASSERT_TRUE(!run_is_leader(&g_emerald_rules, 0));
}

TEST(run_read_dex_counts) {
    build();
    /* owned natdex 1, 5, 386; seen those plus 2 and 9 */
    at(SB2 + 0x28)[0] = 0x11;
    at(SB2 + 0x28 + 48)[0] = 0x02;
    at(SB2 + 0x5C)[0] = 0x13;
    at(SB2 + 0x5C + 1)[0] = 0x01;
    at(SB2 + 0x5C + 48)[0] = 0x02;
    fixture_put16(at(0x08000400 + 2 * 4), 5);   /* species 5 -> natdex 5 */
    fixture_put16(at(0x08000400 + 2 * 5), 6);   /* species 6 -> natdex 6 */
    GameMem m = mem();
    static RunFacts f;
    ASSERT_TRUE(run_read(&k_profile, &m, 5, &f));
    ASSERT_EQ(f.dex_owned, 3);
    ASSERT_EQ(f.dex_seen, 5);
    ASSERT_TRUE(f.enemy_owned);
    ASSERT_TRUE(run_read(&k_profile, &m, 6, &f));
    ASSERT_TRUE(!f.enemy_owned);
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(!f.enemy_owned);
}

static void trainer(uint16_t id, uint8_t party_flags, uint8_t size, uint32_t party) {
    uint8_t* t = at(TRAINERS + id * 0x28u);
    t[0] = party_flags;
    t[0x20] = size;
    fixture_put32(t + 0x24, party);
}

TEST(run_read_facility_flags) {
    build();
    GameMem m = mem();
    static RunFacts f;
    /* Wallace: custom moves, 16-byte entries, ace in slot 6 */
    trainer(335, 0x01, 6, 0x08007000);
    fixture_put16(at(0x08007000 + 5 * 16 + 4), 350);
    fixture_put16(at(0x08007000 + 5 * 8 + 4), 999);   /* where an 8-byte stride would look */
    fixture_put16(at(0x02000E44), 335);

    battle((1u << 3) | (1u << 19), 2);                 /* trainer + factory */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(f.facility);
    ASSERT_EQ(f.opponent, 0);
    ASSERT_EQ(f.ace_species, 0);

    battle(1u << 3, 2);                                /* plain trainer */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(!f.facility);
    ASSERT_EQ(f.opponent, 335);
    ASSERT_EQ(f.ace_species, 350);
    ASSERT_TRUE(f.opponent_leader);

    battle((1u << 3) | (1u << 22), 4);                 /* Steven multi: gated, still a trainer */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(f.facility);
    ASSERT_EQ(f.opponent, 335);
    ASSERT_TRUE(f.partner_battle);

    battle((1u << 3) | (1u << 27), 2);                 /* secret base: not a plain trainer */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.opponent, 0);

    battle(0, 2);                                      /* wild */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.opponent, 0);
    ASSERT_TRUE(!f.facility);

    /* bad party pointer / size -> no ace */
    battle(1u << 3, 2);
    trainer(335, 0x00, 7, 0x08007000);
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.ace_species, 0);
    trainer(335, 0x00, 6, 0x02000000);
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.ace_species, 0);
    fixture_put16(at(0x02000E44), 400);                /* past trainer_count */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.opponent, 0);

    /* Facility maps gate outside battle; the Frontier's outside area does not. */
    at(0x03000010)[0] = 0;
    at(SB1 + 4)[0] = 26; at(SB1 + 5)[0] = 33;          /* Battle Factory battle room */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.map_group, 26);
    ASSERT_EQ(f.map_num, 33);
    ASSERT_TRUE(f.facility);
    ASSERT_EQ(f.battlers, 0);
    at(SB1 + 4)[0] = 9; at(SB1 + 5)[0] = 2;            /* Slateport Battle Tent lobby */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(f.facility);
    at(SB1 + 4)[0] = 26; at(SB1 + 5)[0] = 4;           /* Battle Frontier outside west */
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(!f.facility);
}

TEST(run_read_battlers) {
    build();
    battle(0x01, 2);
    for (uint32_t b = 0; b < 2; b++) {
        uint8_t* bm = at(BATTLE_MONS + b * 0x58);
        fixture_put16(bm + 0x00, (uint16_t)(280 + b));
        fixture_put16(bm + 0x28, (uint16_t)(10 + b));
        bm[0x2A] = (uint8_t)(20 + b);
        fixture_put16(bm + 0x2C, (uint16_t)(40 + b));
        at(0x02000E04 + b)[0] = (uint8_t)b;
        fixture_put16(at(0x02000E08 + 2 * b), (uint16_t)(b ? 0 : 3));
    }
    at(0x02000E40)[0] = 1;
    fixture_put32(at(0x03000020), 0x08001235);
    GameMem m = mem();
    static RunFacts f;
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.battlers, 2);
    ASSERT_EQ(f.b[0].species, 280);
    ASSERT_EQ(f.b[0].hp, 10);
    ASSERT_EQ(f.b[0].max_hp, 40);
    ASSERT_EQ(f.b[0].level, 20);
    ASSERT_EQ(f.b[0].pos, 0);
    ASSERT_EQ(f.b[0].party_index, 3);
    ASSERT_EQ(f.b[1].species, 281);
    ASSERT_EQ(f.b[1].hp, 11);
    ASSERT_EQ(f.b[1].max_hp, 41);
    ASSERT_EQ(f.b[1].level, 21);
    ASSERT_EQ(f.b[1].pos, 1);
    ASSERT_EQ(f.b[1].party_index, 0);
    ASSERT_EQ(f.attacker, 1);
    ASSERT_EQ_HEX(f.callback2, 0x08001235);
    ASSERT_TRUE(f.whiteout);

    at(0x03000010)[0] = 0;                             /* stale count outside battle */
    fixture_put32(at(0x03000020), 0x08001001);
    ASSERT_TRUE(run_read(&k_profile, &m, 0, &f));
    ASSERT_EQ(f.battlers, 0);
    ASSERT_TRUE(!f.whiteout);
}

TEST(run_read_invalid) {
    build();
    fixture_put32(at(0x03000000), 0x03001000);
    GameMem m = mem();
    static RunFacts f;
    f.valid = true;
    ASSERT_TRUE(!run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(!f.valid);
    build();
    fixture_put32(at(0x03000004), 0x0203FFF0);         /* SB2 runs past EWRAM */
    ASSERT_TRUE(!run_read(&k_profile, &m, 0, &f));
    ASSERT_TRUE(!f.valid);
}

/* The story table, badge flag, starter var and masks come from the rules. */
TEST(run_read_story_ids_and_masks_from_rules) {
    build();
    static const RunStoryDef story[2] = {
        {STORY_FLAG, 0x830, 0, STORY_TEXT_PLAIN, 0, "a"},
        {STORY_VAR_AT_LEAST, 0x4050, 3, STORY_TEXT_PLAIN, 0, "b"},
    };
    static GameRules r;
    r = g_emerald_rules;
    r.story = story;
    r.story_count = 2;
    r.story_hof = 1;
    r.flag_badge01 = 0x820;
    r.var_starter = 0x4031;
    r.partner_otid = 0;
    r.run_facility_mask = 1u << 16;
    r.run_not_plain_mask = 1u << 19;
    r.partner_battle_mask = 0;
    r.leaders[0] = 335;
    r.league_count = 0;
    static GameProfile p;
    p = k_profile;
    p.rules = &r;
    set_flag(0x830);
    set_flag(0x822);
    set_var(0x4050, 3);
    set_var(0x4031, 2);
    GameMem m = mem();
    static RunFacts f;
    ASSERT_TRUE(run_read(&p, &m, 0, &f));
    ASSERT_EQ_HEX(f.story, 0x3);
    ASSERT_EQ_HEX(f.badges, 0x04);
    ASSERT_EQ(f.starter, 2);
    ASSERT_EQ(f.hof_story, 1);
    ASSERT_EQ(f.partner_otid, 0);
    set_var(0x4050, 2);
    ASSERT_TRUE(run_read(&p, &m, 0, &f));
    ASSERT_EQ_HEX(f.story, 0x1);

    trainer(335, 0x00, 1, 0x08007000);
    fixture_put16(at(0x08007000 + 4), 25);
    fixture_put16(at(0x02000E44), 335);
    battle((1u << 3) | (1u << 22), 2);       /* Emerald's partner bit means nothing here */
    ASSERT_TRUE(run_read(&p, &m, 0, &f));
    ASSERT_TRUE(!f.facility);
    ASSERT_TRUE(!f.partner_battle);
    ASSERT_EQ(f.opponent, 335);
    ASSERT_TRUE(f.opponent_leader);
    battle((1u << 3) | (1u << 19), 2);       /* not a plain trainer under these rules */
    ASSERT_TRUE(run_read(&p, &m, 0, &f));
    ASSERT_EQ(f.opponent, 0);
    battle(1u << 16, 2);                     /* party swapped */
    ASSERT_TRUE(run_read(&p, &m, 0, &f));
    ASSERT_TRUE(f.facility);
    ASSERT_TRUE(!run_is_leader(&r, 261));
}

void run_run_read_tests(void) {
    RUN_TEST(run_read_stats_xor);
    RUN_TEST(run_read_badges_story);
    RUN_TEST(run_read_champion_trainer_flag);
    RUN_TEST(run_read_dex_counts);
    RUN_TEST(run_read_facility_flags);
    RUN_TEST(run_read_battlers);
    RUN_TEST(run_read_invalid);
    RUN_TEST(run_read_story_ids_and_masks_from_rules);
}
