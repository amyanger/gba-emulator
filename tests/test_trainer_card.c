#include "test_harness.h"
#include "game_fixture.h"
#include "game/trainer_card.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x8000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define PARTY 0x02000400u

static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules,
    .battle_type_flags = 0x02000E10, .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08, .battler_positions = 0x02000E04,
    .battle_mons = 0x02001000, .player_party_count = 0x02000000,
    .player_party = PARTY, .enemy_party = 0x02000800,
    .main_in_battle = 0x03000010, .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .species_names = 0x08000100, .species_count = 4,
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

/* Static: GameState is a few KB. No game_init or game_update: the card reads memory itself. */
static GameState* state(void) {
    static GameState gs;
    memset(&gs, 0, sizeof(gs));
    gs.profile = &k_profile;
    gs.support = GAME_SUPPORT_OK;
    return &gs;
}

/* ASCII uppercase -> Gen 3, 0xFF-terminated. */
static void put_str(uint32_t addr, const char* s) {
    uint8_t* p = at(addr);
    for (; *s; s++, p++) *p = (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}

/* flags: byte 0x13, 0x02 hasSpecies, | 0x04 egg. */
static void put_mon(int slot, uint32_t pid, uint16_t species, uint8_t flags) {
    uint8_t plain[4][12];
    fixture_plain_mon(plain, species);
    if (flags & 0x04) plain[3][7] |= 0x40;   /* isEgg bit 30 of the IV word */
    fixture_encode_mon(at(PARTY + 100u * (uint32_t)slot), pid, 0x0000BEEF, plain, flags);
    at(PARTY + 100u * (uint32_t)slot)[0x54] = 20;   /* level */
}

static void build(uint8_t party_count) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    put_str(SB2, "BRENDAN");
    fixture_put32(at(SB2 + 0x0A), 0x59CDEE45);          /* trainer id (unaligned) */
    fixture_put16(at(SB2 + 0x0E), 23);                  /* 23:56:56 and 0 frames */
    at(SB2 + 0x10)[0] = 56; at(SB2 + 0x11)[0] = 56; at(SB2 + 0x12)[0] = 0;
    put_str(0x08000100 + 11 * 1, "BULBASAUR");
    put_str(0x08000100 + 11 * 2, "ZIGZAGOON");
    at(0x02000000)[0] = party_count;
    for (uint8_t i = 0; i < party_count; i++) put_mon(i, 0x1000u + i, (uint16_t)(1 + (i & 1)), 0x02);
}

static TrainerCardData s_d;

TEST(trainer_card_read_fresh_state) {
    build(2);
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_EQ(s_d.count, 2);
    ASSERT_EQ_HEX(s_d.public_id, 0xEE45);
    ASSERT_STR_EQ(s_d.name, "BRENDAN");
    ASSERT_EQ(s_d.play_frames, (uint32_t)((23 * 60 + 56) * 60 + 56) * 60);
    ASSERT_EQ(s_d.party[0].species_id, 1);
    ASSERT_EQ(s_d.party[1].species_id, 2);
    ASSERT_EQ_HEX(s_d.keys[1].pid, 0x1001);
    ASSERT_EQ_HEX(s_d.keys[1].ot, 0x0000BEEF);
}

TEST(trainer_card_read_not_supported) {
    build(2);
    GameMem m = mem();
    GameState* gs = state();
    gs->support = GAME_SUPPORT_MODIFIED;
    ASSERT_EQ(trainer_card_read(gs, &m, &s_d), CARD_NOT_SUPPORTED);
    gs = state();
    gs->profile = NULL;
    ASSERT_EQ(trainer_card_read(gs, &m, &s_d), CARD_NOT_SUPPORTED);
}

TEST(trainer_card_read_carries_game_and_region) {
    build(2);
    static GameProfile p;
    p = k_profile;
    p.rules = &g_frlg_rules;
    p.game_name = "Pok\xC3\xA9mon FireRed";
    GameState* gs = state();
    gs->profile = &p;
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(gs, &m, &s_d), CARD_OK);
    ASSERT_STR_EQ(s_d.game_name, "Pok\xC3\xA9mon FireRed");
    ASSERT_EQ(s_d.region, GAME_REGION_KANTO);
}

TEST(trainer_card_read_empty_party) {
    build(0);
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_NO_TEAM);
    build(2);
    at(PARTY + 0x1C)[0] ^= 0xFF;          /* both checksums fail */
    at(PARTY + 100 + 0x1C)[0] ^= 0xFF;
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_NO_TEAM);
    /* unreadable save blocks */
    build(2);
    fixture_put32(at(0x03000000), 0);
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_NO_TEAM);
}

TEST(trainer_card_bad_slot_keeps_index) {
    build(3);
    at(PARTY + 100 + 0x1C)[0] ^= 0xFF;    /* slot 1 bad: still listed, like the Party page */
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_EQ(s_d.count, 3);
    ASSERT_TRUE(s_d.party[1].bad);
    ASSERT_EQ(trainer_card_pick_ace(&s_d, 1, NULL), 0);
}

TEST(trainer_card_read_facility) {
    build(2);
    at(0x03000010)[0] = 0x02;                          /* in battle */
    at(0x02000E00)[0] = 2;
    fixture_put32(at(0x02000E10), (1u << 3) | (1u << 19));   /* trainer + factory */
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_FACILITY);
}

TEST(trainer_card_sig_changes_with_party) {
    build(2);
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    uint32_t sig = s_d.sig;
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_EQ_HEX(s_d.sig, sig);
    at(PARTY + 100 + 0x56)[0] ^= 0x01;   /* party HP byte, outside the checksum */
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_TRUE(s_d.sig != sig);
    sig = s_d.sig;
    fixture_put32(at(SB2 + 0x0A), 0x59CDEE46);
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_TRUE(s_d.sig != sig);
}

TEST(trainer_card_ace_order) {
    build(3);
    put_mon(0, 0x2000, 1, 0x02 | 0x04);   /* egg, then A, then B */
    GameMem m = mem();
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_TRUE(s_d.party[0].egg);
    NuzKey key_b = s_d.keys[2], unknown = { 0xDEAD, 0xBEEF, 0 }, key_egg = s_d.keys[0];
    ASSERT_EQ(trainer_card_pick_ace(&s_d, 2, NULL), 2);
    ASSERT_EQ(trainer_card_pick_ace(&s_d, 0, NULL), 1);    /* egg selection falls through */
    ASSERT_EQ(trainer_card_pick_ace(&s_d, 7, NULL), 1);
    ASSERT_EQ(trainer_card_pick_ace(&s_d, -1, &key_b), 2);
    ASSERT_EQ(trainer_card_pick_ace(&s_d, 1, &key_b), 1);  /* selection beats MVP */
    ASSERT_EQ(trainer_card_pick_ace(&s_d, -1, &unknown), 1);
    ASSERT_EQ(trainer_card_pick_ace(&s_d, -1, &key_egg), 1);
    for (int i = 1; i < 3; i++) put_mon(i, 0x2000u + (uint32_t)i, 1, 0x02 | 0x04);
    ASSERT_EQ(trainer_card_read(state(), &m, &s_d), CARD_OK);
    ASSERT_EQ(trainer_card_pick_ace(&s_d, -1, NULL), -1);
}

void run_trainer_card_tests(void) {
    RUN_TEST(trainer_card_read_fresh_state);
    RUN_TEST(trainer_card_read_not_supported);
    RUN_TEST(trainer_card_read_carries_game_and_region);
    RUN_TEST(trainer_card_read_empty_party);
    RUN_TEST(trainer_card_bad_slot_keeps_index);
    RUN_TEST(trainer_card_read_facility);
    RUN_TEST(trainer_card_sig_changes_with_party);
    RUN_TEST(trainer_card_ace_order);
}
