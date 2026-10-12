#include "test_harness.h"
#include "game_fixture.h"
#include "game/game.h"
#include "game/mirage.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x4000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define STORAGE 0x02020000u

static const GameProfile k_profile = {
    .name = "test",
    .player_party = 0x02000400,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .pokemon_storage_ptr = 0x03000008,
    .species_names = 0x08000100, .species_count = 4,
};

static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

static uint8_t* at(uint32_t a) {
    if (a >= 0x08000000) return s_rom + (a - 0x08000000);
    if (a >= 0x03000000) return s_iwram + (a - 0x03000000);
    return s_ewram + (a - 0x02000000);
}

/* Species 1-3 named AAA, BBB, CCC (Gen 3 'A' = 0xBB, 11-byte stride). */
static void reset(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    fixture_put32(at(0x03000008), STORAGE);
    for (int s = 1; s <= 3; s++) {
        uint8_t* n = at(0x08000100 + 11u * s);
        n[0] = n[1] = n[2] = (uint8_t)(0xBB + s - 1);
        n[3] = 0xFF;
    }
}

static void set_value(uint32_t v) {
    fixture_put16(at(SB1 + 0x13E4), (uint16_t)(v >> 16));
    fixture_put16(at(SB1 + 0x13E6), (uint16_t)v);
}
static void set_var_days(uint16_t d) { fixture_put16(at(SB1 + 0x141C), d); }
static void set_clock_flag(void) { at(SB1 + 0x1270 + (0x895 >> 3))[0] |= 1u << (0x895 & 7); }
static void set_offset(int16_t days, int8_t h, int8_t mi, int8_t s) {
    fixture_put16(at(SB2 + 0x98), (uint16_t)days);
    at(SB2 + 0x9A)[0] = (uint8_t)h; at(SB2 + 0x9B)[0] = (uint8_t)mi; at(SB2 + 0x9C)[0] = (uint8_t)s;
}
static void put_mon(uint32_t addr, uint32_t pid, uint16_t species, bool egg, bool party) {
    uint8_t raw[100], plain[4][12];
    fixture_plain_mon(plain, species);
    fixture_encode_mon(raw, pid, 0x1234, plain, (uint8_t)(0x02 | (egg ? 0x04 : 0)));
    memcpy(at(addr), raw, party ? 100 : 80);
}
static void party(int slot, uint32_t pid, uint16_t species) {
    put_mon(0x02000400 + 100u * slot, pid, species, false, true);
}
static void box(int index, uint32_t pid, uint16_t species) {
    put_mon(STORAGE + 4 + 80u * index, pid, species, false, false);
}

/* 2026-03-01 12:00; the game set its clock that same day, so local day 0 = today. */
static MirageClock clock_2026(void) {
    MirageClock c = { true, 26, 3, 1, 12, 0, 0 };
    return c;
}

static void read_info(const MirageClock* clk, MirageInfo* out) {
    static MirageCache mc;
    memset(&mc, 0, sizeof(mc));
    GameMem m = mem();
    mirage_read(&mc, &k_profile, &m, clk, SB1, SB2, out);
}

TEST(mirage_step_matches_pokeemerald) {
    ASSERT_EQ_HEX(mirage_step(0), 0x3039);
    ASSERT_EQ_HEX(mirage_step(0x3039), 0xD3DC167E);
    ASSERT_EQ_HEX(mirage_step(mirage_step(mirage_step(0x12345678))), 0x2E1D6AB7);
}

TEST(mirage_day_count_matches_pokeemerald) {
    ASSERT_EQ(mirage_day_count(0, 1, 1), 1);
    ASSERT_EQ(mirage_day_count(0, 3, 1), 61);  /* 2000 is a leap year */
    ASSERT_EQ(mirage_day_count(26, 3, 1), 9557);
    ASSERT_EQ(mirage_day_count(99, 12, 31), 36525);
}

TEST(mirage_local_days_borrows_like_the_game) {
    MirageClock c = clock_2026();
    MirageTime off = { 9557, 0, 0, 0 };
    ASSERT_EQ(mirage_local_days(&c, &off), 0);
    off.hours = 13; /* offset later in the day than 12:00: borrow a day */
    ASSERT_EQ(mirage_local_days(&c, &off), -1);
    off.hours = 12; off.minutes = 0; off.seconds = 1; /* seconds borrow ripples up */
    ASSERT_EQ(mirage_local_days(&c, &off), -1);
}

TEST(mirage_search_finds_seeded_days) {
    MirageMon owned[2] = {
        { 0x38EF, 1, false, MIRAGE_BOX, 2 },
        { 0x8459, 2, false, MIRAGE_PARTY, 0 },
    };
    MirageMatch out[3];
    uint8_t n = mirage_search(0x12345678, owned, 2, 1, 400, out, 3);
    ASSERT_TRUE(n >= 2);
    ASSERT_EQ(out[0].k, 57);
    ASSERT_EQ(out[0].index, 1);
    ASSERT_EQ(out[1].k, 212);
    ASSERT_EQ(out[1].index, 0);
}

TEST(mirage_search_tie_prefers_first_owned) {
    MirageMon owned[2] = {
        { 0x1234, 1, false, MIRAGE_PARTY, 3 },
        { 0x1234, 2, false, MIRAGE_BOX, 1 },
    };
    MirageMatch out[1];
    ASSERT_EQ(mirage_search(0x12345678, owned, 2, 0, 0, out, 1), 1);
    ASSERT_EQ(out[0].index, 0);
}

TEST(mirage_read_party_match_today) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    set_offset(9557, 0, 0, 0);
    party(0, 0xAAAA0001, 1);
    party(1, 0xBBBB1234, 2);
    MirageClock clk = clock_2026();
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_TRUE(mi.valid && mi.clock_set && mi.clock_known);
    ASSERT_EQ(mi.pending, 0);
    ASSERT_EQ(mi.today.where, MIRAGE_PARTY);
    ASSERT_EQ(mi.today.slot, 1);
    ASSERT_TRUE(strcmp(mi.today.name, "BBB") == 0);
}

TEST(mirage_read_box_match_today_and_forecast) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    set_offset(9557, 0, 0, 0);
    party(0, 0x00000001, 1);
    box(0, 0x00001234, 3);        /* box 1: matches today */
    box(65, 0x000038EF, 2);       /* box 3 (65 / 30 + 1): matches in 212 days */
    box(31, 0x00008459, 1);       /* box 2: matches in 57 days */
    MirageClock clk = clock_2026();
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_EQ(mi.today.where, MIRAGE_BOX);
    ASSERT_EQ(mi.today.slot, 1);
    ASSERT_TRUE(mi.forecast_count >= 2);
    ASSERT_EQ(mi.forecast[0].days, 57);
    ASSERT_EQ(mi.forecast[0].where, MIRAGE_BOX);
    ASSERT_EQ(mi.forecast[0].slot, 2);
    ASSERT_EQ(mi.forecast[1].days, 212);
    ASSERT_EQ(mi.forecast[1].slot, 3);
    ASSERT_TRUE(strcmp(mi.forecast[1].name, "BBB") == 0);
}

TEST(mirage_read_counts_eggs) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    set_offset(9557, 0, 0, 0);
    put_mon(0x02000400, 0x00001234, 1, true, true);
    MirageClock clk = clock_2026();
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_EQ(mi.today.where, MIRAGE_PARTY);
    ASSERT_TRUE(strcmp(mi.today.name, "EGG") == 0);
}

TEST(mirage_read_pending_day_shifts_today_and_forecast) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    set_offset(9556, 0, 0, 0);    /* local day 1 */
    set_var_days(0);              /* last map load was on local day 0 */
    party(0, 0x00000B71, 1);      /* matches the value after one step */
    box(0, 0x000038EF, 2);        /* 212 steps from the stored value = 211 days from now */
    MirageClock clk = clock_2026();
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_EQ(mi.pending, 1);
    ASSERT_EQ_HEX(mi.value, mirage_step(0x12345678));
    ASSERT_EQ(mi.today.where, MIRAGE_PARTY);
    ASSERT_TRUE(mi.forecast_count >= 1);
    ASSERT_EQ(mi.forecast[0].days, 211);
}

TEST(mirage_read_clock_behind_adds_gap) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    set_offset(9557, 0, 0, 0);    /* local day 0 */
    set_var_days(5);              /* clock was moved back 5 days */
    box(0, 0x00008459, 1);        /* 57 steps */
    MirageClock clk = clock_2026();
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_EQ(mi.pending, 0);
    ASSERT_TRUE(mi.forecast_count >= 1);
    ASSERT_EQ(mi.forecast[0].days, 62); /* 5 idle days, then 57 steps */
}

TEST(mirage_read_caps_at_2099) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    MirageClock clk = { true, 99, 12, 1, 12, 0, 0 };
    set_offset(9557, 0, 0, 0);    /* clock set on 2026-03-01: local day 36495 - 9557 = 26938 */
    set_var_days(26938);
    box(0, 0x00008459, 1);        /* 57 days out: past 2099-12-31 (30 days left) */
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_TRUE(mi.clock_known);
    ASSERT_EQ(mi.forecast_count, 0);
}

TEST(mirage_read_clock_not_set_has_no_forecast) {
    reset();
    set_value(0x12345678);
    set_offset(9557, 0, 0, 0);
    box(0, 0x00008459, 1);
    MirageClock clk = clock_2026();
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_TRUE(mi.valid);
    ASSERT_TRUE(!mi.clock_set);
    ASSERT_EQ(mi.forecast_count, 0);
}

TEST(mirage_read_no_rtc_still_shows_today) {
    reset();
    set_value(0x12345678);
    set_clock_flag();
    party(2, 0x00001234, 1);
    MirageClock clk = { 0 };
    MirageInfo mi;
    read_info(&clk, &mi);
    ASSERT_TRUE(!mi.clock_known);
    ASSERT_EQ(mi.today.where, MIRAGE_PARTY);
    ASSERT_EQ(mi.forecast_count, 0);
}

TEST(mirage_read_bad_save_pointer_is_invalid) {
    reset();
    MirageClock clk = clock_2026();
    MirageInfo mi;
    static MirageCache mc;
    memset(&mc, 0, sizeof(mc));
    GameMem m = mem();
    mirage_read(&mc, &k_profile, &m, &clk, 0x01000000u, SB2, &mi);
    ASSERT_TRUE(!mi.valid);
}

void run_mirage_tests(void) {
    TEST_SUITE("mirage");
    RUN_TEST(mirage_step_matches_pokeemerald);
    RUN_TEST(mirage_day_count_matches_pokeemerald);
    RUN_TEST(mirage_local_days_borrows_like_the_game);
    RUN_TEST(mirage_search_finds_seeded_days);
    RUN_TEST(mirage_search_tie_prefers_first_owned);
    RUN_TEST(mirage_read_party_match_today);
    RUN_TEST(mirage_read_box_match_today_and_forecast);
    RUN_TEST(mirage_read_counts_eggs);
    RUN_TEST(mirage_read_pending_day_shifts_today_and_forecast);
    RUN_TEST(mirage_read_clock_behind_adds_gap);
    RUN_TEST(mirage_read_caps_at_2099);
    RUN_TEST(mirage_read_clock_not_set_has_no_forecast);
    RUN_TEST(mirage_read_no_rtc_still_shows_today);
    RUN_TEST(mirage_read_bad_save_pointer_is_invalid);
}
