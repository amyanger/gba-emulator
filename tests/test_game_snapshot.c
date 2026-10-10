#include "test_harness.h"
#include "game_fixture.h"
#include "game/game.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x4000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define KEY 0x0000BEEFu

static const GameProfile k_test_profile = {
    .name = "test",
    .battle_type_flags = 0x02000E10, .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08, .battler_positions = 0x02000E04,
    .battle_mons = 0x02000C00, .player_party_count = 0x02000000,
    .player_party = 0x02000400, .enemy_party = 0x02000800,
    .main_in_battle = 0x03000010, .battle_turn_counter = 0x03000014,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .map_header = 0x02000E20,
    .species_names = 0x08000100, .species_count = 4,
    .move_names = 0x08000200, .move_count = 4,
    .battle_moves = 0x08000300,
    .ability_names = 0x08000400, .ability_count = 4,
    .type_names = 0x08000500, .type_count = 18,
    .type_effectiveness = 0x08000600, .type_effectiveness_len = 12,
    .items = 0x08000700, .item_count = 13,
    .nature_name_ptrs = 0x08000A00,
    .species_info = 0x08000C00,
    .experience_tables = 0x08000D00,
    .species_to_national = 0x08001800,
    .wild_mon_headers = 0x08001900, .wild_header_cap = 200,
};

static GameMem test_mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

/* Static: GameState is a few KB. */
static GameState* test_state(void) {
    static GameState gs;
    memset(&gs, 0, sizeof(gs));
    gs.profile = &k_test_profile;
    return &gs;
}

static uint8_t* at(uint32_t addr) {
    if (addr >= 0x08000000) return s_rom + (addr - 0x08000000);
    if (addr >= 0x03000000) return s_iwram + (addr - 0x03000000);
    return s_ewram + (addr - 0x02000000);
}

/* ASCII uppercase/space -> Gen 3, 0xFF-terminated. */
static void put_str(uint32_t addr, const char* s) {
    uint8_t* p = at(addr);
    for (; *s; s++, p++) *p = (*s == ' ') ? 0x00 : (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}

static void set_dex_caught(uint16_t national) {
    uint32_t byte = (national - 1u) / 8, bit = 1u << ((national - 1u) % 8);
    at(SB2 + 0x28)[byte] |= (uint8_t)bit;
    at(SB2 + 0x5C)[byte] |= (uint8_t)bit;
    at(SB1 + 0x988)[byte] |= (uint8_t)bit;
    at(SB1 + 0x3B24)[byte] |= (uint8_t)bit;
}

static void build_world(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    fixture_put32(at(SB2 + 0xAC), KEY);

    /* ROM tables */
    put_str(0x08000100 + 11 * 1, "BULBASAUR");
    put_str(0x08000100 + 11 * 2, "ZIGZAGOON");
    put_str(0x08000100 + 11 * 3, "WURMPLE");
    put_str(0x08000200 + 13 * 1, "WATER GUN");
    put_str(0x08000200 + 13 * 2, "GROWL");
    at(0x08000300 + 12 * 1)[1] = 40; at(0x08000300 + 12 * 1)[2] = 11;  /* Water Gun */
    at(0x08000300 + 12 * 2)[1] = 0;  at(0x08000300 + 12 * 2)[2] = 0;   /* Growl */
    put_str(0x08000400 + 13 * 1, "OVERGROW");
    put_str(0x08000400 + 13 * 2, "PICKUP");
    put_str(0x08000500 + 7 * 0, "NORMAL");
    put_str(0x08000500 + 7 * 10, "FIRE");
    put_str(0x08000500 + 7 * 11, "WATER");
    put_str(0x08000500 + 7 * 17, "DARK");
    const uint8_t eff[12] = {11, 10, 20, 10, 11, 5, 0xFF, 0xFF, 0, 0, 0, 0};
    memcpy(at(0x08000600), eff, sizeof(eff));
    put_str(0x08000700 + 44 * 4, "POKE BALL");
    put_str(0x08000700 + 44 * 12, "PREMIER BALL");
    for (int n = 0; n < 25; n++) fixture_put32(at(0x08000A00 + 4u * n), 0x08000B00);
    put_str(0x08000B00, "HARDY");
    /* SpeciesInfo: species 1 grass, catch 45, ratio 31, growth 3; species 2 fire, catch 45 */
    uint8_t* bulba = at(0x08000C00 + 28 * 1);
    bulba[6] = 12; bulba[7] = 3; bulba[8] = 45; bulba[0x10] = 31; bulba[0x13] = 3;
    bulba[0x16] = 1; bulba[0x17] = 2;  /* abilities: OVERGROW, PICKUP */
    uint8_t* zig = at(0x08000C00 + 28 * 2);
    zig[6] = 10; zig[7] = 10; zig[8] = 45; zig[0x10] = 127; zig[0x13] = 0; zig[0x16] = 2;
    /* exp table: growth 3, level 6 needs 1100 */
    fixture_put32(at(0x08000D00 + 4u * (3 * 101 + 6)), 1100);
    /* species -> national: 1->1, 2->263, 3->265 */
    fixture_put16(at(0x08001800 + 0), 1);
    fixture_put16(at(0x08001800 + 2), 263);
    fixture_put16(at(0x08001800 + 4), 265);
    /* wild header for map 0:16: land only, Zigzagoon in every slot */
    at(0x08001900)[0] = 0; at(0x08001900)[1] = 16;
    fixture_put32(at(0x08001904), 0x08001A00);
    at(0x08001914)[0] = 0xFF;
    fixture_put32(at(0x08001A04), 0x08001B00);
    for (int i = 0; i < 12; i++) {
        uint8_t* slot = at(0x08001B00 + 4u * i);
        slot[0] = 2; slot[1] = 3; fixture_put16(slot + 2, 2);
    }

    /* Overworld state: map 0:16, one party mon */
    at(SB1)[4] = 0; at(SB1)[5] = 16;
    at(0x02000000)[0] = 1;
    uint8_t plain[4][12];
    fixture_plain_mon(plain, 1);
    fixture_put16(plain[1] + 0, 1);  /* moves: Water Gun, Growl */
    fixture_put16(plain[1] + 2, 2);
    uint8_t* mon = at(0x02000400);
    fixture_encode_mon(mon, 25, 7, plain, 0x02);
    mon[0x08] = 0xBC; mon[0x09] = 0xFF;  /* nickname "B" */
    mon[0x54] = 5;
}

static void enter_wild_battle(void) {
    at(0x03000010)[0] = 0x02;          /* inBattle */
    at(0x02000E00)[0] = 2;
    at(0x02000E04)[0] = 0; at(0x02000E04)[1] = 1;
    uint8_t* player = at(0x02000C00);
    fixture_put16(player + 0x00, 1);
    fixture_put16(player + 0x0C, 1);
    fixture_put16(player + 0x0E, 2);
    uint8_t* enemy = at(0x02000C00 + 0x58);
    fixture_put16(enemy + 0x00, 2);
    fixture_put32(enemy + 0x14, 0x3FFFFFFF);
    enemy[0x20] = 2; enemy[0x21] = 10; enemy[0x22] = 10;
    fixture_put16(enemy + 0x28, 10);
    enemy[0x2A] = 3;
    fixture_put16(enemy + 0x2C, 20);
    fixture_put16(at(SB1 + 0x650), 4);                       /* Poke Ball */
    fixture_put16(at(SB1 + 0x652), (uint16_t)(5 ^ KEY));     /* qty 5, encrypted */
}

TEST(snapshot_without_profile_explains) {
    GameState gs;
    game_init(&gs, NULL, 0);
    GameMem m = test_mem();
    game_update_mem(&gs, &m);
    ASSERT_TRUE(!gs.snap.valid);
    ASSERT_STR_EQ(gs.snap.reason, "No game info for this ROM");
}

TEST(snapshot_waits_when_save_pointers_invalid) {
    build_world();
    GameState* gs = test_state();
    GameMem m = test_mem();
    fixture_put32(at(0x03000000), 0);
    game_update_mem(gs, &m);
    ASSERT_TRUE(!gs->snap.valid);
    ASSERT_STR_EQ(gs->snap.reason, "Waiting for save data");
    fixture_put32(at(0x03000000), 0x0203FFF0);  /* in EWRAM but block runs off the end */
    game_update_mem(gs, &m);
    ASSERT_TRUE(!gs->snap.valid);
}

TEST(snapshot_overworld_party_and_encounters) {
    build_world();
    set_dex_caught(263);
    static uint8_t before[0x40000];
    memcpy(before, s_ewram, sizeof(s_ewram));
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_TRUE(gs->snap.valid);
    ASSERT_EQ(gs->snap.context, GAME_CTX_OVERWORLD);
    ASSERT_EQ(gs->snap.party_count, 1);
    const GamePartyMon* p = &gs->snap.party[0];
    ASSERT_STR_EQ(p->species, "BULBASAUR");
    ASSERT_STR_EQ(p->nickname, "B");
    ASSERT_EQ(p->level, 5);
    ASSERT_STR_EQ(p->nature, "HARDY");
    ASSERT_STR_EQ(p->hp_type, "DARK");
    ASSERT_EQ(p->hp_power, 70);
    ASSERT_STR_EQ(p->ability, "PICKUP");   /* abilityNum 1 -> abilities[1] */
    ASSERT_EQ(p->exp_to_next, 100);
    ASSERT_TRUE(gs->snap.has_encounters);
    ASSERT_EQ(gs->snap.enc_count[ENC_LAND], 1);
    ASSERT_STR_EQ(gs->snap.enc[ENC_LAND][0].species, "ZIGZAGOON");
    ASSERT_EQ(gs->snap.enc[ENC_LAND][0].percent, 100);
    ASSERT_TRUE(gs->snap.enc[ENC_LAND][0].caught);
    ASSERT_MEM_EQ(before, s_ewram, sizeof(s_ewram));
}

TEST(snapshot_wild_battle_matchups_and_catch) {
    build_world();
    enter_wild_battle();
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.context, GAME_CTX_BATTLE_WILD);
    ASSERT_EQ(gs->snap.enemy_count, 1);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_STR_EQ(e->species, "ZIGZAGOON");
    ASSERT_EQ(e->hp, 10);
    ASSERT_EQ(e->max_hp, 20);
    ASSERT_STR_EQ(e->ability, "PICKUP");
    ASSERT_STR_EQ(e->type1, "FIRE");
    ASSERT_EQ(e->eff_count, 2);
    ASSERT_STR_EQ(e->eff[0].move, "WATER GUN");
    ASSERT_EQ(e->eff[0].quarters, 8);
    ASSERT_TRUE(e->eff[1].status_move);
    ASSERT_EQ(gs->snap.catch_count, 1);
    ASSERT_STR_EQ(gs->snap.catch_rows[0].ball, "POKE BALL");
    ASSERT_EQ(gs->snap.catch_rows[0].quantity, 5);
    ASSERT_EQ(gs->snap.catch_rows[0].permille, 123);
}

TEST(snapshot_trainer_battle_has_no_catch_rows) {
    build_world();
    enter_wild_battle();
    fixture_put32(at(0x02000E10), 1u << 3);
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.context, GAME_CTX_BATTLE_TRAINER);
    ASSERT_EQ(gs->snap.catch_count, 0);
}

TEST(snapshot_battle_flag_without_enemy_is_overworld) {
    build_world();
    enter_wild_battle();
    fixture_put16(at(0x02000C00 + 0x58), 0);  /* enemy species not loaded yet */
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.context, GAME_CTX_OVERWORLD);
    ASSERT_EQ(gs->snap.enemy_count, 0);
}

TEST(snapshot_out_of_range_ids_show_unknown) {
    build_world();
    enter_wild_battle();
    uint8_t* enemy = at(0x02000C00 + 0x58);
    enemy[0x20] = 200;                       /* ability */
    fixture_put16(enemy + 0x2E, 9999);       /* item */
    fixture_put16(at(0x02000C00 + 0x0C), 999); /* player move */
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_STR_EQ(gs->snap.enemies[0].ability, "???");
    ASSERT_STR_EQ(gs->snap.enemies[0].item, "???");
    ASSERT_STR_EQ(gs->snap.enemies[0].eff[0].move, "???");
    fixture_put16(enemy + 0x00, 0xFFFF);     /* species out of range: not an enemy */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.context, GAME_CTX_OVERWORLD);
}

TEST(snapshot_clamps_party_count) {
    build_world();
    at(0x02000000)[0] = 200;
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_TRUE(gs->snap.party_count <= 6);
}

TEST(snapshot_dump_writes_text) {
    build_world();
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    FILE* f = fopen("game_dump_test.txt", "w+");
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(game_dump(&gs->snap, f));
    rewind(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    remove("game_dump_test.txt");
    ASSERT_TRUE(strstr(buf, "BULBASAUR") != NULL);
    ASSERT_TRUE(strstr(buf, "ZIGZAGOON") != NULL);
}

void run_game_snapshot_tests(void) {
    TEST_SUITE("game_snapshot");
    RUN_TEST(snapshot_without_profile_explains);
    RUN_TEST(snapshot_waits_when_save_pointers_invalid);
    RUN_TEST(snapshot_overworld_party_and_encounters);
    RUN_TEST(snapshot_wild_battle_matchups_and_catch);
    RUN_TEST(snapshot_trainer_battle_has_no_catch_rows);
    RUN_TEST(snapshot_battle_flag_without_enemy_is_overworld);
    RUN_TEST(snapshot_out_of_range_ids_show_unknown);
    RUN_TEST(snapshot_clamps_party_count);
    RUN_TEST(snapshot_dump_writes_text);
}
