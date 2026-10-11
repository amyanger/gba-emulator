#include "test_harness.h"
#include "game_fixture.h"
#include "game/nuzlocke_read.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x8000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define STORAGE 0x02020000u

static const GameProfile k_profile = {
    .name = "test",
    .battle_type_flags = 0x02000E10, .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08, .battler_positions = 0x02000E04,
    .player_party_count = 0x02000000, .player_party = 0x02000400, .enemy_party = 0x02000800,
    .main_in_battle = 0x03000010, .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .map_header = 0x02000E20, .battle_outcome = 0x02000E30, .pokemon_storage_ptr = 0x03000008,
    .species_count = 8, .species_info = 0x08000C00, .experience_tables = 0x08000D00,
    .evolution_table = 0x08002000,
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

static void put_mon(uint8_t* raw, uint32_t pid, uint32_t ot, uint16_t species, uint8_t met,
                    uint32_t exp) {
    uint8_t plain[4][12];
    fixture_plain_mon(plain, species);
    fixture_put32(plain[0] + 4, exp);
    plain[3][1] = met;
    fixture_encode_mon(raw, pid, ot, plain, 0x02);
}

static void evo(uint16_t from, int slot, uint16_t to) {
    uint8_t* e = at(0x08002000 + (from * 5u + (uint32_t)slot) * 8u);
    fixture_put16(e, 4);
    fixture_put16(e + 2, 16);
    fixture_put16(e + 4, to);
}

static void build(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    fixture_put32(at(0x03000008), STORAGE);
    fixture_put32(at(SB2 + 0x0A), 0x12345678);           /* trainer id (unaligned) */
    fixture_put16(at(SB2 + 0x0E), 2);                    /* 2:03:04 and 5 frames */
    at(SB2 + 0x10)[0] = 3; at(SB2 + 0x11)[0] = 4; at(SB2 + 0x12)[0] = 5;
    at(0x02000E20 + 0x14)[0] = 17;                       /* mapsec */
    at(0x02000000)[0] = 2;
    put_mon(at(0x02000400), 0xAAAA0001, 0x12345678, 3, 16, 0);
    at(0x02000400 + 0x54)[0] = 9;
    fixture_put16(at(0x02000400 + 0x56), 20);
    put_mon(at(0x02000400 + 100), 0xAAAA0002, 0x12345678, 2, 17, 0);
    at(0x02000400 + 100 + 0x13)[0] = 0x03;               /* bad egg flag: checksum-failed slot */
    /* species 5 growth 0, level 4 needs 64 exp */
    at(0x08000C00 + 28 * 5 + 0x13)[0] = 0;
    for (uint32_t l = 1; l <= 100; l++) fixture_put32(at(0x08000D00 + 4u * l), l * l * l);
    put_mon(at(STORAGE + 4 + 80 * 7), 0xBBBB0003, 0x12345678, 5, 20, 70);
}

TEST(nuz_read_waits_for_save_blocks) {
    build();
    fixture_put32(at(0x03000004), 0);
    GameMem m = mem();
    static NuzFacts f;
    ASSERT_TRUE(!nuz_read(&k_profile, &m, false, &f));
    ASSERT_TRUE(!f.valid);
}

TEST(nuz_read_trainer_time_party_and_holes) {
    build();
    GameMem m = mem();
    static NuzFacts f;
    ASSERT_TRUE(nuz_read(&k_profile, &m, false, &f));
    ASSERT_EQ_HEX(f.trainer_id, 0x12345678);
    ASSERT_EQ(f.play_time, ((2u * 60 + 3) * 60 + 4) * 60 + 5);
    ASSERT_EQ(f.mapsec, 17);
    ASSERT_EQ(f.party_count, 2);
    ASSERT_EQ_HEX(f.party[0].key.pid, 0xAAAA0001);
    ASSERT_EQ(f.party[0].species, 3);
    ASSERT_EQ(f.party[0].met, 16);
    ASSERT_EQ(f.party[0].level, 9);
    ASSERT_EQ(f.party[0].hp, 20);
    ASSERT_EQ(f.party[1].species, 0);  /* bad slot left as an empty hole */
    ASSERT_TRUE(!f.has_boxes);
    ASSERT_TRUE(!f.in_battle);
    ASSERT_EQ(f.active_party, -1);
}

TEST(nuz_read_boxes_with_level_from_exp) {
    build();
    GameMem m = mem();
    static NuzFacts f;
    ASSERT_TRUE(nuz_read(&k_profile, &m, true, &f));
    ASSERT_TRUE(f.has_boxes);
    ASSERT_EQ(f.box_count, 1);
    ASSERT_EQ_HEX(f.boxes[0].key.pid, 0xBBBB0003);
    ASSERT_EQ(f.boxes[0].met, 20);
    ASSERT_EQ(f.boxes[0].level, 4);  /* 64 <= 70 < 125 */
    ASSERT_TRUE(nuz_read(&k_profile, &m, false, &f));
    ASSERT_TRUE(!f.has_boxes);
    ASSERT_EQ(f.box_count, 1);  /* kept between box reads */
}

TEST(nuz_read_battle_enemy_and_active_slot) {
    build();
    at(0x03000010)[0] = 0x02;
    fixture_put32(at(0x02000E10), 1u << 7);
    at(0x02000E30)[0] = 7;
    put_mon(at(0x02000800), 0xCCCC0004, 0x12345678, 4, 0, 0);
    at(0x02000800 + 0x54)[0] = 3;
    at(0x02000E00)[0] = 2;
    at(0x02000E04)[0] = 1; at(0x02000E04)[1] = 0;      /* battler 1 is the player */
    fixture_put16(at(0x02000E08 + 2), 1);
    GameMem m = mem();
    static NuzFacts f;
    ASSERT_TRUE(nuz_read(&k_profile, &m, false, &f));
    ASSERT_TRUE(f.in_battle);
    ASSERT_EQ_HEX(f.battle_flags, 1u << 7);
    ASSERT_EQ(f.battle_outcome, 7);
    ASSERT_EQ(f.enemy_species, 4);
    ASSERT_EQ(f.enemy_level, 3);
    ASSERT_EQ_HEX(f.enemy_pid, 0xCCCC0004);
    ASSERT_EQ(f.active_party, 1);
}

TEST(nuz_families_follow_evolution_lines) {
    build();
    evo(1, 0, 2);
    evo(2, 0, 3);
    evo(4, 0, 5);
    evo(4, 1, 6);
    GameMem m = mem();
    static uint16_t fam[NUZ_SPECIES_MAX];
    ASSERT_TRUE(nuz_families(&k_profile, &m, fam));
    ASSERT_EQ(fam[3], 1);
    ASSERT_EQ(fam[2], 1);
    ASSERT_EQ(fam[1], 1);
    ASSERT_EQ(fam[6], 4);
    ASSERT_EQ(fam[5], 4);
    ASSERT_EQ(fam[7], 7);
}

TEST(nuz_shiny_and_time_format) {
    ASSERT_TRUE(nuz_is_shiny(0x12345678, 0x12345678));
    ASSERT_TRUE(!nuz_is_shiny(0x00000000, 0x12345678));
    char buf[16];
    nuz_format_time(((1u * 60 + 2) * 60 + 3) * 60 + 59, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1:02:03");
}

void run_nuzlocke_read_tests(void) {
    RUN_TEST(nuz_read_waits_for_save_blocks);
    RUN_TEST(nuz_read_trainer_time_party_and_holes);
    RUN_TEST(nuz_read_boxes_with_level_from_exp);
    RUN_TEST(nuz_read_battle_enemy_and_active_slot);
    RUN_TEST(nuz_families_follow_evolution_lines);
    RUN_TEST(nuz_shiny_and_time_format);
}
