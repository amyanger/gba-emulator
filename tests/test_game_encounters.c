#include "test_harness.h"
#include "game_fixture.h"
#include "game/encounters.h"

static uint8_t s_rom[0x1000];

static GameMem rom_mem(void) {
    GameMem m = { NULL, NULL, s_rom, sizeof(s_rom) };
    return m;
}

static void put_mon(uint32_t off, uint8_t lo, uint8_t hi, uint16_t species) {
    s_rom[off] = lo; s_rom[off + 1] = hi; fixture_put16(s_rom + off + 2, species);
}

/* Header 0: map 0:16 with land + fishing. Header 1: map 1:2, no data.
 * Header 2: terminator. */
static void build_rom(void) {
    memset(s_rom, 0, sizeof(s_rom));
    s_rom[0x100] = 0; s_rom[0x101] = 16;
    fixture_put32(s_rom + 0x104, 0x08000200);  /* land info */
    fixture_put32(s_rom + 0x110, 0x08000220);  /* fishing info */
    s_rom[0x114] = 1; s_rom[0x115] = 2;
    s_rom[0x128] = 0xFF; s_rom[0x129] = 0xFF;
    s_rom[0x200] = 20; fixture_put32(s_rom + 0x204, 0x08000300);
    s_rom[0x220] = 30; fixture_put32(s_rom + 0x224, 0x08000400);
    put_mon(0x300, 2, 2, 10);
    put_mon(0x304, 2, 3, 11);
    put_mon(0x308, 3, 3, 10);
    for (int i = 3; i < 12; i++) put_mon(0x300 + 4 * (uint32_t)i, 3, 4, 12);
    for (int i = 0; i < 10; i++) put_mon(0x400 + 4 * (uint32_t)i, 5, 10, (uint16_t)(20 + i));
}

TEST(encounters_merge_land_slots_by_species) {
    build_rom();
    GameMem m = rom_mem();
    EncounterList out[ENC_METHOD_COUNT];
    ASSERT_TRUE(encounters_find(&m, 0x08000100, 200, 412, 0, 16, out));
    ASSERT_EQ(out[ENC_LAND].count, 3);
    ASSERT_EQ(out[ENC_LAND].entries[0].species, 10);
    ASSERT_EQ(out[ENC_LAND].entries[0].percent, 30);
    ASSERT_EQ(out[ENC_LAND].entries[0].min_level, 2);
    ASSERT_EQ(out[ENC_LAND].entries[0].max_level, 3);
    ASSERT_EQ(out[ENC_LAND].entries[1].species, 11);
    ASSERT_EQ(out[ENC_LAND].entries[1].percent, 20);
    ASSERT_EQ(out[ENC_LAND].entries[2].species, 12);
    ASSERT_EQ(out[ENC_LAND].entries[2].percent, 50);
    ASSERT_EQ(out[ENC_WATER].count, 0);
}

TEST(encounters_split_fishing_by_rod) {
    build_rom();
    GameMem m = rom_mem();
    EncounterList out[ENC_METHOD_COUNT];
    ASSERT_TRUE(encounters_find(&m, 0x08000100, 200, 412, 0, 16, out));
    ASSERT_EQ(out[ENC_OLD_ROD].count, 2);
    ASSERT_EQ(out[ENC_OLD_ROD].entries[0].percent, 70);
    ASSERT_EQ(out[ENC_OLD_ROD].entries[1].percent, 30);
    ASSERT_EQ(out[ENC_GOOD_ROD].count, 3);
    ASSERT_EQ(out[ENC_GOOD_ROD].entries[0].species, 22);
    ASSERT_EQ(out[ENC_GOOD_ROD].entries[0].percent, 60);
    ASSERT_EQ(out[ENC_SUPER_ROD].count, 5);
    ASSERT_EQ(out[ENC_SUPER_ROD].entries[4].species, 29);
    ASSERT_EQ(out[ENC_SUPER_ROD].entries[4].percent, 1);
}

TEST(encounters_missing_map_and_terminator) {
    build_rom();
    GameMem m = rom_mem();
    EncounterList out[ENC_METHOD_COUNT];
    ASSERT_TRUE(encounters_find(&m, 0x08000100, 200, 412, 1, 2, out));  /* header, no data */
    ASSERT_EQ(out[ENC_LAND].count, 0);
    ASSERT_TRUE(!encounters_find(&m, 0x08000100, 200, 412, 9, 9, out)); /* stops at 0xFF */
}

TEST(encounters_cap_and_bad_pointers) {
    build_rom();
    GameMem m = rom_mem();
    EncounterList out[ENC_METHOD_COUNT];
    ASSERT_TRUE(!encounters_find(&m, 0x08000100, 1, 412, 1, 2, out));   /* cap reached */
    fixture_put32(s_rom + 0x104, 0x09FFFFF0);                           /* land ptr out of ROM */
    ASSERT_TRUE(encounters_find(&m, 0x08000100, 200, 412, 0, 16, out));
    ASSERT_EQ(out[ENC_LAND].count, 0);
    ASSERT_EQ(out[ENC_OLD_ROD].count, 2);
}

TEST(encounters_skip_out_of_range_species) {
    build_rom();
    put_mon(0x300, 2, 2, 0xFFFF);
    GameMem m = rom_mem();
    EncounterList out[ENC_METHOD_COUNT];
    ASSERT_TRUE(encounters_find(&m, 0x08000100, 200, 412, 0, 16, out));
    ASSERT_EQ(out[ENC_LAND].entries[0].species, 11);
}

void run_game_encounters_tests(void) {
    TEST_SUITE("game_encounters");
    RUN_TEST(encounters_merge_land_slots_by_species);
    RUN_TEST(encounters_split_fishing_by_rod);
    RUN_TEST(encounters_missing_map_and_terminator);
    RUN_TEST(encounters_cap_and_bad_pointers);
    RUN_TEST(encounters_skip_out_of_range_species);
}
