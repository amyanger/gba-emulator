#include "game/hunt_read.h"
#include "game/pokemon.h"
#include "game_fixture.h"
#include "test_harness.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x4000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define PARTY 0x02000400u
#define ENEMY 0x02000800u
#define SPECIES_INFO 0x08001000u
#define HEADERS 0x08002000u
#define LAND_INFO 0x08002100u
#define LAND_MONS 0x08002110u
#define TILE_BITS 0x08003000u
#define PREV_BEHAVIOR 0x02000E70u
#define IMMUNITY 0x02000E72u
#define MATCH_CALL 0x02000E80u

#define CB2_OVERWORLD 0x08000101u
#define CB2_WILD 0x08000201u
#define CB2_SAFARI 0x08000301u
#define CB2_SCRIPTED 0x08000401u

/* pret include/constants/abilities.h, items.h, battle.h */
#define ABIL_SAND_VEIL 8
#define ABIL_ILLUMINATE 35
#define ABIL_SYNCHRONIZE 28
#define ITEM_CLEANSE_TAG 190
#define BATTLE_TYPE_ROAMER (1u << 10)
#define BATTLE_TYPE_PIKE (1u << 20)

static const GameProfile k_profile = {
    .name = "test",
    .battle_type_flags = 0x02000E10,
    .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08,
    .battler_positions = 0x02000E04,
    .player_party_count = 0x02000000,
    .player_party = PARTY,
    .enemy_party = ENEMY,
    .main_in_battle = 0x03000010,
    .save_block1_ptr = 0x03000000,
    .save_block2_ptr = 0x03000004,
    .map_header = 0x02000E20,
    .battle_outcome = 0x02000E30,
    .pokemon_storage_ptr = 0x03000008,
    .species_count = 16,
    .species_info = SPECIES_INFO,
    .wild_mon_headers = HEADERS,
    .wild_header_cap = 4,
    .main_callback2 = 0x03000034,
    .cb2_overworld = CB2_OVERWORLD,
    .rng_value = 0x03000020,
    .rand_count = 0x02000E40,
    .main_saved_callback = 0x03000038,
    .cb2_end_wild_battle = CB2_WILD,
    .cb2_end_safari_battle = CB2_SAFARI,
    .cb2_end_scripted_wild_battle = CB2_SCRIPTED,
    .roamer_location = 0x02000E50,
    .player_avatar_flags = 0x02000E60,
    .roamer_sb1_offset = 0x31DC,
    .outbreak_sb1_offset = 0x2B90,
    .safari_flag = 0x88C,
    .repel_var = 0x4021,
    .altering_cave_map = {24, 106},
    .tile_bit_attributes = TILE_BITS,
    .prev_metatile_behavior = PREV_BEHAVIOR,
    .wild_immunity_steps = IMMUNITY,
    .friendship_step_var = 0x402A,
    .poison_step_var = 0x402B,
    .match_call_flag = 0x12F,
    .daycare_sb1_offset = 0x3030,
    .match_call_state = MATCH_CALL,
};

static uint8_t* at(uint32_t addr) {
    if (addr >= 0x08000000)
        return s_rom + (addr - 0x08000000);
    if (addr >= 0x03000000)
        return s_iwram + (addr - 0x03000000);
    return s_ewram + (addr - 0x02000000);
}

static GameMem mem(void) {
    GameMem m = {s_ewram, s_iwram, s_rom, sizeof(s_rom)};
    return m;
}

static void species(uint16_t sp, uint8_t t1, uint8_t t2, uint8_t ratio, uint8_t a1, uint8_t a2) {
    uint8_t* si = at(SPECIES_INFO + 28u * sp);
    si[0x06] = t1;
    si[0x07] = t2;
    si[0x10] = ratio;
    si[0x16] = a1;
    si[0x17] = a2;
}

/* Party slot i: species, level, hp, held item; abilityNum 1 from fixture_plain_mon. */
static void party_mon(int i, uint32_t pid, uint16_t sp, uint8_t level, uint16_t hp, uint16_t item,
                      uint8_t flags) {
    uint8_t plain[4][12];
    fixture_plain_mon(plain, sp);
    fixture_put16(plain[0] + 2, item);
    uint8_t* raw = at(PARTY + 100u * (uint32_t)i);
    fixture_encode_mon(raw, pid, 0x59CDEE45u, plain, flags);
    raw[0x54] = level;
    fixture_put16(raw + 0x56, hp);
}

static void set_flag(uint16_t flag) {
    at(SB1 + 0x1270 + flag / 8)[0] |= (uint8_t)(1u << (flag % 8));
}

/* Route 101 shaped: map 0.16, rate 20, 12 slots of species 1-3; lead species 4 lv 30. */
static void build(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    fixture_put32(at(SB2 + 0x0A), 0x59CDEE45u);
    at(SB1 + 0x04)[0] = 0;
    at(SB1 + 0x05)[0] = 16;
    fixture_put32(at(0x03000020), 0x6226B90Fu);
    fixture_put32(at(0x02000E40), 2349);
    fixture_put32(at(0x03000034), CB2_OVERWORLD);
    at(0x02000E60)[0] = 0x01; /* on foot */
    /* sTileBitAttributes (metatile_behavior.c): MB_TALL_GRASS 2 = UNUSED | HAS_ENCOUNTERS,
     * MB_SHORT_GRASS 7 = UNUSED, MB_POND_WATER 16 = UNUSED | SURFABLE | HAS_ENCOUNTERS */
    at(TILE_BITS + 2)[0] = 0x05;
    at(TILE_BITS + 7)[0] = 0x04;
    at(TILE_BITS + 16)[0] = 0x07;
    fixture_put16(at(PREV_BEHAVIOR), 2);
    at(IMMUNITY)[0] = 4;

    species(1, 6, 6, 127, 1, 0);
    species(2, 13, 0, 0, 2, 3);
    species(3, 8, 8, 255, 4, 0);
    species(4, 11, 11, 127, 50, ABIL_ILLUMINATE);
    at(HEADERS)[0] = 0;
    at(HEADERS)[1] = 16;
    fixture_put32(at(HEADERS + 4), LAND_INFO);
    at(HEADERS + 20)[0] = 0xFF;
    at(LAND_INFO)[0] = 20;
    fixture_put32(at(LAND_INFO + 4), LAND_MONS);
    for (uint32_t i = 0; i < 12; i++) {
        uint8_t* e = at(LAND_MONS + 4 * i);
        e[0] = 2;
        e[1] = (uint8_t)(3 + i % 2);
        fixture_put16(e + 2, (uint16_t)(1 + i % 3));
    }
    at(0x02000000)[0] = 2;
    party_mon(0, 0x00000007u, 4, 30, 50, 0, 0x02);
    party_mon(1, 0x00000008u, 1, 12, 20, 0, 0x02);
}

static void read(NuzFacts* nf, HuntFacts* hf) {
    GameMem m = mem();
    nuz_read(&k_profile, &m, false, nf);
    hunt_read(&k_profile, &m, nf, true, hf);
}

static void start_battle(uint32_t saved_cb, uint32_t flags) {
    at(0x03000010)[0] = 0x02;
    fixture_put32(at(0x02000E10), flags);
    fixture_put32(at(0x03000038), saved_cb);
}

TEST(hunt_read_rng_and_count) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    read(&nf, &hf);
    ASSERT_TRUE(hf.valid);
    ASSERT_EQ_HEX(hf.rng, 0x6226B90Fu);
    ASSERT_EQ(hf.rand_count, 2349);
    ASSERT_EQ_HEX(hf.otid, 0x59CDEE45u);
    ASSERT_EQ(hf.map_group, 0);
    ASSERT_EQ(hf.map_num, 16);
    ASSERT_TRUE(hf.on_overworld);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_NONE);

    fixture_put32(at(0x03000004), 0); /* save blocks moving */
    read(&nf, &hf);
    ASSERT_TRUE(!hf.valid);
    ASSERT_TRUE(!hf.predictable);
}

TEST(hunt_read_fills_wild_input) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    ASSERT_EQ(hf.wild.method, WILD_LAND);
    ASSERT_EQ(hf.wild.slots[2].species, 3);
    ASSERT_EQ(hf.wild.slots[2].min_level, 2);
    ASSERT_EQ(hf.wild.slots[2].max_level, 3);
    ASSERT_EQ(hf.wild.slots[2].type1, 8);
    ASSERT_EQ(hf.wild.slots[2].gender_ratio, 255);
    ASSERT_TRUE(!hf.wild.slots[0].two_abilities);
    ASSERT_TRUE(hf.wild.slots[1].two_abilities);
    ASSERT_EQ(hf.wild.slots[1].type1, 13);
    ASSERT_TRUE(hf.wild.lead_ok);
    ASSERT_EQ(hf.wild.lead_ability, ABIL_ILLUMINATE); /* abilityNum 1 */
    ASSERT_EQ(hf.wild.lead_level, 30);
    ASSERT_EQ(hf.wild.lead_nature, 7);
    ASSERT_EQ(hf.wild.lead_gender, MON_GENDER_FEMALE); /* 127 > (pid & 0xFF) */
    ASSERT_EQ(hf.wild.rate, 20 * 16 * 2);              /* Illuminate x2 */
    ASSERT_TRUE(!hf.wild.metatile_roll);
    ASSERT_TRUE(!hf.wild.roamer_here);
    ASSERT_TRUE(!hf.wild.repel_active);
    ASSERT_EQ_HEX(hf.wild.otid, 0x59CDEE45u);

    /* Mach bike x0.8, then White Flute x1.5, then Cleanse Tag x2/3, then Illuminate x2 */
    at(0x02000E60)[0] = 0x02;
    set_flag(0x8AD);
    party_mon(0, 0x00000007u, 4, 30, 50, ITEM_CLEANSE_TAG, 0x02);
    read(&nf, &hf);
    ASSERT_EQ(hf.wild.rate, (((320 * 80 / 100) * 3 / 2) * 2 / 3) * 2);

    /* Roamer active on this map */
    fixture_put16(at(SB1 + 0x31DC + 8), 408);
    at(SB1 + 0x31DC + 0x0C)[0] = 40;
    at(SB1 + 0x31DC + 0x13)[0] = 1;
    at(0x02000E50)[0] = 0;
    at(0x02000E50)[1] = 16;
    read(&nf, &hf);
    ASSERT_TRUE(hf.wild.roamer_here);
    ASSERT_EQ(hf.wild.roamer_species, 408);
    ASSERT_EQ(hf.wild.roamer_level, 40);
    at(0x02000E50)[1] = 17;
    read(&nf, &hf);
    ASSERT_TRUE(!hf.wild.roamer_here);
}

TEST(hunt_read_repel_and_egg_lead) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    /* Fainted egg lead: no lead effects, Repel compares with slot 1 (first with HP, not an egg). */
    party_mon(0, 0x00000007u, 4, 5, 0, 0, 0x06);
    fixture_put16(at(SB1 + 0x139C + 2 * 0x21), 100);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    ASSERT_TRUE(!hf.wild.lead_ok);
    ASSERT_EQ(hf.wild.rate, 320); /* no Illuminate from an egg */
    ASSERT_TRUE(hf.wild.repel_active);
    ASSERT_EQ(hf.wild.repel_level, 12);

    /* No Repel steps left */
    fixture_put16(at(SB1 + 0x139C + 2 * 0x21), 0);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.wild.repel_active);
}

static void set_var(uint16_t var, uint16_t v) {
    fixture_put16(at(SB1 + 0x139C + 2u * (uint32_t)(var - 0x4000)), v);
}

/* An Egg in party slot i with `cycles` steps-to-hatch cycles (the friendship byte). */
static void egg_mon(int i, uint8_t cycles) {
    uint8_t plain[4][12];
    fixture_plain_mon(plain, 1);
    plain[0][0x9] = cycles;
    fixture_encode_mon(at(PARTY + 100u * (uint32_t)i), 0x00000009u, 0x59CDEE45u, plain, 0x06);
}

TEST(hunt_read_friendship_step_not_predicted) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    set_var(0x402A, 126);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    /* The next step is the 128th: friendship rolls for every party mon. */
    set_var(0x402A, 127);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Walk one more step to predict");
    set_var(0x402A, 0);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
}

TEST(hunt_read_poison_faint_step) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    at(PARTY + 100 + 0x50)[0] = 0x08; /* slot 1 poisoned, 20 HP */
    set_var(0x402B, 3);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable); /* loses 1 HP, the wild check still runs */
    fixture_put16(at(PARTY + 100 + 0x56), 1);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Walk one more step to predict");
    set_var(0x402B, 2);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
}

TEST(hunt_read_daycare_steps) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    uint32_t dc = SB1 + 0x3030;
    at(dc + 0x13)[0] = 0x02;
    at(dc + 0x8C + 0x13)[0] = 0x02;
    fixture_put32(at(dc + 0x8C + 0x88), 0x1FD);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    /* The second parent's next step is a 255th: the egg roll draws. */
    fixture_put32(at(dc + 0x8C + 0x88), 0x1FE);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Walk one more step to predict");
    fixture_put32(at(dc + 0x118), 0x1234); /* an egg already waits: no roll */
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);

    /* Hatch check: counter 254 -> 255 with an Egg out of cycles. */
    at(0x02000000)[0] = 3;
    egg_mon(2, 3);
    at(dc + 0x11C)[0] = 254;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    egg_mon(2, 0);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    at(dc + 0x11C)[0] = 253;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
}

TEST(hunt_read_match_call_and_repel_steps) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    at(MATCH_CALL + 6)[0] = 9;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable); /* no Match Call yet: the counter doesn't run */
    set_flag(0x12F);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Walk one more step to predict");
    at(MATCH_CALL + 6)[0] = 8;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);

    set_var(0x4021, 1);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Repel is wearing off");
    set_var(0x4021, 2);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    ASSERT_TRUE(hf.wild.repel_active);
}

TEST(hunt_read_classifies_saved_callback) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    start_battle(CB2_WILD, 0);
    read(&nf, &hf);
    ASSERT_TRUE(hf.in_battle);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_WILD);
    start_battle(CB2_WILD, BATTLE_TYPE_ROAMER);
    read(&nf, &hf);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_WILD);
    start_battle(CB2_WILD, BATTLE_TYPE_PIKE);
    read(&nf, &hf);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_OTHER);
    start_battle(CB2_SAFARI, 1u << 7);
    read(&nf, &hf);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_SAFARI);
    start_battle(CB2_SCRIPTED, 0);
    read(&nf, &hf);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_STATIC);
    start_battle(0x08000501u, 1u << 3);
    read(&nf, &hf);
    ASSERT_EQ(hf.battle_kind, HUNT_BATTLE_OTHER);
}

TEST(hunt_read_not_predictable_reasons) {
    static NuzFacts nf;
    static HuntFacts hf;

    build();
    start_battle(CB2_WILD, 0);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Not predicted in battle");

    build();
    at(0x02000E60)[0] = 0x08; /* PLAYER_AVATAR_FLAG_SURFING */
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Not predicted while surfing");

    build();
    set_flag(0x88C);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Not predicted in the Safari Zone");

    build();
    fixture_put16(at(SB1 + 0x2B90), 283); /* outbreak on another map: still predicted */
    at(SB1 + 0x2B92)[0] = 17;
    at(SB1 + 0x2B93)[0] = 0;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    at(SB1 + 0x2B92)[0] = 16;
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Not predicted during an outbreak");

    build();
    at(SB1 + 0x05)[0] = 17; /* no wild header for 0.17 */
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Not predicted here: no grass");

    build();
    fixture_put32(at(HEADERS + 4), 0); /* header with water only */
    read(&nf, &hf);
    ASSERT_STR_EQ(hf.why_not, "Not predicted here: no grass");

    build();
    at(PARTY + 0x13)[0] = 0x03; /* bad egg flag on the lead */
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Lead Pokemon unreadable");

    build();
    species(4, 11, 11, 127, 50, ABIL_SAND_VEIL);
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Not predicted with Sand Veil");

    build();
    species(4, 11, 11, 127, 50, ABIL_SYNCHRONIZE);
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "");
}

TEST(hunt_read_altering_cave_uses_var) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    at(SB1 + 0x04)[0] = 24;
    at(SB1 + 0x05)[0] = 106;
    for (uint32_t h = 0; h < 2; h++) {
        at(HEADERS + 20 * h)[0] = 24;
        at(HEADERS + 20 * h)[1] = 106;
        fixture_put32(at(HEADERS + 20 * h + 4), LAND_INFO + 0x80 * h);
        at(LAND_INFO + 0x80 * h)[0] = (uint8_t)(7 + h);
        fixture_put32(at(LAND_INFO + 0x80 * h + 4), LAND_MONS);
    }
    at(HEADERS + 40)[0] = 0xFF;
    read(&nf, &hf);
    ASSERT_EQ(hf.wild.rate, 7 * 16 * 2);
    fixture_put16(at(SB1 + 0x139C + 2 * 0x3E), 1); /* VAR_ALTERING_CAVE_WILD_SET */
    read(&nf, &hf);
    ASSERT_EQ(hf.wild.rate, 8 * 16 * 2);
}

TEST(hunt_read_enemy_incomplete_until_checksum) {
    build();
    static NuzFacts nf;
    static HuntFacts hf;
    start_battle(CB2_WILD, 0);
    read(&nf, &hf); /* gEnemyParty still zeroed */
    ASSERT_TRUE(hf.in_battle);
    ASSERT_TRUE(!hf.enemy_complete);

    uint8_t plain[4][12];
    fixture_plain_mon(plain, 2);
    fixture_encode_mon(at(ENEMY), 0x601534C5u, 0x59CDEE45u, plain, 0x02);
    at(ENEMY + 0x54)[0] = 3;
    at(ENEMY + 0x1C)[0] ^= 1; /* half written: checksum does not match yet */
    read(&nf, &hf);
    ASSERT_TRUE(!hf.enemy_complete);

    at(ENEMY + 0x1C)[0] ^= 1;
    read(&nf, &hf);
    ASSERT_TRUE(hf.enemy_complete);
    ASSERT_EQ(hf.enemy_species, 2);
    ASSERT_EQ_HEX(hf.enemy_pid, 0x601534C5u);
    ASSERT_EQ_HEX(hf.enemy_ot, 0x59CDEE45u);
    ASSERT_EQ(hf.enemy_level, 3);
}

/* CheckStandardWildEncounter (field_control_avatar.c:668) stores the step tile's behavior in
 * sPrevMetatileBehavior; StandardWildEncounter needs MetatileBehavior_IsLandWildEncounter. */
TEST(hunt_read_needs_grass_tile) {
    static NuzFacts nf;
    static HuntFacts hf;
    build();
    fixture_put16(at(PREV_BEHAVIOR), 7); /* short grass: no encounters */
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Step into tall grass to predict");
    fixture_put16(at(PREV_BEHAVIOR), 16); /* pond water has encounters but is surfable */
    read(&nf, &hf);
    ASSERT_STR_EQ(hf.why_not, "Step into tall grass to predict");
    fixture_put16(at(PREV_BEHAVIOR), 0x102); /* the game indexes the table with a u8 */
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);

    /* The 4 steps after a warp or a battle run no encounter check. */
    build();
    at(IMMUNITY)[0] = 3;
    read(&nf, &hf);
    ASSERT_TRUE(!hf.predictable);
    ASSERT_STR_EQ(hf.why_not, "Walk a few steps to predict");
    at(IMMUNITY)[0] = 4;
    read(&nf, &hf);
    ASSERT_TRUE(hf.predictable);
}

void run_hunt_read_tests(void) {
    RUN_TEST(hunt_read_rng_and_count);
    RUN_TEST(hunt_read_fills_wild_input);
    RUN_TEST(hunt_read_repel_and_egg_lead);
    RUN_TEST(hunt_read_classifies_saved_callback);
    RUN_TEST(hunt_read_not_predictable_reasons);
    RUN_TEST(hunt_read_altering_cave_uses_var);
    RUN_TEST(hunt_read_enemy_incomplete_until_checksum);
    RUN_TEST(hunt_read_needs_grass_tile);
    RUN_TEST(hunt_read_friendship_step_not_predicted);
    RUN_TEST(hunt_read_poison_faint_step);
    RUN_TEST(hunt_read_daycare_steps);
    RUN_TEST(hunt_read_match_call_and_repel_steps);
}
