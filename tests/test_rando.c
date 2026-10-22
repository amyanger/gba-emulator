#include "test_harness.h"
#include "game_fixture.h"
#include "rando/rando.h"
#include <stdlib.h>

#define ROM_SIZE 0x1000000u
#define LS_DEFAULT 0x08F00000u /* shared learnset */
#define LS_TEN     0x08F00100u /* species 10's own learnset */
#define WILD_INFO  0x08F01000u
#define WILD_MONS  0x08F01100u
#define PARTY      0x08F02000u /* 16 bytes per trainer below */

static uint8_t* at(uint8_t* rom, uint32_t a) { return rom + (a - 0x08000000u); }
static void put16(uint8_t* rom, uint32_t a, uint16_t v) { fixture_put16(at(rom, a), v); }
static void put32(uint8_t* rom, uint32_t a, uint32_t v) { fixture_put32(at(rom, a), v); }
static uint16_t get16(uint8_t* rom, uint32_t a) { const uint8_t* p = at(rom, a); return (uint16_t)(p[0] | (p[1] << 8)); }

static const GameProfile* P = &g_emerald_profile;

static uint32_t trainer_addr(uint32_t t) { return P->trainers + 40u * t; }

/* Trainer t header: flags, party size, party pointer. */
static void put_trainer(uint8_t* rom, uint32_t t, uint8_t flags, uint8_t size, uint32_t party) {
    at(rom, trainer_addr(t))[0x00] = flags;
    at(rom, trainer_addr(t))[0x20] = size;
    put32(rom, trainer_addr(t) + 0x24, party);
}

/* One wild header (land only, 12 x species 263), starters 277/280/283, every species
 * learning {1@1, 2@1, 1@5, 3@7, 4@9, 5@10, 6@11}, move 1 power 40. */
static uint8_t* make_rom(void) {
    uint8_t* rom = calloc(1, ROM_SIZE);
    static const uint16_t ls[] = {1 | 1 << 9, 2 | 1 << 9, 1 | 5 << 9, 3 | 7 << 9, 4 | 9 << 9,
                                  5 | 10 << 9, 6 | 11 << 9, 0xFFFF};
    for (uint32_t i = 0; i < 8; i++) put16(rom, LS_DEFAULT + 2 * i, ls[i]);
    for (uint32_t s = 0; s < 412; s++) put32(rom, P->level_up_learnsets + 4 * s, LS_DEFAULT);
    at(rom, P->battle_moves + 12 * 1)[1] = 40;

    at(rom, P->wild_mon_headers)[0] = 0;
    at(rom, P->wild_mon_headers)[1] = 1;
    put32(rom, P->wild_mon_headers + 4, WILD_INFO);
    at(rom, P->wild_mon_headers + 20)[0] = 0xFF;
    put32(rom, WILD_INFO + 4, WILD_MONS);
    for (uint32_t i = 0; i < 12; i++) {
        at(rom, WILD_MONS + 4 * i)[0] = 2;
        at(rom, WILD_MONS + 4 * i)[1] = 3;
        put16(rom, WILD_MONS + 4 * i + 2, 263);
    }
    put16(rom, P->starter_mons, 277);
    put16(rom, P->starter_mons + 2, 280);
    put16(rom, P->starter_mons + 4, 283);
    return rom;
}

static bool in_pool(uint16_t s) {
    static const uint16_t legends[] = {144, 145, 146, 150, 151, 243, 244, 245, 249, 250, 251};
    if (s < 1 || s > 411 || (s >= 252 && s <= 276) || (s >= 401 && s <= 410)) return false;
    for (size_t i = 0; i < sizeof(legends) / sizeof(legends[0]); i++)
        if (legends[i] == s) return false;
    return true;
}

TEST(rando_parse_formats) {
    uint32_t seed = 0;
    ASSERT_TRUE(rando_parse("seed 7F3A91C2 v1\n", &seed));
    ASSERT_EQ_HEX(seed, 0x7F3A91C2u);
    ASSERT_TRUE(!rando_parse("seed 7F3A91C2 v2", &seed));
    ASSERT_TRUE(!rando_parse("seed zz v1", &seed));
    ASSERT_TRUE(!rando_parse("", &seed));
    char buf[32];
    rando_format(buf, sizeof(buf), 0x0000ABCDu);
    ASSERT_STR_EQ(buf, "seed 0000ABCD v1\n");
    ASSERT_TRUE(rando_parse(buf, &seed));
    ASSERT_EQ_HEX(seed, 0x0000ABCDu);
}

TEST(rando_file_roundtrip) {
    const char* save = "rando_test.sav";
    uint32_t seed = 0;
    ASSERT_TRUE(rando_clear(save));
    ASSERT_TRUE(!rando_load(save, &seed));
    ASSERT_TRUE(rando_store(save, 0xDEADBEEFu));
    ASSERT_TRUE(rando_load(save, &seed));
    ASSERT_EQ_HEX(seed, 0xDEADBEEFu);
    ASSERT_TRUE(rando_clear(save));
    ASSERT_TRUE(!rando_load(save, &seed));
}

TEST(rando_wild_and_starters_change_and_skip_legends) {
    uint8_t* rom = make_rom();
    rando_apply(rom, ROM_SIZE, P, 1);
    bool changed = false;
    for (uint32_t i = 0; i < 12; i++) {
        uint16_t s = get16(rom, WILD_MONS + 4 * i + 2);
        ASSERT_TRUE(in_pool(s));
        changed |= s != 263;
    }
    static const uint16_t old[3] = {277, 280, 283};
    for (uint32_t i = 0; i < 3; i++) {
        uint16_t s = get16(rom, P->starter_mons + 2 * i);
        ASSERT_TRUE(in_pool(s));
        changed |= s != old[i];
    }
    ASSERT_TRUE(changed);
    ASSERT_EQ(at(rom, WILD_MONS)[0], 2); /* levels untouched */
    ASSERT_EQ(at(rom, WILD_MONS)[1], 3);
    free(rom);
}

TEST(rando_deterministic_and_idempotent) {
    uint8_t* a = make_rom();
    uint8_t* b = make_rom();
    put_trainer(a, 1, 3, 1, PARTY + 16);
    put_trainer(b, 1, 3, 1, PARTY + 16);
    rando_apply(a, ROM_SIZE, P, 0x12345678u);
    rando_apply(b, ROM_SIZE, P, 0x12345678u);
    ASSERT_TRUE(memcmp(a, b, ROM_SIZE) == 0);
    rando_apply(b, ROM_SIZE, P, 0x12345678u);
    ASSERT_TRUE(memcmp(a, b, ROM_SIZE) == 0);
    rando_apply(b, ROM_SIZE, P, 0x87654321u);
    ASSERT_TRUE(memcmp(a, b, ROM_SIZE) != 0);
    free(a);
    free(b);
}

TEST(rando_trainer_custom_moves) {
    uint8_t* rom = make_rom();
    put_trainer(rom, 1, 3, 1, PARTY + 16);
    at(rom, PARTY + 16)[2] = 10;
    put16(rom, PARTY + 16 + 4, 1);
    put16(rom, PARTY + 16 + 6, 0x1234);
    rando_apply(rom, ROM_SIZE, P, 1);
    ASSERT_TRUE(in_pool(get16(rom, PARTY + 16 + 4)));
    ASSERT_TRUE(get16(rom, PARTY + 16 + 4) != 303);
    ASSERT_EQ_HEX(get16(rom, PARTY + 16 + 6), 0x1234);
    static const uint16_t want[4] = {2, 3, 4, 5};
    for (uint32_t k = 0; k < 4; k++) ASSERT_EQ(get16(rom, PARTY + 16 + 8 + 2 * k), want[k]);
    free(rom);
}

TEST(rando_bad_pointers_skipped) {
    uint8_t* rom = make_rom();
    static const uint32_t untouched[] = {0, 2, 3, 851, 852, 853, 854};
    for (size_t i = 0; i < sizeof(untouched) / sizeof(untouched[0]); i++) {
        uint32_t t = untouched[i];
        put_trainer(rom, t, 0, t == 2 ? 7 : 1, t == 3 ? 0x09FFFFF0u : PARTY + 16 * t);
        put16(rom, PARTY + 16 * t + 4, 1);
    }
    uint8_t* before = malloc(ROM_SIZE);
    memcpy(before, rom, ROM_SIZE);
    rando_apply(rom, ROM_SIZE, P, 1);
    for (size_t i = 0; i < sizeof(untouched) / sizeof(untouched[0]); i++) {
        uint32_t t = untouched[i];
        ASSERT_TRUE(memcmp(at(rom, PARTY + 16 * t), at(before, PARTY + 16 * t), 16) == 0);
    }
    /* Water, rock smash and fishing info pointers are 0: header untouched. */
    ASSERT_TRUE(memcmp(at(rom, P->wild_mon_headers), at(before, P->wild_mon_headers), 20) == 0);
    free(before);
    free(rom);
}

TEST(rando_starter_pool_needs_damage) {
    uint8_t* rom = make_rom();
    at(rom, P->battle_moves + 12 * 1)[1] = 0;
    put16(rom, LS_TEN, 7 | 1 << 9);
    put16(rom, LS_TEN + 2, 0xFFFF);
    put32(rom, P->level_up_learnsets + 4 * 10, LS_TEN);
    at(rom, P->battle_moves + 12 * 7)[1] = 40;
    rando_apply(rom, ROM_SIZE, P, 1);
    for (uint32_t i = 0; i < 3; i++) ASSERT_EQ(get16(rom, P->starter_mons + 2 * i), 10);
    free(rom);
}

TEST(rando_starter_pool_skips_reactive_moves) {
    uint8_t* rom = make_rom();
    at(rom, P->battle_moves + 12 * 1)[1] = 0;
    put16(rom, LS_TEN, 68 | 1 << 9); /* Counter only */
    put16(rom, LS_TEN + 2, 0xFFFF);
    put32(rom, P->level_up_learnsets + 4 * 10, LS_TEN);
    at(rom, P->battle_moves + 12 * 68)[1] = 40;
    put16(rom, LS_TEN + 8, 7 | 1 << 9);
    put16(rom, LS_TEN + 10, 0xFFFF);
    put32(rom, P->level_up_learnsets + 4 * 11, LS_TEN + 8);
    at(rom, P->battle_moves + 12 * 7)[1] = 40;
    for (uint32_t seed = 1; seed <= 16; seed++) {
        rando_apply(rom, ROM_SIZE, P, seed);
        for (uint32_t i = 0; i < 3; i++) ASSERT_EQ(get16(rom, P->starter_mons + 2 * i), 11);
    }
    free(rom);
}

TEST(rando_unsupported_is_noop) {
    uint8_t* rom = make_rom();
    uint8_t* before = malloc(ROM_SIZE);
    memcpy(before, rom, ROM_SIZE);
    ASSERT_TRUE(!rando_supported(&g_firered_profile));
    ASSERT_TRUE(!rando_supported(NULL));
    ASSERT_TRUE(rando_supported(P));
    rando_apply(rom, ROM_SIZE, &g_firered_profile, 1);
    rando_apply(rom, ROM_SIZE, NULL, 1);
    ASSERT_TRUE(memcmp(rom, before, ROM_SIZE) == 0);
    free(before);
    free(rom);
}

void run_rando_tests(void) {
    TEST_SUITE("rando");
    RUN_TEST(rando_parse_formats);
    RUN_TEST(rando_file_roundtrip);
    RUN_TEST(rando_wild_and_starters_change_and_skip_legends);
    RUN_TEST(rando_deterministic_and_idempotent);
    RUN_TEST(rando_trainer_custom_moves);
    RUN_TEST(rando_bad_pointers_skipped);
    RUN_TEST(rando_starter_pool_needs_damage);
    RUN_TEST(rando_starter_pool_skips_reactive_moves);
    RUN_TEST(rando_unsupported_is_noop);
}
