#include "frontend/hunt_panel.h"
#include "frontend/hunt_session.h"
#include "game_fixture.h"
#include "test_harness.h"
#include <stdio.h>

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

#define CB2_OVERWORLD 0x08000101u
#define CB2_WILD 0x08000201u
#define CB2_SAFARI 0x08000301u
#define CB2_SCRIPTED 0x08000401u
#define ABIL_SAND_VEIL 8
#define ABIL_ILLUMINATE 35

#define ROM "hunt_session_test.gba"
#define FILE_A ROM ".59CDEE45.hunt"
#define BAD_ROM "hunt_no_such_dir/x.gba"
#define SHINY_PID 0x0000B788u /* with OT 0x59CDEE45: 0xEE45 ^ 0x59CD ^ 0xB788 = 0 */
#define PLAIN_PID 0x601534C5u

static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules,
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
    .species_names = 0x08000100,
    .species_count = 16,
    .species_info = SPECIES_INFO,
    .nature_name_ptrs = 0x08000200,
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

static void put_str(uint32_t addr, const char* s) {
    uint8_t* p = at(addr);
    for (; *s; s++, p++)
        *p = (*s == ' ') ? 0x00 : (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}

static void build_game(void) {
    build();
    put_str(0x08000100 + 11 * 1, "TREECKO");
    put_str(0x08000100 + 11 * 2, "ZIGZAGOON");
    put_str(0x08000100 + 11 * 3, "POOCHYENA");
    fixture_put32(at(0x08000200), 0x08000300);
    put_str(0x08000300, "HARDY");
}

static HuntSession s_a, s_b;

static bool frame1(HuntSession* s, bool rewinding, char* title, char* detail) {
    static NuzFacts nf;
    GameMem m = mem();
    nuz_read(&k_profile, &m, false, &nf);
    return hunt_session_frame(s, &m, &nf, rewinding, title, detail);
}

static void frames(HuntSession* s, int n) {
    char t[48], d[48];
    for (int i = 0; i < n; i++)
        frame1(s, false, t, d);
}

static void start_battle(uint32_t saved_cb, uint32_t flags) {
    at(0x03000010)[0] = 0x02;
    fixture_put32(at(0x02000E10), flags);
    fixture_put32(at(0x03000038), saved_cb);
}

static void end_battle(void) {
    at(0x03000010)[0] = 0;
}

static void set_enemy(uint32_t pid, uint16_t sp) {
    uint8_t plain[4][12];
    fixture_plain_mon(plain, sp);
    fixture_encode_mon(at(ENEMY), pid, 0x59CDEE45u, plain, 0x02);
    at(ENEMY + 0x54)[0] = 3;
}

/* A whole wild battle for one enemy; returns true when the toast fired. */
static bool encounter(HuntSession* s, uint32_t pid, uint16_t sp, char* title, char* detail) {
    bool toast = false;
    set_enemy(pid, sp);
    start_battle(CB2_WILD, 0);
    for (int i = 0; i < 6; i++)
        toast |= frame1(s, false, title, detail);
    end_battle();
    frames(s, 3);
    return toast;
}

static long file_lines(const char* path) {
    FILE* f = fopen(path, "r");
    long n = 0;
    int c;
    if (!f)
        return -1;
    while ((c = fgetc(f)) != EOF)
        n += c == '\n';
    fclose(f);
    return n;
}

static bool file_has(const char* path, const char* line) {
    FILE* f = fopen(path, "r");
    char buf[128];
    bool found = false;
    if (!f)
        return false;
    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\n")] = 0;
        if (strcmp(buf, line) == 0)
            found = true;
    }
    fclose(f);
    return found;
}

static void attached(HuntSession* s, const char* rom) {
    hunt_session_init(s, &k_profile, rom, NULL);
    hunt_session_set_shown(s, true);
    frames(s, 40);
}

TEST(hunt_session_counts_writes_and_toasts) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    ASSERT_TRUE(s_a.attached);
    char title[48] = "", detail[48] = "";
    ASSERT_TRUE(!encounter(&s_a, PLAIN_PID, 2, title, detail));
    ASSERT_EQ(s_a.hunt.total, 1);
    ASSERT_TRUE(encounter(&s_a, SHINY_PID, 3, title, detail));
    ASSERT_STR_EQ(title, "Shiny Poochyena!");
    ASSERT_STR_EQ(detail, "After 2 encounters");
    ASSERT_EQ(file_lines(FILE_A), 3);
    ASSERT_TRUE(file_has(FILE_A, "huntlog 1"));
    ASSERT_TRUE(file_has(FILE_A, "E 2 601534C5 0 0"));
    ASSERT_TRUE(file_has(FILE_A, "E 3 0000B788 1 0"));
    remove(FILE_A);
}

TEST(hunt_session_toast_singular) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    char title[48] = "", detail[48] = "";
    ASSERT_TRUE(encounter(&s_a, SHINY_PID, 1, title, detail));
    ASSERT_STR_EQ(title, "Shiny Treecko!");
    ASSERT_STR_EQ(detail, "After 1 encounter");
    remove(FILE_A);
}

TEST(hunt_session_replays_file) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    encounter(&s_a, 0x11112222u, 2, t, d);
    encounter(&s_a, SHINY_PID, 3, t, d);
    hunt_session_target(&s_a, 1);
    hunt_session_nature(&s_a, 1);
    ASSERT_EQ(s_a.hunt.target, 1);
    ASSERT_EQ(s_a.hunt.nature, 0);

    build_game();
    attached(&s_b, ROM);
    ASSERT_EQ(s_b.hunt.total, 3);
    ASSERT_EQ(s_b.hunt.shiny_total, 1);
    ASSERT_EQ(s_b.hunt.session, 0);
    ASSERT_EQ(s_b.hunt.tally_n, 2);
    ASSERT_EQ(s_b.hunt.tally_species[0], 2);
    ASSERT_EQ(s_b.hunt.tally_count[0], 2);
    ASSERT_EQ(s_b.hunt.target, 1);
    ASSERT_EQ(s_b.hunt.nature, 0);

    /* the reset line replays, later events count again */
    hunt_session_reset_key(&s_a, 1000);
    ASSERT_TRUE(hunt_session_reset_key(&s_a, 2000));
    encounter(&s_a, 0x33334444u, 1, t, d);
    attached(&s_b, ROM);
    ASSERT_EQ(s_b.hunt.total, 1);
    ASSERT_EQ(s_b.hunt.target, 1);
    remove(FILE_A);
}

TEST(hunt_session_skips_unknown_lines) {
    FILE* f = fopen(FILE_A, "w");
    ASSERT_TRUE(f != NULL);
    fputs("huntlog 1\nE 2 601534C5 0 0\nZ what\nE garbage\nE 2 11112222 0 9\nE 3 0000B788 1 2\nN "
          "99\nT\n",
          f);
    fclose(f);
    build_game();
    attached(&s_a, ROM);
    ASSERT_EQ(s_a.hunt.total, 2);
    ASSERT_EQ(s_a.hunt.shiny_total, 1);
    ASSERT_EQ(s_a.hunt.nature, HUNT_NATURE_ANY);
    ASSERT_TRUE(s_a.path_ok);
    remove(FILE_A);
}

TEST(hunt_session_leaves_foreign_file_alone) {
    FILE* f = fopen(FILE_A, "w");
    ASSERT_TRUE(f != NULL);
    fputs("not a hunt\n", f);
    fclose(f);
    build_game();
    attached(&s_a, ROM);
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    ASSERT_EQ(s_a.hunt.total, 1);
    ASSERT_EQ(file_lines(FILE_A), 1);
    ASSERT_TRUE(s_a.queue_n > 0);
    GameMem m = mem();
    hunt_session_present(&s_a, &m, 1000, 0);
    ASSERT_TRUE(hunt_session_view(&s_a)->write_failed);
    ASSERT_TRUE(hunt_session_view(&s_a)->file_foreign);
    remove(FILE_A);
}

TEST(hunt_session_queues_failed_writes_and_retries) {
    remove(FILE_A);
    build_game();
    attached(&s_a, BAD_ROM);
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    encounter(&s_a, 0x11112222u, 3, t, d);
    ASSERT_EQ(s_a.hunt.total, 2);
    ASSERT_EQ(s_a.queue_n, 2);
    char buf[1100];
    hunt_session_present(&s_a, &(GameMem){s_ewram, s_iwram, s_rom, sizeof(s_rom)}, 1000, 0);
    ASSERT_TRUE(hunt_session_view(&s_a)->write_failed);

    /* the folder shows up: the next retry writes everything in order */
    snprintf(buf, sizeof(buf), "%s", FILE_A);
    snprintf(s_a.path, sizeof(s_a.path), "%s", buf);
    frames(&s_a, 60);
    ASSERT_EQ(s_a.queue_n, 0);
    ASSERT_EQ(file_lines(FILE_A), 3);
    ASSERT_TRUE(file_has(FILE_A, "E 3 11112222 0 0"));
    hunt_session_present(&s_a, &(GameMem){s_ewram, s_iwram, s_rom, sizeof(s_rom)}, 2000, 0);
    ASSERT_TRUE(!hunt_session_view(&s_a)->write_failed);
    remove(FILE_A);
}

TEST(hunt_session_queue_is_bounded) {
    build_game();
    attached(&s_a, BAD_ROM);
    for (int i = 0; i < HUNT_QUEUE_MAX + 5; i++)
        hunt_session_nature(&s_a, 1);
    ASSERT_EQ(s_a.queue_n, HUNT_QUEUE_MAX);
}

TEST(hunt_session_reset_arm_and_confirm) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    GameMem m = mem();
    ASSERT_TRUE(!hunt_session_reset_key(&s_a, 1000));
    hunt_session_present(&s_a, &m, 1000, 0);
    ASSERT_TRUE(hunt_session_view(&s_a)->reset_armed);
    ASSERT_EQ(s_a.hunt.total, 1);
    hunt_session_present(&s_a, &m, 4500, 0); /* 3.5 s later: disarmed */
    ASSERT_TRUE(!hunt_session_view(&s_a)->reset_armed);
    ASSERT_TRUE(!hunt_session_reset_key(&s_a, 4600));
    ASSERT_TRUE(!hunt_session_reset_key(&s_a, 8000)); /* too late: arms again */
    ASSERT_EQ(s_a.hunt.total, 1);
    ASSERT_TRUE(hunt_session_reset_key(&s_a, 9000));
    ASSERT_EQ(s_a.hunt.total, 0);
    ASSERT_TRUE(file_has(FILE_A, "R"));
    remove(FILE_A);
}

TEST(hunt_session_target_cycles_map_species) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    encounter(&s_a, 0x11112222u, 2, t, d);
    hunt_session_present(&s_a, &m, 1000, 0);
    const HuntView* v = hunt_session_view(&s_a);
    ASSERT_TRUE(v->any_target);
    ASSERT_STR_EQ(v->target, "Any");
    hunt_session_target(&s_a, 1);
    hunt_session_present(&s_a, &m, 1001, 0);
    ASSERT_STR_EQ(v->target, "TREECKO");
    ASSERT_EQ(v->target_percent, 39);
    ASSERT_EQ(v->target_types[0], 6);
    ASSERT_EQ(v->target_count, 0);
    hunt_session_target(&s_a, 1);
    hunt_session_present(&s_a, &m, 1002, 0);
    ASSERT_STR_EQ(v->target, "ZIGZAGOON");
    ASSERT_EQ(v->target_percent, 36);
    ASSERT_EQ(v->target_count, 2);
    ASSERT_EQ(v->target_types[0], 13);
    hunt_session_target(&s_a, 2);
    hunt_session_present(&s_a, &m, 1003, 0);
    ASSERT_TRUE(v->any_target); /* Any, treecko, zigzagoon, poochyena, back to Any: +2 from
                                   zigzagoon is Any */
    hunt_session_target(&s_a, -1);
    hunt_session_present(&s_a, &m, 1004, 0);
    ASSERT_STR_EQ(v->target, "POOCHYENA");
    ASSERT_EQ(v->target_percent, 25);
    ASSERT_TRUE(file_has(FILE_A, "T 3"));
    remove(FILE_A);
}

TEST(hunt_session_target_survives_stepping_off_grass) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    hunt_session_target(&s_a, 1);
    hunt_session_present(&s_a, &m, 1000, 0);
    const HuntView* v = hunt_session_view(&s_a);
    ASSERT_EQ(v->target_percent, 39);

    fixture_put16(at(PREV_BEHAVIOR),
                  7); /* short grass: the step gate fails, the table is the same */
    frames(&s_a, 2);
    ASSERT_TRUE(!s_a.facts.predictable);
    hunt_session_present(&s_a, &m, 1500, 0);
    ASSERT_EQ(v->target_percent, 39);
    ASSERT_EQ(v->target_types[0], 6);
    hunt_session_target(&s_a, 1);
    ASSERT_EQ(s_a.hunt.target, 2);

    at(SB1 + 0x05)[0] = 17; /* another map with no table: nothing known */
    frames(&s_a, 2);
    hunt_session_present(&s_a, &m, 2000, 0);
    ASSERT_EQ(v->target_percent, 0);
    hunt_session_target(&s_a, 1);
    ASSERT_EQ(s_a.hunt.target, 2);
    remove(FILE_A);
}

TEST(hunt_session_flushes_old_path_before_switching) {
    remove(FILE_A);
    remove(ROM ".0000CAFE.hunt");
    build_game();
    attached(&s_a, BAD_ROM);
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    ASSERT_EQ(s_a.queue_n, 1);
    snprintf(s_a.path, sizeof(s_a.path), "%s", FILE_A); /* the folder shows up */
    s_a.frame = 1;                                      /* no periodic retry before the switch */
    fixture_put32(at(SB2 + 0x0A), 0x0000CAFE);
    frames(&s_a, 35);
    ASSERT_EQ_HEX(s_a.tid, 0x0000CAFE);
    ASSERT_TRUE(file_has(FILE_A, "E 2 601534C5 0 0"));
    ASSERT_EQ(s_a.hunt.total, 0);
    remove(FILE_A);
    remove(BAD_ROM ".0000CAFE.hunt");
}

TEST(hunt_session_nature_filter_view) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    hunt_session_present(&s_a, &m, 1000, 0);
    ASSERT_STR_EQ(hunt_session_view(&s_a)->nature_filter, "Any");
    hunt_session_nature(&s_a, 1);
    hunt_session_present(&s_a, &m, 1001, 0);
    ASSERT_STR_EQ(hunt_session_view(&s_a)->nature_filter, "HARDY");
    hunt_session_nature(&s_a, -1);
    ASSERT_EQ(s_a.hunt.nature, HUNT_NATURE_ANY);
    remove(FILE_A);
}

TEST(hunt_session_view_predicts_and_gates) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    hunt_session_present(&s_a, &m, 1000, 16384);
    const HuntView* v = hunt_session_view(&s_a);
    ASSERT_TRUE(v->on);
    ASSERT_STR_EQ(v->method, "Grass");
    ASSERT_EQ(v->advance, 2349);
    ASSERT_TRUE(v->predict);
    ASSERT_TRUE(v->rows[0].kind != WG_NONE);
    ASSERT_TRUE(v->match_found || v->scanning);

    /* a battle: no rows, the card says why (once it has held for 4 frames) */
    start_battle(CB2_WILD, 0);
    frames(&s_a, 4);
    hunt_session_present(&s_a, &m, 2000, 16384);
    ASSERT_TRUE(!v->predict);
    ASSERT_STR_EQ(v->why_not, "Not predicted in battle");
    ASSERT_TRUE(!v->match_found);
    end_battle();

    at(0x02000E60)[0] = 0x08; /* surfing */
    frames(&s_a, 4);
    hunt_session_present(&s_a, &m, 3000, 16384);
    ASSERT_TRUE(!v->predict);
    ASSERT_STR_EQ(v->why_not, "Not predicted while surfing");
    remove(FILE_A);
}

TEST(hunt_session_view_hides_one_frame_misreads) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    const HuntView* v = hunt_session_view(&s_a);
    hunt_session_present(&s_a, &m, 1000, 0);
    ASSERT_TRUE(v->predict);

    /* one frame with the lead unreadable (party mid-write), then readable again */
    at(PARTY + 0x20)[0] ^= 0xFF;
    frames(&s_a, 1);
    ASSERT_TRUE(!s_a.facts.predictable);
    ASSERT_STR_EQ(s_a.facts.why_not, "Lead Pokemon unreadable");
    hunt_session_present(&s_a, &m, 2000, 0);
    ASSERT_TRUE(v->predict);
    ASSERT_TRUE(v->rows[0].kind != WG_NONE);
    at(PARTY + 0x20)[0] ^= 0xFF;
    frames(&s_a, 1);
    hunt_session_present(&s_a, &m, 3000, 0);
    ASSERT_TRUE(v->predict);

    /* three frames are still a misread; the fourth makes it real */
    at(PARTY + 0x20)[0] ^= 0xFF;
    frames(&s_a, 3);
    hunt_session_present(&s_a, &m, 4000, 0);
    ASSERT_TRUE(v->predict);
    frames(&s_a, 1);
    hunt_session_present(&s_a, &m, 5000, 0);
    ASSERT_TRUE(!v->predict);
    ASSERT_STR_EQ(v->why_not, "Lead Pokemon unreadable");

    /* a savestate load shows the loaded state at once */
    at(PARTY + 0x20)[0] ^= 0xFF;
    hunt_session_discontinuity(&s_a);
    frames(&s_a, 1);
    hunt_session_present(&s_a, &m, 6000, 0);
    ASSERT_TRUE(v->predict);
    remove(FILE_A);
}

TEST(hunt_session_hidden_counts_without_predicting) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    const HuntView* v = hunt_session_view(&s_a);
    hunt_session_present(&s_a, &m, 1000, 0);
    ASSERT_TRUE(v->predict);

    hunt_session_set_shown(&s_a, false);
    frames(&s_a, 1);
    ASSERT_TRUE(!s_a.facts.predictable);
    ASSERT_STR_EQ(s_a.facts.why_not, "");
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    ASSERT_EQ(s_a.hunt.total, 1);

    /* Shown again: nothing stale before the next frame, then that frame's state at once. */
    hunt_session_set_shown(&s_a, true);
    hunt_session_present(&s_a, &m, 2000, 0);
    ASSERT_TRUE(!v->predict);
    ASSERT_STR_EQ(v->why_not, "");
    frames(&s_a, 1);
    hunt_session_present(&s_a, &m, 2001, 0);
    ASSERT_TRUE(v->predict);
    ASSERT_TRUE(v->rows[0].kind != WG_NONE);
    remove(FILE_A);
}

TEST(hunt_session_target_chance_and_separators) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    encounter(&s_a, 0x11112222u, 2, t, d);
    encounter(&s_a, 0x33334444u, 3, t, d);
    hunt_session_present(&s_a, &m, 1000, 0);
    const HuntView* v = hunt_session_view(&s_a);
    ASSERT_TRUE(v->chance == hunt_chance(3)); /* Any: every encounter */
    hunt_session_target(&s_a, 2);             /* Zigzagoon, seen twice */
    hunt_session_present(&s_a, &m, 1001, 0);
    ASSERT_STR_EQ(v->target, "ZIGZAGOON");
    ASSERT_TRUE(v->chance == hunt_chance(2));
    hunt_session_target(&s_a, -1); /* Treecko, never seen */
    hunt_session_present(&s_a, &m, 1002, 0);
    ASSERT_TRUE(v->chance == 0.0);

    char buf[16];
    hunt_panel_number(0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0");
    hunt_panel_number(999, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "999");
    hunt_panel_number(1285, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1,285");
    hunt_panel_number(48213, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "48,213");
    hunt_panel_number(4294967295u, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "4,294,967,295");

    /* the toast count goes through the same formatter */
    s_a.hunt.total = 1284;
    ASSERT_TRUE(encounter(&s_a, SHINY_PID, 3, t, d));
    ASSERT_STR_EQ(d, "After 1,285 encounters");
    remove(FILE_A);
}

TEST(hunt_session_view_throttle_and_version) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    hunt_session_present(&s_a, &m, 1000, 0);
    uint32_t ver = hunt_session_view(&s_a)->version;
    hunt_session_present(&s_a, &m, 1100, 0);
    ASSERT_EQ(hunt_session_view(&s_a)->version, ver); /* nothing changed */
    fixture_put32(at(0x02000E40), 5000);
    frames(&s_a, 1);
    hunt_session_present(&s_a, &m, 1200, 0);
    ASSERT_EQ(hunt_session_view(&s_a)->advance, 2349); /* inside the 250 ms window */
    hunt_session_present(&s_a, &m, 1300, 0);
    ASSERT_EQ(hunt_session_view(&s_a)->advance, 5000);
    ASSERT_TRUE(hunt_session_view(&s_a)->version != ver);
    remove(FILE_A);
}

TEST(hunt_session_dump_lines) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    GameMem m = mem();
    hunt_session_present(&s_a, &m, 1000, 64);
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(hunt_session_dump(&s_a, f));
    ASSERT_TRUE(ftell(f) > 0);
    fclose(f);
    remove(FILE_A);
}

TEST(hunt_session_switches_hunts_with_trainer_id) {
    remove(FILE_A);
    build_game();
    attached(&s_a, ROM);
    char t[48], d[48];
    encounter(&s_a, PLAIN_PID, 2, t, d);
    fixture_put32(at(SB2 + 0x0A), 0x0000CAFE);
    frames(&s_a, 5); /* a brief misread does not switch */
    ASSERT_EQ_HEX(s_a.tid, 0x59CDEE45u);
    fixture_put32(at(SB2 + 0x0A), 0x59CDEE45u);
    frames(&s_a, 5);
    fixture_put32(at(SB2 + 0x0A), 0x0000CAFE);
    frames(&s_a, 40);
    ASSERT_EQ_HEX(s_a.tid, 0x0000CAFE);
    ASSERT_EQ(s_a.hunt.total, 0);
    ASSERT_EQ(file_lines(FILE_A), 2);
    remove(FILE_A);
    remove(ROM ".0000CAFE.hunt");
}

TEST(hunt_session_off_without_profile) {
    hunt_session_init(&s_a, NULL, ROM, NULL);
    frames(&s_a, 5);
    GameMem m = mem();
    hunt_session_present(&s_a, &m, 1000, 100);
    ASSERT_TRUE(!hunt_session_view(&s_a)->on);
    ASSERT_STR_EQ(hunt_session_view(&s_a)->reason, "Game not supported");
}

void run_hunt_session_tests(void) {
    RUN_TEST(hunt_session_counts_writes_and_toasts);
    RUN_TEST(hunt_session_toast_singular);
    RUN_TEST(hunt_session_replays_file);
    RUN_TEST(hunt_session_skips_unknown_lines);
    RUN_TEST(hunt_session_leaves_foreign_file_alone);
    RUN_TEST(hunt_session_queues_failed_writes_and_retries);
    RUN_TEST(hunt_session_queue_is_bounded);
    RUN_TEST(hunt_session_reset_arm_and_confirm);
    RUN_TEST(hunt_session_target_cycles_map_species);
    RUN_TEST(hunt_session_target_survives_stepping_off_grass);
    RUN_TEST(hunt_session_flushes_old_path_before_switching);
    RUN_TEST(hunt_session_nature_filter_view);
    RUN_TEST(hunt_session_view_predicts_and_gates);
    RUN_TEST(hunt_session_view_hides_one_frame_misreads);
    RUN_TEST(hunt_session_hidden_counts_without_predicting);
    RUN_TEST(hunt_session_target_chance_and_separators);
    RUN_TEST(hunt_session_view_throttle_and_version);
    RUN_TEST(hunt_session_dump_lines);
    RUN_TEST(hunt_session_switches_hunts_with_trainer_id);
    RUN_TEST(hunt_session_off_without_profile);
}
