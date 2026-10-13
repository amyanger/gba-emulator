#include "test_harness.h"
#include "game_fixture.h"
#include "game/game.h"
#include "game/damage_read.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x10000];

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
    .move_names = 0x08000200, .move_count = 240,
    .battle_moves = 0x08000300,
    .ability_names = 0x08000400, .ability_count = 4,
    .type_names = 0x08000500, .type_count = 18,
    .type_effectiveness = 0x08000600, .type_effectiveness_len = 15,
    .items = 0x08000700, .item_count = 13,
    .nature_name_ptrs = 0x08000A00,
    .species_info = 0x08000C00,
    .experience_tables = 0x08000D00,
    .species_to_national = 0x08001800,
    .wild_mon_headers = 0x08001900, .wild_header_cap = 200,
    .region_map_entries = 0x08001C00, .region_map_count = 2,
    .berries = 0x08002000,
    /* damage calculator */
    .battle_weather = 0x02000E50, .side_statuses = 0x02000E54,
    .absent_battler_flags = 0x02000E58, .trainer_battle_opponent_a = 0x02000E5A,
    .battle_resources = 0x02000E5C, .statuses3 = 0x02000E60, .disable_structs = 0x02000F00,
    .stat_stage_ratios = 0x08006100,
    .hold_effect_to_type = 0x08006120, .hold_effect_to_type_count = 2,
    .flail_table = 0x08006140, .weight_table = 0x08006150,
    .pokedex_entries = 0x08008000, .pokedex_count = 387,
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
    at(0x08000300 + 12 * 2)[1] = 0;  at(0x08000300 + 12 * 2)[2] = 3;   /* Growl, stored type 3 */
    put_str(0x08000200 + 13 * 3, "MUD SLAP");
    at(0x08000300 + 12 * 3)[1] = 20; at(0x08000300 + 12 * 3)[2] = 4;   /* Ground */
    put_str(0x08000200 + 13 * 237, "HIDDEN POWER");
    at(0x08000300 + 12 * 237)[1] = 60; at(0x08000300 + 12 * 237)[2] = 0;  /* stored Normal */
    at(0x08000300 + 12 * 237)[0] = 135;                                  /* EFFECT_HIDDEN_POWER */
    put_str(0x08000200 + 13 * 4, "WEATHER BALL");
    at(0x08000300 + 12 * 4)[0] = 203; at(0x08000300 + 12 * 4)[1] = 50;  /* EFFECT_WEATHER_BALL, Normal */
    put_str(0x08000200 + 13 * 5, "THUNDERSHOCK");
    at(0x08000300 + 12 * 5)[0] = 6; at(0x08000300 + 12 * 5)[1] = 40; at(0x08000300 + 12 * 5)[2] = 13;
    put_str(0x08000200 + 13 * 6, "LOW KICK");
    at(0x08000300 + 12 * 6)[0] = 196; at(0x08000300 + 12 * 6)[1] = 1; at(0x08000300 + 12 * 6)[2] = 1;
    /* item 5: WATER_POWER (52) x1.1 */
    at(0x08000700 + 44 * 5)[0x12] = 52; at(0x08000700 + 44 * 5)[0x13] = 10;
    /* damage tables: stat stage ratios, two hold-effect types, Flail and Low Kick tables */
    const uint8_t ratios[26] = {10, 40, 10, 35, 10, 30, 10, 25, 10, 20, 10, 15, 10, 10,
                                15, 10, 20, 10, 25, 10, 30, 10, 35, 10, 40, 10};
    memcpy(at(0x08006100), ratios, sizeof(ratios));
    const uint8_t hold_types[4] = {52, 11, 58, 10};
    memcpy(at(0x08006120), hold_types, sizeof(hold_types));
    const uint8_t flail[12] = {1, 200, 4, 150, 9, 100, 16, 80, 32, 40, 48, 20};
    memcpy(at(0x08006140), flail, sizeof(flail));
    const uint16_t weight[12] = {100, 20, 250, 40, 500, 60, 1000, 80, 2000, 100, 0xFFFF, 0xFFFF};
    for (int i = 0; i < 12; i++) fixture_put16(at(0x08006150 + 2u * i), weight[i]);
    put_str(0x08000400 + 13 * 1, "OVERGROW");
    put_str(0x08000400 + 13 * 2, "PICKUP");
    put_str(0x08000500 + 7 * 0, "NORMAL");
    put_str(0x08000500 + 7 * 10, "FIRE");
    put_str(0x08000500 + 7 * 11, "WATER");
    put_str(0x08000500 + 7 * 17, "DARK");
    const uint8_t eff[15] = {11, 10, 20, 10, 11, 5, 17, 10, 20, 0xFF, 0xFF, 0, 0, 0, 0};
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

    /* region map sections: 0 unnamed, 1 "ROUTE ONE" */
    fixture_put32(at(0x08001C00 + 8 * 1 + 4), 0x08001D00);
    put_str(0x08001D00, "ROUTE ONE");
    at(0x02000E20 + 0x14)[0] = 1;

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
    fixture_put16(mon + 0x56, 17);  /* hp */
    fixture_put16(mon + 0x58, 23);  /* max hp */
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
    ASSERT_EQ(gs.snap.support, GAME_SUPPORT_OTHER_GAME);
    ASSERT_STR_EQ(gs.snap.reason, "Game not supported");

    memcpy(s_rom + 0xAC, "BPEE", 4);  /* Emerald header on a ROM that isn't the retail one */
    game_init(&gs, s_rom, sizeof(s_rom));
    game_update_mem(&gs, &m);
    ASSERT_TRUE(!gs.snap.valid);
    ASSERT_EQ(gs.snap.support, GAME_SUPPORT_MODIFIED);
    ASSERT_STR_EQ(gs.snap.reason, "ROM hack or modified ROM not supported");
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
    ASSERT_EQ(p->type_ids[0], 12);
    ASSERT_EQ(p->type_ids[1], 3);
    ASSERT_EQ(p->hp, 17);
    ASSERT_EQ(p->max_hp, 23);
    ASSERT_TRUE(gs->snap.has_encounters);
    ASSERT_EQ(gs->snap.enc_count[ENC_LAND], 1);
    ASSERT_STR_EQ(gs->snap.enc[ENC_LAND][0].species, "ZIGZAGOON");
    ASSERT_EQ(gs->snap.enc[ENC_LAND][0].percent, 100);
    ASSERT_TRUE(gs->snap.enc[ENC_LAND][0].caught);
    ASSERT_STR_EQ(gs->snap.map_name, "ROUTE ONE");
    ASSERT_MEM_EQ(before, s_ewram, sizeof(s_ewram));
}

TEST(snapshot_map_name_empty_when_section_unknown) {
    build_world();
    GameState* gs = test_state();
    GameMem m = test_mem();
    at(0x02000E20 + 0x14)[0] = 2;  /* past region_map_count */
    game_update_mem(gs, &m);
    ASSERT_STR_EQ(gs->snap.map_name, "");
    at(0x02000E20 + 0x14)[0] = 0;  /* null name pointer */
    game_update_mem(gs, &m);
    ASSERT_STR_EQ(gs->snap.map_name, "");
}

TEST(snapshot_wild_battle_matchups_and_catch) {
    build_world();
    enter_wild_battle();
    GameState* gs = test_state();
    GameMem m = test_mem();
    fixture_put16(at(0x02000E08), 0);  /* gBattlerPartyIndexes[0] */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.context, GAME_CTX_BATTLE_WILD);
    ASSERT_STR_EQ(gs->snap.active_name, "B");
    ASSERT_EQ(gs->snap.enemy_count, 1);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_STR_EQ(e->species, "ZIGZAGOON");
    ASSERT_EQ(e->hp, 10);
    ASSERT_EQ(e->max_hp, 20);
    ASSERT_STR_EQ(e->ability, "PICKUP");
    ASSERT_STR_EQ(e->type1, "FIRE");
    ASSERT_EQ(e->type_ids[0], 10);
    ASSERT_EQ(e->eff[0].type, 11);
    ASSERT_EQ(e->eff_count, 2);
    ASSERT_STR_EQ(e->eff[0].move, "WATER GUN");
    ASSERT_EQ(e->eff[0].quarters, 8);
    ASSERT_TRUE(e->eff[1].status_move);
    ASSERT_EQ(e->eff[1].type, 3);  /* power-0 move keeps its stored type */
    ASSERT_EQ(gs->snap.catch_count, 1);
    ASSERT_STR_EQ(gs->snap.catch_rows[0].ball, "POKE BALL");
    ASSERT_EQ(gs->snap.catch_rows[0].quantity, 5);
    ASSERT_EQ(gs->snap.catch_rows[0].permille, 123);
}

TEST(snapshot_hidden_power_uses_the_players_ivs) {
    build_world();
    enter_wild_battle();
    fixture_put16(at(0x02000C00 + 0x0C), 237);
    fixture_put32(at(0x02000C00 + 0x14), 0x3FFFFFFF);  /* all IVs 31 -> Dark */
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_STR_EQ(e->eff[0].move, "HIDDEN POWER");
    ASSERT_EQ(e->eff[0].type, 17);
    ASSERT_EQ(e->eff[0].quarters, 8);  /* Dark vs Fire; stored Normal would be 4 */
}

/* Enemy (Fire, IVs all 31) knows Water Gun, Growl, Mud Slap, Hidden Power;
 * the player's battler is made Fire with IVs 0. */
static void give_enemy_moves(void) {
    uint8_t* enemy = at(0x02000C00 + 0x58);
    fixture_put16(enemy + 0x0C, 1);
    fixture_put16(enemy + 0x0E, 2);
    fixture_put16(enemy + 0x10, 3);
    fixture_put16(enemy + 0x12, 237);
    uint8_t* player = at(0x02000C00);
    player[0x21] = 10; player[0x22] = 10;
}

TEST(snapshot_enemy_moves_against_the_player) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_EQ(e->move_count, 4);
    ASSERT_STR_EQ(e->moves[0].move, "WATER GUN");
    ASSERT_EQ(e->moves[0].type, 11);
    ASSERT_EQ(e->moves[0].quarters, 8);   /* Water vs Fire */
    ASSERT_TRUE(!e->moves[0].status_move);
    ASSERT_STR_EQ(e->moves[1].move, "GROWL");
    ASSERT_TRUE(e->moves[1].status_move);
    ASSERT_EQ(e->moves[1].type, 3);
    ASSERT_STR_EQ(e->moves[2].move, "MUD SLAP");
    ASSERT_EQ(e->moves[2].quarters, 4);   /* no Ground row in the test table */
    ASSERT_STR_EQ(e->moves[3].move, "HIDDEN POWER");
    ASSERT_EQ(e->moves[3].type, 17);      /* the enemy's IVs; the player's would give Fighting */
    ASSERT_EQ(e->moves[3].quarters, 8);   /* Dark vs Fire */
}

TEST(snapshot_enemy_ground_move_misses_levitate) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    at(0x02000C00)[0x20] = 26;  /* player battler has Levitate */
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_STR_EQ(e->moves[2].move, "MUD SLAP");
    ASSERT_EQ(e->moves[2].quarters, 0);
    ASSERT_EQ(e->moves[0].quarters, 8);
}

TEST(snapshot_enemy_moves_without_player_battler) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    at(0x02000E04)[0] = 1;  /* both battlers on the enemy side */
    fixture_put16(at(0x02000C00), 0);
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemy_count, 1);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_EQ(e->eff_count, 0);
    ASSERT_EQ(e->move_count, 4);
    ASSERT_STR_EQ(e->moves[0].move, "WATER GUN");
    ASSERT_EQ(e->moves[0].type, 11);
    ASSERT_EQ(e->moves[0].quarters, 0xFF);
    ASSERT_TRUE(e->moves[1].status_move);
    ASSERT_EQ(e->moves[1].quarters, 0xFF);
    ASSERT_EQ(e->moves[3].type, 17);
    ASSERT_EQ(e->moves[3].quarters, 0xFF);
}

TEST(snapshot_active_name_clamps_bad_party_index) {
    build_world();
    enter_wild_battle();
    fixture_put16(at(0x02000E08), 9);
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.context, GAME_CTX_BATTLE_WILD);
    ASSERT_STR_EQ(gs->snap.active_name, "");
    fixture_put16(at(0x02000E08), 1);  /* in 0..5 but past the party count */
    game_update_mem(gs, &m);
    ASSERT_STR_EQ(gs->snap.active_name, "");
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

    enter_wild_battle();
    give_enemy_moves();
    game_update_mem(gs, &m);
    f = fopen("game_dump_test.txt", "w+");
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(game_dump(&gs->snap, f));
    rewind(f);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    remove("game_dump_test.txt");
    ASSERT_TRUE(strstr(buf, "\nenemy 0 ZIGZAGOON ") != NULL);
    ASSERT_TRUE(strstr(buf, "\nenemy_move 0 0 WATER GUN type 11 x8 dmg 0 0 0\n") != NULL);
    ASSERT_TRUE(strstr(buf, "\nenemy_move 0 1 GROWL type 3 x0 status\n") != NULL);
}

TEST(snapshot_includes_mirage_and_dump_line) {
    build_world();  /* sets the SB1/SB2 pointers and ROM tables */
    GameState* gs = test_state();
    fixture_put16(at(SB1 + 0x13E4), 0x1234);
    fixture_put16(at(SB1 + 0x13E6), 0x5678);
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_TRUE(gs->snap.valid);
    ASSERT_TRUE(gs->snap.mirage.valid);
    ASSERT_EQ_HEX(gs->snap.mirage.value, 0x12345678);
    char buf[2048] = {0};
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(game_dump(&gs->snap, f));
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    ASSERT_TRUE(strstr(buf, "mirage value 12345678") != NULL);
}

TEST(snapshot_reads_berries_and_dumps_them) {
    build_world();
    GameState* gs = test_state();
    gs->places.count = 1;
    gs->places.place[0].sec = 1;
    gs->places.place[0].spots = 2;
    game_strcpy(gs->places.place[0].name, sizeof(gs->places.place[0].name), "ROUTE ONE");
    memset(gs->places.place_of, BERRY_PLACE_NONE, sizeof(gs->places.place_of));
    gs->places.place_of[3] = 0;
    put_str(0x08002000, "CHERI");          /* gBerries[0]: yield 2-3, 3 h stages */
    at(0x08002000)[0x0A] = 3;
    at(0x08002000)[0x0B] = 2;
    at(0x08002000)[0x14] = 3;
    uint8_t* t = at(SB1 + 0x169C + 8 * 3);
    t[0] = 1; t[1] = 5; fixture_put16(t + 2, 300); t[4] = 3;
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_TRUE(gs->snap.berries.valid);
    ASSERT_EQ(gs->snap.berries.count, 1);
    ASSERT_EQ(gs->snap.berries.here, 0);
    char buf[2048] = {0};
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(game_dump(&gs->snap, f));
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    ASSERT_TRUE(strstr(buf, "\nberry 3 ROUTE ONE CHERI stage 5 next 300 ripe 0 yield 3-3 status 1 water 0\n") != NULL);
}

TEST(snapshot_dex_caught_bits) {
    build_world();
    fixture_put16(at(0x08001800 + 4), 3);  /* species 3 -> national 3 for this test */
    set_dex_caught(1);
    set_dex_caught(3);
    at(SB2 + 0x28)[32] |= 0x40;            /* species 2 = national 263: SB2 owned bit only */
    GameState* gs = test_state();
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_TRUE(gs->snap.valid);
    ASSERT_EQ(gs->snap.dex_caught[0], 0x05);
    ASSERT_EQ(gs->snap.dex_caught[1], 0);
    ASSERT_EQ(gs->snap.dex_caught[32] & 0x40, 0);   /* owned alone is not caught */
    set_dex_caught(263);                            /* the mapping is live: all four flags -> caught */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.dex_caught[32] & 0x40, 0x40);
    fixture_put32(at(0x03000000), 0);
    game_update_mem(gs, &m);
    for (int i = 0; i < 52; i++) ASSERT_EQ(gs->snap.dex_caught[i], 0);
}

/* Makes the fixture battle readable for the damage calculator: ROM tables loaded, stats and
 * neutral stages, personalities that match party slot 0 on each side, and a gBattleResources
 * chain inside EWRAM. Player: Lv10, 20 in every stat, 30/30 HP. Enemy: Lv3, 10 in every stat. */
static bool enable_damage(GameState* gs) {
    GameMem m = test_mem();
    uint8_t* player = at(0x02000C00);
    uint8_t* enemy = at(0x02000C00 + 0x58);
    for (int i = 0; i < 5; i++) {
        fixture_put16(player + 2 + 2 * i, 20);
        fixture_put16(enemy + 2 + 2 * i, 10);
    }
    memset(player + 0x18, 6, 8);
    memset(enemy + 0x18, 6, 8);
    player[0x2A] = 10;
    fixture_put16(player + 0x28, 30);
    fixture_put16(player + 0x2C, 30);
    fixture_put32(player + 0x48, 25);           /* gPlayerParty[0] personality */
    fixture_put32(enemy + 0x48, 0x1234);
    fixture_put32(at(0x02000800), 0x1234);      /* gEnemyParty[0] personality */
    fixture_put16(at(0x02000E08), 0);           /* gBattlerPartyIndexes[0], [1] */
    fixture_put16(at(0x02000E0A), 0);
    fixture_put32(at(0x02000E5C), 0x02001000);  /* gBattleResources */
    fixture_put32(at(0x02001004), 0x02001100);  /* ->flags */
    return damage_tables_load(&m, &k_test_profile, &gs->dmg_tables);
}

/* Water Gun both ways: player (Fire, Lv10, SpA 20) vs enemy (Fire, SpD 10):
 * 20*40*6/10/50 = 9, +2 = 11, x2 = 22, rolls 18..22. Enemy (Lv3, SpA 10) vs player (SpD 20):
 * 10*40*3/20/50 = 1, +2 = 3, x2 = 6, rolls 5..6. */
TEST(snapshot_damage_wild_battle_both_directions) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    const GameEnemy* e = &gs->snap.enemies[0];
    ASSERT_EQ(e->eff[0].dmg_kind, DMG_RANGE);
    ASSERT_EQ(e->eff[0].dmg_lo, 18);
    ASSERT_EQ(e->eff[0].dmg_hi, 22);
    ASSERT_EQ(e->eff[1].dmg_kind, DMG_NONE);  /* Growl */
    ASSERT_EQ(e->moves[0].dmg_kind, DMG_RANGE);
    ASSERT_EQ(e->moves[0].dmg_lo, 5);
    ASSERT_EQ(e->moves[0].dmg_hi, 6);
    ASSERT_EQ(e->player_hp, 30);
    ASSERT_EQ(e->player_max_hp, 30);
}

TEST(snapshot_damage_none_without_tables) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    gs->dmg_tables.valid = false;
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    ASSERT_EQ(gs->snap.enemies[0].moves[0].dmg_kind, DMG_NONE);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].quarters, 8);
}

/* R4: gBattleMons from the previous battle (personality differs from the party slot). */
TEST(snapshot_damage_stale_personality_is_none) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    fixture_put32(at(0x02000C00 + 0x58 + 0x48), 0x9999);
    game_update_mem(gs, &m);
    const GameEnemy* e = &gs->snap.enemies[0];
    for (int i = 0; i < e->eff_count; i++) ASSERT_EQ(e->eff[i].dmg_kind, DMG_NONE);
    for (int i = 0; i < e->move_count; i++) ASSERT_EQ(e->moves[i].dmg_kind, DMG_NONE);
    ASSERT_EQ(e->eff[0].quarters, 8);         /* the multiplier display still works */

    fixture_put32(at(0x02000C00 + 0x58 + 0x48), 0x1234);
    fixture_put32(at(0x02000C00 + 0x48), 26);  /* player side mismatch */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    ASSERT_EQ(gs->snap.enemies[0].moves[0].dmg_kind, DMG_NONE);

    fixture_put32(at(0x02000C00 + 0x48), 25);
    fixture_put16(at(0x02000E0A), 6);          /* enemy party index out of range */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
}

/* R3: Charge on battler 0 (the player) doubles the player's Electric move only:
 * 11 -> 22 (rolls 18..22); the enemy's stays 3. */
TEST(snapshot_damage_reads_status3_by_battler_id) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    fixture_put16(at(0x02000C00 + 0x0E), 5);         /* player: Thundershock in slot 1 */
    fixture_put16(at(0x02000C00 + 0x58 + 0x0E), 5);  /* enemy: Thundershock in slot 1 */
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[1].dmg_hi, 11);
    ASSERT_EQ(gs->snap.enemies[0].moves[1].dmg_hi, 3);
    fixture_put32(at(0x02000E60), 1u << 9);          /* gStatuses3[0] |= STATUS3_CHARGED_UP */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[1].dmg_kind, DMG_RANGE);
    ASSERT_EQ(gs->snap.enemies[0].eff[1].dmg_lo, 18);
    ASSERT_EQ(gs->snap.enemies[0].eff[1].dmg_hi, 22);
    ASSERT_EQ(gs->snap.enemies[0].moves[1].dmg_kind, DMG_RANGE);
    ASSERT_EQ(gs->snap.enemies[0].moves[1].dmg_hi, 3);
}

/* Doubles: per-battler reads use the battler id. Battler 1 (enemy side) is empty, battler 3
 * becomes enemies[0]; Charge set only on battler 3 doubles the enemy's Electric move and
 * leaves the player's alone. */
TEST(snapshot_damage_doubles_uses_battler_id) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    fixture_put16(at(0x02000C00 + 0x0E), 5);         /* player: Thundershock in slot 1 */
    fixture_put16(at(0x02000C00 + 0x58 + 0x0E), 5);  /* enemy: Thundershock in slot 1 */
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    memcpy(at(0x02000C00 + 3 * 0x58), at(0x02000C00 + 0x58), 0x58);  /* enemy -> battler 3 */
    fixture_put16(at(0x02000C00 + 0x58), 0);                         /* battler 1: no species */
    at(0x02000E00)[0] = 4;                                           /* gBattlersCount */
    at(0x02000E04)[2] = 0; at(0x02000E04)[3] = 3;                    /* positions: [2] ally, [3] foe */
    fixture_put16(at(0x02000E0E), 0);                                /* gBattlerPartyIndexes[3] */
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemy_count, 1);
    ASSERT_EQ(gs->snap.enemies[0].eff[1].dmg_hi, 11);
    ASSERT_EQ(gs->snap.enemies[0].moves[1].dmg_hi, 3);
    fixture_put32(at(0x02000E60 + 4 * 3), 1u << 9);  /* gStatuses3[3] |= STATUS3_CHARGED_UP */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].moves[1].dmg_kind, DMG_RANGE);
    ASSERT_EQ(gs->snap.enemies[0].moves[1].dmg_hi, 6);   /* enemy's Electric move doubled */
    ASSERT_EQ(gs->snap.enemies[0].eff[1].dmg_hi, 11);    /* player's is not */
}

/* R7: Weather Ball in rain is a Water move for the pill and the multiplier. */
TEST(snapshot_damage_weather_ball_effective_type) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    fixture_put16(at(0x02000C00 + 0x0C), 4);  /* player: Weather Ball in slot 0 */
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].type, 0);      /* no weather: Normal */
    ASSERT_EQ(gs->snap.enemies[0].eff[0].quarters, 4);
    fixture_put16(at(0x02000E50), 0x0001);               /* B_WEATHER_RAIN_TEMPORARY */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].type, 11);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].quarters, 8);   /* Water vs Fire */
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_RANGE);
    at(0x02000C00 + 0x58)[0x20] = 13;                     /* enemy Cloud Nine: no effect */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].type, 0);
}

TEST(snapshot_damage_fainted_target_is_none) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    fixture_put16(at(0x02000C00 + 0x58 + 0x28), 0);  /* enemy hp 0 */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    ASSERT_EQ(gs->snap.enemies[0].moves[0].dmg_kind, DMG_NONE);
}

TEST(snapshot_damage_resources_outside_ewram_is_none) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    fixture_put32(at(0x02000E5C), 0x03000000);  /* gBattleResources in IWRAM */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    ASSERT_EQ(gs->snap.enemies[0].moves[0].dmg_kind, DMG_NONE);
    fixture_put32(at(0x02000E5C), 0);           /* NULL, as outside battle */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    fixture_put32(at(0x02000E5C), 0x02001000);
    fixture_put32(at(0x02001004), 0x0203FFFC);  /* flags[] runs off the end of EWRAM */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    fixture_put32(at(0x02001004), 0x02001100);
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_RANGE);
}

TEST(snapshot_damage_safari_is_none) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    fixture_put32(at(0x02000E10), 1u << 7);
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);
    ASSERT_EQ(gs->snap.enemies[0].moves[0].dmg_kind, DMG_NONE);
}

/* Stone Badge (flag 0x867 = byte 0x10C bit 7): Mud Slap 20*20*6/10/50 + 2 = 6 -> Atk 22 gives 7.
 * Not against a secret base trainer (gTrainerBattleOpponent_A 1024). */
TEST(snapshot_damage_badges_from_save_flags) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    fixture_put16(at(0x02000C00 + 0x0C), 3);  /* player: Mud Slap in slot 0 */
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 6);
    at(SB1 + 0x1270 + 0x10C)[0] = 0x80;
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 7);
    ASSERT_EQ(gs->snap.enemies[0].moves[0].dmg_hi, 6);  /* the enemy never gets badges */
    fixture_put32(at(0x02000E10), 1u << 3);           /* trainer battle */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 7);
    fixture_put16(at(0x02000E5A), 1024);               /* TRAINER_SECRET_BASE */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 6);
}

/* Item 5 is WATER_POWER x1.1: SpA 22 -> 22*40*6/10/50 + 2 = 12, x2 = 24. Item ids past the
 * table mean no hold effect, as GetItemHoldEffect does. */
TEST(snapshot_damage_reads_hold_items) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    fixture_put16(at(0x02000C00 + 0x2E), 5);
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 24);
    fixture_put16(at(0x02000C00 + 0x2E), 50);
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_RANGE);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 22);
}

/* Low Kick needs the target's Pokedex weight by national number (species 2 -> 263). */
TEST(snapshot_damage_low_kick_weight_by_national_dex) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    fixture_put16(at(0x02000C00 + 0x0C), 6);  /* player: Low Kick in slot 0 */
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_NONE);  /* weight 0 = unknown */
    fixture_put16(at(0x08008000 + 263 * 0x20 + 0x0E), 100);    /* 10.0 kg -> power 40 */
    game_update_mem(gs, &m);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_kind, DMG_RANGE);
    ASSERT_EQ(gs->snap.enemies[0].eff[0].dmg_hi, 11);           /* 20*40*6/10/50 + 2 */
}

TEST(snapshot_damage_dump_lines) {
    build_world();
    enter_wild_battle();
    give_enemy_moves();
    GameState* gs = test_state();
    ASSERT_TRUE(enable_damage(gs));
    GameMem m = test_mem();
    game_update_mem(gs, &m);
    char buf[4096];
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(game_dump(&gs->snap, f));
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    ASSERT_TRUE(strstr(buf, "\nyour_move 0 WATER GUN type 11 x8 dmg 1 18 22\n") != NULL);
    ASSERT_TRUE(strstr(buf, "\nyour_move 1 GROWL type 3 x0 status\n") != NULL);
    ASSERT_TRUE(strstr(buf, "\nenemy_move 0 0 WATER GUN type 11 x8 dmg 1 5 6\n") != NULL);
    ASSERT_TRUE(strstr(buf, "\nenemy_move 0 1 GROWL type 3 x0 status\n") != NULL);
}

void run_game_snapshot_tests(void) {
    RUN_TEST(snapshot_dex_caught_bits);
    RUN_TEST(snapshot_reads_berries_and_dumps_them);
    TEST_SUITE("game_snapshot");
    RUN_TEST(snapshot_without_profile_explains);
    RUN_TEST(snapshot_waits_when_save_pointers_invalid);
    RUN_TEST(snapshot_overworld_party_and_encounters);
    RUN_TEST(snapshot_map_name_empty_when_section_unknown);
    RUN_TEST(snapshot_wild_battle_matchups_and_catch);
    RUN_TEST(snapshot_hidden_power_uses_the_players_ivs);
    RUN_TEST(snapshot_enemy_moves_against_the_player);
    RUN_TEST(snapshot_enemy_ground_move_misses_levitate);
    RUN_TEST(snapshot_enemy_moves_without_player_battler);
    RUN_TEST(snapshot_active_name_clamps_bad_party_index);
    RUN_TEST(snapshot_trainer_battle_has_no_catch_rows);
    RUN_TEST(snapshot_battle_flag_without_enemy_is_overworld);
    RUN_TEST(snapshot_out_of_range_ids_show_unknown);
    RUN_TEST(snapshot_clamps_party_count);
    RUN_TEST(snapshot_dump_writes_text);
    RUN_TEST(snapshot_includes_mirage_and_dump_line);
    RUN_TEST(snapshot_damage_wild_battle_both_directions);
    RUN_TEST(snapshot_damage_none_without_tables);
    RUN_TEST(snapshot_damage_stale_personality_is_none);
    RUN_TEST(snapshot_damage_reads_status3_by_battler_id);
    RUN_TEST(snapshot_damage_doubles_uses_battler_id);
    RUN_TEST(snapshot_damage_weather_ball_effective_type);
    RUN_TEST(snapshot_damage_fainted_target_is_none);
    RUN_TEST(snapshot_damage_resources_outside_ewram_is_none);
    RUN_TEST(snapshot_damage_safari_is_none);
    RUN_TEST(snapshot_damage_badges_from_save_flags);
    RUN_TEST(snapshot_damage_reads_hold_items);
    RUN_TEST(snapshot_damage_low_kick_weight_by_national_dex);
    RUN_TEST(snapshot_damage_dump_lines);
}
