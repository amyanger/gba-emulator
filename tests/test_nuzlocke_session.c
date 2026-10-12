#include "test_harness.h"
#include "game_fixture.h"
#include "frontend/nuzlocke_session.h"
#include <stdio.h>

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x4000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define ROM "nuz_session_test.gba"
#define FILE_A ROM ".0000BEEF.nuzlocke"
#define FILE_B ROM ".0000CAFE.nuzlocke"

static const GameProfile k_profile = {
    .name = "test",
    .battle_type_flags = 0x02000E10, .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08, .battler_positions = 0x02000E04,
    .player_party_count = 0x02000000, .player_party = 0x02000400, .enemy_party = 0x02000800,
    .main_in_battle = 0x03000010, .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .map_header = 0x02000E20, .battle_outcome = 0x02000E30, .pokemon_storage_ptr = 0x03000008,
    .species_names = 0x08000100, .species_count = 8, .species_info = 0x08000C00,
    .experience_tables = 0x08000D00, .evolution_table = 0x08002000,
    .region_map_entries = 0x08001C00, .region_map_count = 20,
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

static void put_str(uint32_t addr, const char* s) {
    uint8_t* p = at(addr);
    for (; *s; s++, p++) *p = (*s == ' ') ? 0x00 : (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}

static void set_tid(uint32_t tid) { fixture_put32(at(SB2 + 0x0A), tid); }

static void build(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    set_tid(0x0000BEEF);
    fixture_put16(at(SB2 + 0x0E), 1);  /* 1:00:00 */
    at(0x02000E20 + 0x14)[0] = 3;
    put_str(0x08000100 + 11 * 2, "ZIGZAGOON");
    fixture_put32(at(0x08001C00 + 8 * 3 + 4), 0x08001D00);
    put_str(0x08001D00, "ROUTE ONE");
    at(0x02000000)[0] = 1;
    uint8_t plain[4][12];
    fixture_plain_mon(plain, 2);
    plain[3][1] = 3;
    fixture_encode_mon(at(0x02000400), 0x11112222, 0x0000BEEF, plain, 0x02);
    at(0x02000400 + 0x54)[0] = 5;
    fixture_put16(at(0x02000400 + 0x56), 12);
}

static void frames(NuzSession* s, int n) {
    GameMem m = mem();
    for (int i = 0; i < n; i++) nuz_session_frame(s, &m);
}

static long file_lines(const char* path) {
    FILE* f = fopen(path, "r");
    long n = 0;
    int c;
    if (!f) return -1;
    while ((c = fgetc(f)) != EOF) n += c == '\n';
    fclose(f);
    return n;
}

/* Large: keep off the stack. */
static NuzSession s_a, s_b;

TEST(nuz_session_starts_run_and_writes_file) {
    remove(FILE_A);
    build();
    nuz_session_init(&s_a, &k_profile, ROM, NULL);
    frames(&s_a, 40);
    ASSERT_EQ(s_a.view.state, NUZ_VIEW_NO_RUN);
    ASSERT_EQ(file_lines(FILE_A), -1);
    ASSERT_TRUE(nuz_session_request_start(&s_a));
    frames(&s_a, 2);
    ASSERT_EQ(s_a.view.state, NUZ_VIEW_RUN);
    ASSERT_EQ(file_lines(FILE_A), 3);  /* header, start, owned */
    ASSERT_EQ(s_a.view.alive, 1);
    ASSERT_EQ(s_a.view.routes_used, 1);
    ASSERT_EQ(s_a.view.route_count, 1);
    ASSERT_STR_EQ(s_a.view.routes[0].place, "ROUTE ONE");
    ASSERT_STR_EQ(s_a.view.routes[0].who, "ZIGZAGOON");
    ASSERT_TRUE(s_a.view.routes[0].here);
    ASSERT_TRUE(!nuz_session_request_start(&s_a));
    remove(FILE_A);
}

TEST(nuz_session_replays_file_into_same_run) {
    remove(FILE_A);
    build();
    nuz_session_init(&s_a, &k_profile, ROM, NULL);
    frames(&s_a, 40);
    nuz_session_request_start(&s_a);
    frames(&s_a, 5);
    fixture_put16(at(0x02000400 + 0x56), 0);  /* faints */
    frames(&s_a, 40);
    ASSERT_EQ(s_a.run.deaths, 1);
    nuz_session_init(&s_b, &k_profile, ROM, NULL);
    frames(&s_b, 40);
    ASSERT_EQ(s_b.view.state, NUZ_VIEW_RUN);
    ASSERT_EQ(s_b.run.deaths, 1);
    ASSERT_EQ(s_b.run.member_count, s_a.run.member_count);
    ASSERT_EQ(s_b.view.grave_count, 1);
    remove(FILE_A);
}

TEST(nuz_session_switches_runs_with_trainer_id) {
    remove(FILE_A);
    remove(FILE_B);
    build();
    nuz_session_init(&s_a, &k_profile, ROM, NULL);
    frames(&s_a, 40);
    nuz_session_request_start(&s_a);
    frames(&s_a, 5);
    set_tid(0x0000CAFE);
    frames(&s_a, 5);  /* a brief misread does not switch */
    ASSERT_EQ_HEX(s_a.tid, 0x0000BEEF);
    set_tid(0x0000BEEF);
    frames(&s_a, 5);
    set_tid(0x0000CAFE);
    frames(&s_a, 40);
    ASSERT_EQ_HEX(s_a.tid, 0x0000CAFE);
    ASSERT_EQ(s_a.view.state, NUZ_VIEW_NO_RUN);
    ASSERT_EQ(file_lines(FILE_B), -1);
    ASSERT_EQ(file_lines(FILE_A), 3);
    remove(FILE_A);
}

TEST(nuz_session_leaves_foreign_file_alone) {
    FILE* f = fopen(FILE_A, "w");
    ASSERT_TRUE(f != NULL);
    fputs("not a run\n", f);
    fclose(f);
    build();
    nuz_session_init(&s_a, &k_profile, ROM, NULL);
    frames(&s_a, 40);
    nuz_session_request_start(&s_a);
    frames(&s_a, 5);
    ASSERT_EQ(s_a.view.state, NUZ_VIEW_RUN);
    ASSERT_TRUE(s_a.view.write_failed);
    ASSERT_EQ(file_lines(FILE_A), 1);
    remove(FILE_A);
}

TEST(nuz_session_start_reads_boxes_fresh) {
    remove(FILE_A);
    build();
    fixture_put32(at(0x03000008), 0x02020000);   /* PokemonStorage, boxes at +4 */
    nuz_session_init(&s_a, &k_profile, ROM, NULL);
    frames(&s_a, 62);   /* the last cadence box read (frame 60) saw empty boxes */
    uint8_t plain[4][12], raw[100];
    fixture_plain_mon(plain, 3);
    plain[3][1] = 4;
    fixture_encode_mon(raw, 0x55556666, 0x0000BEEF, plain, 0x02);
    memcpy(at(0x02020000 + 4), raw, 80);
    ASSERT_TRUE(nuz_session_request_start(&s_a));
    frames(&s_a, 2);    /* not a cadence frame: the start must read boxes itself */
    ASSERT_EQ(s_a.view.state, NUZ_VIEW_RUN);
    ASSERT_EQ(s_a.run.member_count, 2);
    remove(FILE_A);
}

TEST(nuz_session_off_without_profile) {
    nuz_session_init(&s_a, NULL, ROM, NULL);
    frames(&s_a, 5);
    ASSERT_EQ(s_a.view.state, NUZ_VIEW_OFF);
    ASSERT_TRUE(!nuz_session_request_start(&s_a));
}

void run_nuzlocke_session_tests(void) {
    RUN_TEST(nuz_session_starts_run_and_writes_file);
    RUN_TEST(nuz_session_replays_file_into_same_run);
    RUN_TEST(nuz_session_switches_runs_with_trainer_id);
    RUN_TEST(nuz_session_leaves_foreign_file_alone);
    RUN_TEST(nuz_session_start_reads_boxes_fresh);
    RUN_TEST(nuz_session_off_without_profile);
}
