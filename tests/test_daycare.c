#include "test_harness.h"
#include "game_fixture.h"
#include "game/daycare.h"
#include "game/game.h"
#include <string.h>

static DaycareParent mon(uint16_t sp, uint32_t ot, uint32_t pid, uint8_t g0, uint8_t g1, uint8_t ratio) {
    DaycareParent d = { sp, ot, pid, { g0, g1 }, ratio };
    return d;
}

TEST(daycare_compat_rules) {
    DaycareCompatReason r;
    /* ratio 127: pid & 0xFF < 127 -> female */
    DaycareParent f = mon(280, 1, 0x00, 11, 11, 127), m = mon(280, 2, 0xFF, 11, 11, 127);
    ASSERT_EQ(daycare_compat(&f, &m, &r), 70); ASSERT_EQ(r, DAYCARE_COMPAT_OK);
    m.ot_id = 1;  ASSERT_EQ(daycare_compat(&f, &m, &r), 50);
    DaycareParent other = mon(300, 2, 0xFF, 11, 5, 127);
    ASSERT_EQ(daycare_compat(&f, &other, &r), 50);
    other.ot_id = 1; ASSERT_EQ(daycare_compat(&f, &other, &r), 20);
    DaycareParent f2 = mon(280, 3, 0x00, 11, 11, 127);
    ASSERT_EQ(daycare_compat(&f, &f2, &r), 0); ASSERT_EQ(r, DAYCARE_SAME_GENDER);
    DaycareParent ditto = mon(132, 9, 0, 13, 13, 0xFF), meta = mon(376, 9, 0, 10, 10, 0xFF);
    ASSERT_EQ(daycare_compat(&ditto, &meta, &r), 20);
    meta.ot_id = 8; ASSERT_EQ(daycare_compat(&ditto, &meta, &r), 50);
    ASSERT_EQ(daycare_compat(&ditto, &ditto, &r), 0); ASSERT_EQ(r, DAYCARE_TWO_DITTO);
    DaycareParent baby = mon(350, 2, 0xFF, 15, 15, 127);
    ASSERT_EQ(daycare_compat(&f, &baby, &r), 0); ASSERT_EQ(r, DAYCARE_UNDISCOVERED);
    DaycareParent meta2 = mon(376, 1, 0, 10, 10, 0xFF);
    ASSERT_EQ(daycare_compat(&meta2, &f, &r), 0); ASSERT_EQ(r, DAYCARE_GENDERLESS);
    DaycareParent fly = mon(300, 2, 0xFF, 4, 4, 127);
    ASSERT_EQ(daycare_compat(&f, &fly, &r), 0); ASSERT_EQ(r, DAYCARE_NO_SHARED_GROUP);
}

TEST(daycare_next_roll_window) {
    ASSERT_EQ(daycare_next_roll(0), 255);
    ASSERT_EQ(daycare_next_roll(254), 1);
    ASSERT_EQ(daycare_next_roll(255), 256);
    ASSERT_EQ(daycare_next_roll(0x1FE), 1);
}

TEST(daycare_hatch_single_and_fire) {
    DaycareSlot s[6] = {0};
    uint32_t out[6];
    s[0].egg = true; s[0].cycles = 0;
    daycare_hatch_steps(s, 1, 250, out);
    ASSERT_EQ(out[0], 5u);                  /* next check at counter 255 */
    s[0].cycles = 1;
    daycare_hatch_steps(s, 1, 255, out);
    ASSERT_EQ(out[0], 256u + 256u);         /* to_next 256, then one more check */
    s[0].cycles = 3;
    s[1].egg = false; s[1].fire = true;     /* Slugma in party: 2 per check */
    daycare_hatch_steps(s, 2, 0, out);
    ASSERT_EQ(out[0], 255u + 256u * 2);     /* 3 -> 1 -> 0 -> hatch on check 3 */
    ASSERT_EQ(out[1], 0u);
}

TEST(daycare_hatch_one_per_check_and_bad_eggs) {
    DaycareSlot s[6] = {0};
    uint32_t out[6];
    s[0].egg = s[1].egg = true;            /* both at 0 cycles */
    s[2].egg = s[2].bad = true;            /* Bad Egg: never hatches, never blocks */
    daycare_hatch_steps(s, 3, 254, out);
    ASSERT_EQ(out[0], 1u);
    ASSERT_EQ(out[1], 1u + 256u);
    ASSERT_EQ(out[2], 0u);
}

TEST(daycare_hatch_fire_after_hatch) {
    DaycareSlot s[6] = {0};
    uint32_t out[6];
    s[0].egg = true; s[0].cycles = 0; s[0].fire = true;  /* hatches into Slugma */
    s[1].egg = true; s[1].cycles = 4;
    daycare_hatch_steps(s, 2, 254, out);
    /* check 1: slot 0 hatches (slot 1 untouched). checks 2,3: slot 1 4->2->0. check 4: hatch */
    ASSERT_EQ(out[0], 1u);
    ASSERT_EQ(out[1], 1u + 256u * 3);
}

TEST(daycare_level_and_cost) {
    uint32_t t[101];
    for (int i = 0; i <= 100; i++) t[i] = (uint32_t)(i * i * i);  /* medium fast */
    ASSERT_EQ(daycare_level_for_exp(t, 27000), 30);
    ASSERT_EQ(daycare_level_for_exp(t, 26999), 29);
    ASSERT_EQ(daycare_level_for_exp(t, 2000000), 100);
    ASSERT_EQ(daycare_cost(32, 30), 300u);
    ASSERT_EQ(daycare_cost(30, 30), 100u);
}

#define S_NIDORAN_F 29
#define S_NIDORAN_M 32
#define S_DITTO 132
#define S_MARILL 183
#define S_WOBBUFFET 202
#define S_AZURILL 350
#define S_WYNAUT 360
#define S_METAGROSS 376
#define S_VOLBEAT 386
#define S_ILLUMISE 387

static const GameProfile k_prof = {
    .incense_baby = {S_AZURILL, S_WYNAUT}, .incense_item = {220, 221},
    .species_ditto = S_DITTO, .species_nidoran_f = S_NIDORAN_F, .species_nidoran_m = S_NIDORAN_M,
    .species_volbeat = S_VOLBEAT, .species_illumise = S_ILLUMISE,
};

static uint16_t egg(uint16_t s0, MonGender g0, uint16_t s1, MonGender g1, uint16_t i0, uint16_t i1,
                    uint32_t pid) {
    uint16_t parent[400] = {0};
    parent[281] = 280; parent[282] = 281; parent[S_MARILL] = S_AZURILL; parent[S_WOBBUFFET] = S_WYNAUT;
    uint16_t sp[2] = {s0, s1}, it[2] = {i0, i1};
    MonGender g[2] = {g0, g1};
    return daycare_egg_species(sp, g, it, pid, parent, 400, &k_prof);
}

TEST(daycare_egg_species_rules) {
    ASSERT_EQ(egg(282, MON_GENDER_FEMALE, 260, MON_GENDER_MALE, 0, 0, 0), 280);
    ASSERT_EQ(egg(282, MON_GENDER_MALE, 260, MON_GENDER_FEMALE, 0, 0, 0), 260);
    ASSERT_EQ(egg(S_DITTO, MON_GENDER_NONE, 282, MON_GENDER_MALE, 0, 0, 0), 280);
    ASSERT_EQ(egg(282, MON_GENDER_MALE, S_DITTO, MON_GENDER_NONE, 0, 0, 0), 280);
    ASSERT_EQ(egg(S_DITTO, MON_GENDER_NONE, S_METAGROSS, MON_GENDER_NONE, 0, 0, 0), S_METAGROSS);
    ASSERT_EQ(egg(282, MON_GENDER_MALE, 260, MON_GENDER_MALE, 0, 0, 0), 0);
}

TEST(daycare_egg_species_incense) {
    ASSERT_EQ(egg(S_MARILL, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 0, 0), S_MARILL);
    ASSERT_EQ(egg(S_MARILL, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 220, 0), S_AZURILL);
    ASSERT_EQ(egg(S_MARILL, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 220, 0, 0), S_AZURILL);
    ASSERT_EQ(egg(S_WOBBUFFET, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 0, 0), S_WOBBUFFET);
    ASSERT_EQ(egg(S_WOBBUFFET, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 221, 0), S_WYNAUT);
    /* the Sea Incense does not make a Wynaut */
    ASSERT_EQ(egg(S_WOBBUFFET, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 220, 220, 0), S_WOBBUFFET);
}

TEST(daycare_egg_species_gender_bit) {
    ASSERT_EQ(egg(S_NIDORAN_F, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 0, 0x8000), S_NIDORAN_M);
    ASSERT_EQ(egg(S_NIDORAN_F, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 0, 0x7FFF), S_NIDORAN_F);
    ASSERT_EQ(egg(S_ILLUMISE, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 0, 0x8000), S_VOLBEAT);
    ASSERT_EQ(egg(S_ILLUMISE, MON_GENDER_FEMALE, 100, MON_GENDER_MALE, 0, 0, 0), S_ILLUMISE);
}

/* ---- daycare reader (fake memory, like test_game_snapshot.c) ---- */

static uint8_t s_ew[0x40000];
static uint8_t s_iw[0x8000];
static uint8_t s_rom[0x2000];

#define DC_SB1A 0x02010000u
#define DC_SB1B 0x02020000u
#define DC_SB2  0x02000100u
#define DC_OFF  0x3030u
#define DC_TID  0x0000BEEFu
#define R_RALTS 1
#define R_GARDEVOIR 2
#define R_DITTO 3
#define R_SLUGMA 4
#define R_BELDUM 5
#define R_MODEST 15

static const GameProfile k_dc_prof = {
    .name = "test",
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .player_party_count = 0x02000000, .player_party = 0x02000400,
    .species_names = 0x08000100, .species_count = 6,
    .ability_names = 0x08000200, .ability_count = 50,
    .type_names = 0x08000A00, .type_count = 18,
    .items = 0x08000B00, .item_count = 1,
    .nature_name_ptrs = 0x08000C00,
    .species_info = 0x08000D00,
    .experience_tables = 0x08001000,
    .daycare_sb1_offset = DC_OFF, .pending_egg_flag = 0x86,
    .species_ditto = R_DITTO,
};

static uint8_t* dc_at(uint32_t addr) {
    if (addr >= 0x08000000) return s_rom + (addr - 0x08000000);
    if (addr >= 0x03000000) return s_iw + (addr - 0x03000000);
    return s_ew + (addr - 0x02000000);
}

static void dc_str(uint32_t addr, const char* s) {
    uint8_t* p = dc_at(addr);
    for (; *s; s++, p++) *p = (*s == ' ') ? 0x00 : (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}

static void dc_species(uint16_t sp, const char* name, uint8_t ratio, uint8_t g0, uint8_t g1,
                       uint8_t a0, uint8_t a1) {
    uint8_t* si = dc_at(0x08000D00 + 28u * sp);
    dc_str(0x08000100 + 11u * sp, name);
    si[0x10] = ratio; si[0x11] = 20; si[0x13] = 0; si[0x14] = g0; si[0x15] = g1;
    si[0x16] = a0; si[0x17] = a1;
}

static GameState* dc_world(void) {
    static GameState gs;
    memset(s_ew, 0, sizeof(s_ew));
    memset(s_iw, 0, sizeof(s_iw));
    memset(s_rom, 0, sizeof(s_rom));
    memset(&gs, 0, sizeof(gs));
    gs.profile = &k_dc_prof;
    gs.search.ok = true;
    gs.search.parent[R_GARDEVOIR] = R_RALTS;
    fixture_put32(dc_at(0x03000000), DC_SB1A);
    fixture_put32(dc_at(0x03000004), DC_SB2);
    fixture_put32(dc_at(DC_SB2 + 0x0A), DC_TID);
    dc_species(R_RALTS, "RALTS", 127, 11, 11, 1, 2);
    dc_species(R_GARDEVOIR, "GARDEVOIR", 127, 11, 11, 1, 0);
    dc_species(R_DITTO, "DITTO", 0xFF, 13, 13, 3, 0);
    dc_species(R_SLUGMA, "SLUGMA", 127, 14, 14, 40, 49);
    dc_species(R_BELDUM, "BELDUM", 0xFF, 10, 10, 4, 0);
    dc_str(0x08000200 + 13u * 1, "TRACE");
    dc_str(0x08000200 + 13u * 2, "SYNCHRONIZE");
    dc_str(0x08000200 + 13u * 3, "LIMBER");
    dc_str(0x08000200 + 13u * 4, "CLEAR BODY");
    dc_str(0x08000E00, "HARDY");
    dc_str(0x08000E10, "MODEST");
    for (uint32_t n = 0; n < 25; n++)
        fixture_put32(dc_at(0x08000C00 + 4u * n), n == R_MODEST ? 0x08000E10 : 0x08000E00);
    for (uint32_t lv = 0; lv <= 100; lv++) fixture_put32(dc_at(0x08001000 + 4u * lv), lv * lv * lv);
    return &gs;
}

/* exp 1000 = level 10 on the n^3 table */
static void dc_parent(uint32_t sb1, uint8_t slot, uint16_t species, uint32_t pid, uint32_t ot,
                      uint32_t steps) {
    uint8_t plain[4][12], raw[100];
    fixture_plain_mon(plain, species);
    fixture_put32(plain[3] + 4, 0x3FFFFFFFu);  /* abilityNum 0 */
    fixture_encode_mon(raw, pid, ot, plain, 0x02);
    uint8_t* d = dc_at(sb1 + DC_OFF + DAYCARE_MON_SIZE * slot);
    memcpy(d, raw, 0x50);
    fixture_put32(d + DAYCARE_MON_STEPS, steps);
}

static void dc_party(uint8_t i, uint16_t species, uint32_t pid, bool egg, uint8_t friendship) {
    uint8_t plain[4][12];
    fixture_plain_mon(plain, species);  /* abilityNum 1 */
    plain[0][9] = friendship;
    if (egg) fixture_put32(plain[3] + 4, 0x3FFFFFFFu | (1u << 30) | (1u << 31));
    fixture_encode_mon(dc_at(0x02000400 + 100u * i), pid, DC_TID, plain, egg ? 0x06 : 0x02);
    if (dc_at(0x02000000)[0] < i + 1) dc_at(0x02000000)[0] = (uint8_t)(i + 1);
}

static void dc_egg(uint32_t sb1, uint32_t pid, bool flag) {
    fixture_put32(dc_at(sb1 + DC_OFF + DAYCARE_OFFSPRING), pid);
    if (flag) dc_at(sb1 + 0x1270 + 0x10)[0] |= 0x40;
}

/* female Gardevoir (pid low byte 0x10) in slot 0, male with another OT in slot 1 */
static void dc_two_gardevoir(uint32_t sb1) {
    dc_parent(sb1, 0, R_GARDEVOIR, 0x00000010, DC_TID, 331);       /* 1331 exp: lv 11 */
    dc_parent(sb1, 1, R_GARDEVOIR, 0x000000F0, 0x12345678, 0x1DA); /* 1474 exp: lv 11 */
}

TEST(daycare_read_unsupported_rom) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    gs->profile = NULL;
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(!d.valid);
}

TEST(daycare_read_empty) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.valid);
    ASSERT_EQ(d.parent_count, 0);
    ASSERT_EQ(d.next_roll, 0);
    ASSERT_TRUE(!d.egg_waiting);
    ASSERT_TRUE(!d.egg_known);
    for (int i = 0; i < 6; i++) ASSERT_EQ(d.hatch_steps[i], 0u);
}

TEST(daycare_read_two_parents) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_two_gardevoir(DC_SB1A);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.valid);
    ASSERT_EQ(d.parent_count, 2);
    ASSERT_STR_EQ(d.mon[0].species, "GARDEVOIR");
    ASSERT_EQ(d.mon[0].gender, MON_GENDER_FEMALE);
    ASSERT_EQ(d.mon[1].gender, MON_GENDER_MALE);
    ASSERT_EQ(d.slot[0], 0); ASSERT_EQ(d.slot[1], 1);
    ASSERT_EQ(d.mon[0].level, 11); ASSERT_EQ(d.gained[0], 1); ASSERT_EQ(d.cost[0], 200u);
    ASSERT_EQ(d.steps[0], 331u);
    ASSERT_EQ(d.mon[1].level, 11); ASSERT_EQ(d.gained[1], 1); ASSERT_EQ(d.cost[1], 200u);
    ASSERT_EQ(d.mon[0].exp_to_next, 1728u - 1331u);
    ASSERT_TRUE(d.compat_known);
    ASSERT_EQ(d.compat, 70);
    ASSERT_EQ(d.compat_reason, DAYCARE_COMPAT_OK);
    ASSERT_EQ(d.next_roll, 37);
    ASSERT_TRUE(!d.egg_waiting);

    /* same OT: 50; same gender: no roll */
    dc_parent(DC_SB1A, 1, R_GARDEVOIR, 0x000000F0, DC_TID, 0x1DA);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.compat, 50);
    dc_parent(DC_SB1A, 1, R_GARDEVOIR, 0x00000011, DC_TID, 0x1DA);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.compat, 0);
    ASSERT_EQ(d.compat_reason, DAYCARE_SAME_GENDER);
    ASSERT_EQ(d.next_roll, 0);
}

TEST(daycare_read_one_parent_in_slot_1) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_parent(DC_SB1A, 1, R_GARDEVOIR, 0x000000F0, DC_TID, 0);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.parent_count, 1);
    ASSERT_EQ(d.slot[0], 1);
    ASSERT_EQ(d.mon[0].level, 10);
    ASSERT_EQ(d.cost[0], 100u);
    ASSERT_TRUE(!d.compat_known);
    ASSERT_EQ(d.next_roll, 0);
}

TEST(daycare_read_egg_waiting) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_two_gardevoir(DC_SB1A);
    /* nature 15 (Modest), low byte 0x10 < 127: female, odd: ability slot 1 */
    uint32_t pid = 0x2B5B0000u | 0x10u;
    pid = pid - pid % 25u + R_MODEST;
    if (!(pid & 1u)) pid += 25u;
    ASSERT_EQ(pid % 25u, (uint32_t)R_MODEST);
    ASSERT_TRUE((pid & 0xFFu) < 127u);
    dc_egg(DC_SB1A, pid, true);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.egg_waiting);
    ASSERT_TRUE(d.egg_known);
    ASSERT_EQ(d.next_roll, 0);  /* no roll while an egg waits */
    ASSERT_STR_EQ(d.egg_species, "RALTS");
    ASSERT_EQ(d.egg_gender, MON_GENDER_FEMALE);
    ASSERT_STR_EQ(d.egg_nature, "MODEST");
    ASSERT_EQ(d.egg_nature_up, 3); ASSERT_EQ(d.egg_nature_down, 0);
    ASSERT_STR_EQ(d.egg_ability, "SYNCHRONIZE");
    ASSERT_TRUE(!d.egg_shiny);

    /* TID ^ SID ^ pid halves < 8 */
    fixture_put32(dc_at(DC_SB2 + 0x0A), pid ^ 7u);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.egg_shiny);
    fixture_put32(dc_at(DC_SB2 + 0x0A), pid ^ 8u);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(!d.egg_shiny);
}

TEST(daycare_read_offspring_without_flag) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_two_gardevoir(DC_SB1A);
    dc_egg(DC_SB1A, 0x12345678, false);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(!d.egg_waiting);
    ASSERT_TRUE(!d.egg_known);
    ASSERT_EQ(d.next_roll, 0);  /* offspringPersonality != 0 still blocks the roll */
}

TEST(daycare_read_genderless_egg_one_ability) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_parent(DC_SB1A, 0, R_DITTO, 0x00000001, 0x11111111, 0);
    dc_parent(DC_SB1A, 1, R_BELDUM, 0x00000002, 0x22222222, 0);
    dc_egg(DC_SB1A, 0x00010003, true);  /* odd pid, Beldum has one ability */
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.compat, 50);
    ASSERT_TRUE(d.egg_known);
    ASSERT_STR_EQ(d.egg_species, "BELDUM");
    ASSERT_EQ(d.egg_gender, MON_GENDER_NONE);
    ASSERT_STR_EQ(d.egg_ability, "CLEAR BODY");
}

TEST(daycare_read_egg_unknown_without_index) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    gs->search.ok = false;
    dc_two_gardevoir(DC_SB1A);
    dc_egg(DC_SB1A, 0x12345678, true);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.egg_waiting);
    ASSERT_TRUE(!d.egg_known);
}

TEST(daycare_read_party_hatch_steps) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_party(0, R_RALTS, 0x100, true, 3);
    dc_party(1, R_SLUGMA, 0x200, false, 70);  /* abilityNum 1: Flame Body */
    dc_at(DC_SB1A + DC_OFF + DAYCARE_STEP_COUNTER)[0] = 0;
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.fire);
    ASSERT_EQ(d.hatch_steps[0], 255u + 256u * 2);  /* 3 -> 1 -> 0 -> hatch */
    ASSERT_EQ(d.hatch_steps[1], 0u);

    dc_party(1, R_GARDEVOIR, 0x200, false, 70);  /* abilityNum 1 on a one-ability species: 0 */
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(!d.fire);
    ASSERT_EQ(d.hatch_steps[0], 255u + 256u * 3);
}

TEST(daycare_hold_on_save_block_move) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    static GameDaycare first, d;
    dc_two_gardevoir(DC_SB1A);
    game_read_daycare_mem(gs, &m, &first);
    ASSERT_EQ(first.parent_count, 2);
    memset(dc_at(DC_SB1B + DC_OFF), 0xAA, 0x120);
    fixture_put32(dc_at(0x03000000), DC_SB1B);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_MEM_EQ(&d, &first, sizeof(d));
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.mon[0].bad);
    ASSERT_TRUE(memcmp(&d, &first, sizeof(d)) != 0);
}

TEST(daycare_forget_trusts_next_read) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    static GameDaycare first, d;
    dc_two_gardevoir(DC_SB1A);
    game_read_daycare_mem(gs, &m, &first);
    ASSERT_EQ(first.parent_count, 2);
    game_daycare_forget(gs);
    /* SB1 moved to a block holding an empty Daycare while nothing was reading */
    memset(dc_at(DC_SB1B + DC_OFF), 0, 0x120);
    fixture_put32(dc_at(0x03000000), DC_SB1B);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.parent_count, 0);
}

TEST(daycare_read_now_ignores_hold) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    static GameDaycare first, held, now;
    dc_two_gardevoir(DC_SB1A);
    game_read_daycare_mem(gs, &m, &first);
    ASSERT_TRUE(!first.egg_waiting);
    /* a new egg arrives and the save block moves before the next held read */
    dc_two_gardevoir(DC_SB1B);
    dc_egg(DC_SB1B, 0x5A5A5A5Au, true);
    fixture_put32(dc_at(0x03000000), DC_SB1B);
    game_read_daycare_now_mem(gs, &m, &now);
    ASSERT_TRUE(now.egg_waiting);
    game_read_daycare_mem(gs, &m, &held);
    ASSERT_TRUE(!held.egg_waiting);   /* the hold still returns the old view */
    ASSERT_MEM_EQ(&held, &first, sizeof(held));
}

TEST(daycare_hold_on_torn_slot) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    static GameDaycare first, d;
    dc_two_gardevoir(DC_SB1A);
    game_read_daycare_mem(gs, &m, &first);
    dc_at(DC_SB1A + DC_OFF + 0x1C)[0] ^= 1;  /* checksum fails, same personality */
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_MEM_EQ(&d, &first, sizeof(d));
    ASSERT_TRUE(!d.mon[0].bad);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_TRUE(d.mon[0].bad);
    ASSERT_TRUE(!d.compat_known);
}

TEST(daycare_hold_on_torn_party_slot) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    static GameDaycare first, d;
    dc_party(0, R_RALTS, 0x100, true, 3);
    dc_party(1, R_SLUGMA, 0x200, false, 70);
    game_read_daycare_mem(gs, &m, &first);
    ASSERT_TRUE(first.fire);
    ASSERT_EQ(first.hatch_steps[0], 255u + 256u * 2);
    dc_at(0x02000400 + 100u + 0x1C)[0] ^= 1;  /* Slugma fails its checksum, same personality */
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_MEM_EQ(&d, &first, sizeof(d));
    game_read_daycare_mem(gs, &m, &d);  /* bad twice: real */
    ASSERT_TRUE(!d.fire);
    ASSERT_EQ(d.hatch_steps[0], 255u + 256u * 3);

    /* a torn egg is held once too */
    dc_party(1, R_SLUGMA, 0x200, false, 70);
    game_read_daycare_mem(gs, &m, &first);
    dc_at(0x02000400 + 0x1C)[0] ^= 1;
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.hatch_steps[0], first.hatch_steps[0]);
    game_read_daycare_mem(gs, &m, &d);
    ASSERT_EQ(d.hatch_steps[0], 0u);
}

TEST(daycare_dump_lines) {
    GameState* gs = dc_world();
    GameMem m = { s_ew, s_iw, s_rom, sizeof(s_rom) };
    GameDaycare d;
    dc_two_gardevoir(DC_SB1A);
    dc_party(0, R_RALTS, 0x100, true, 0);
    dc_at(DC_SB1A + DC_OFF + DAYCARE_STEP_COUNTER)[0] = 250;
    game_read_daycare_mem(gs, &m, &d);
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(game_dump_daycare(&d, f));
    rewind(f);
    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_TRUE(strstr(buf, "daycare parents 2 compat 70 reason 0 next_roll 37 egg 0 known 0 fire 0\n") != NULL);
    ASSERT_TRUE(strstr(buf, "daycare parent 0 GARDEVOIR lv 11 gained 1 steps 331 cost 200\n") != NULL);
    ASSERT_TRUE(strstr(buf, "daycare parent 1 GARDEVOIR lv 11 gained 1 steps 474 cost 200\n") != NULL);
    ASSERT_TRUE(strstr(buf, "daycare hatch 0 5\n") != NULL);
}

void run_daycare_tests(void) {
    RUN_TEST(daycare_compat_rules);
    RUN_TEST(daycare_next_roll_window);
    RUN_TEST(daycare_hatch_single_and_fire);
    RUN_TEST(daycare_hatch_one_per_check_and_bad_eggs);
    RUN_TEST(daycare_hatch_fire_after_hatch);
    RUN_TEST(daycare_level_and_cost);
    RUN_TEST(daycare_egg_species_rules);
    RUN_TEST(daycare_egg_species_incense);
    RUN_TEST(daycare_egg_species_gender_bit);
    RUN_TEST(daycare_read_unsupported_rom);
    RUN_TEST(daycare_read_empty);
    RUN_TEST(daycare_read_two_parents);
    RUN_TEST(daycare_read_one_parent_in_slot_1);
    RUN_TEST(daycare_read_egg_waiting);
    RUN_TEST(daycare_read_offspring_without_flag);
    RUN_TEST(daycare_read_genderless_egg_one_ability);
    RUN_TEST(daycare_read_egg_unknown_without_index);
    RUN_TEST(daycare_read_party_hatch_steps);
    RUN_TEST(daycare_hold_on_save_block_move);
    RUN_TEST(daycare_forget_trusts_next_read);
    RUN_TEST(daycare_read_now_ignores_hold);
    RUN_TEST(daycare_hold_on_torn_slot);
    RUN_TEST(daycare_hold_on_torn_party_slot);
    RUN_TEST(daycare_dump_lines);
}
