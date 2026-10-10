#include "test_harness.h"
#include "game_fixture.h"
#include "game/pokemon.h"

TEST(pokemon_decodes_all_24_substruct_orders) {
    uint8_t plain[4][12];
    uint8_t raw[100];
    fixture_plain_mon(plain, 277);
    for (uint32_t n = 0; n < 24; n++) {
        uint32_t personality = 0x10000 * 24 + n;  /* personality % 24 == n */
        fixture_encode_mon(raw, personality, 0xA5A55A5A, plain, 0x02);
        MonData d;
        ASSERT_TRUE(pokemon_decode(raw, false, &d));
        ASSERT_TRUE(!d.bad);
        ASSERT_EQ(d.species, 277);
        ASSERT_EQ(d.held_item, 139);
        ASSERT_EQ(d.experience, 1000);
        ASSERT_EQ(d.friendship, 70);
        ASSERT_EQ(d.moves[0], 33);
        ASSERT_EQ(d.moves[1], 45);
        ASSERT_EQ(d.pp[1], 40);
        ASSERT_EQ(d.evs[5], 6);
        ASSERT_EQ(d.ivs[0], 31);
        ASSERT_EQ(d.ivs[5], 31);
        ASSERT_EQ(d.ability_num, 1);
    }
}

TEST(pokemon_checksum_mismatch_is_bad_and_raw_untouched) {
    uint8_t plain[4][12];
    uint8_t raw[100], before[100];
    fixture_plain_mon(plain, 1);
    fixture_encode_mon(raw, 12345, 678, plain, 0x02);
    raw[0x1C] ^= 0x01;
    memcpy(before, raw, sizeof(raw));
    MonData d;
    ASSERT_TRUE(pokemon_decode(raw, true, &d));
    ASSERT_TRUE(d.bad);
    ASSERT_MEM_EQ(raw, before, sizeof(raw));
}

TEST(pokemon_empty_slot_is_not_present) {
    uint8_t raw[100] = {0};
    MonData d;
    ASSERT_TRUE(!pokemon_decode(raw, true, &d));
    ASSERT_TRUE(!d.present);
}

TEST(pokemon_party_fields_and_egg_flag) {
    uint8_t plain[4][12];
    uint8_t raw[100];
    fixture_plain_mon(plain, 1);
    fixture_put32(plain[3] + 0x4, (1u << 30));  /* isEgg, IVs 0 */
    fixture_encode_mon(raw, 99, 1, plain, 0x06);
    raw[0x54] = 12;
    fixture_put16(raw + 0x56, 20);
    fixture_put16(raw + 0x58, 33);
    fixture_put32(raw + 0x50, 0x40);
    MonData d;
    ASSERT_TRUE(pokemon_decode(raw, true, &d));
    ASSERT_TRUE(d.is_egg);
    ASSERT_EQ(d.level, 12);
    ASSERT_EQ(d.hp, 20);
    ASSERT_EQ(d.max_hp, 33);
    ASSERT_EQ(d.status, 0x40);
}

TEST(pokemon_nature_and_effects) {
    int8_t up, down;
    ASSERT_EQ(pokemon_nature(28), 3);              /* 28 % 25 = Adamant */
    pokemon_nature_effect(3, &up, &down);          /* Adamant: +Atk -SpA */
    ASSERT_EQ(up, 0); ASSERT_EQ(down, 3);
    pokemon_nature_effect(15, &up, &down);         /* Modest: +SpA -Atk */
    ASSERT_EQ(up, 3); ASSERT_EQ(down, 0);
    pokemon_nature_effect(0, &up, &down);          /* Hardy: neutral */
    ASSERT_EQ(up, -1); ASSERT_EQ(down, -1);
}

TEST(pokemon_gender_rules) {
    ASSERT_EQ(pokemon_gender(0xFF, 0x00), MON_GENDER_NONE);
    ASSERT_EQ(pokemon_gender(0xFE, 0xFF), MON_GENDER_FEMALE);
    ASSERT_EQ(pokemon_gender(0x00, 0x00), MON_GENDER_MALE);
    ASSERT_EQ(pokemon_gender(127, 126), MON_GENDER_FEMALE);
    ASSERT_EQ(pokemon_gender(127, 127), MON_GENDER_MALE);
}

TEST(pokemon_hidden_power) {
    const uint8_t all31[6] = {31, 31, 31, 31, 31, 31};
    const uint8_t all0[6] = {0, 0, 0, 0, 0, 0};
    const uint8_t hp30[6] = {30, 31, 31, 31, 31, 31};
    const uint8_t mixed[6] = {31, 30, 31, 30, 31, 30};
    ASSERT_EQ(pokemon_hidden_power_type(all31), 17);  /* Dark */
    ASSERT_EQ(pokemon_hidden_power_power(all31), 70);
    ASSERT_EQ(pokemon_hidden_power_type(all0), 1);    /* Fighting */
    ASSERT_EQ(pokemon_hidden_power_power(all0), 30);
    ASSERT_EQ(pokemon_hidden_power_type(hp30), 16);   /* Dragon */
    ASSERT_EQ(pokemon_hidden_power_type(mixed), 6);   /* Bug */
}

void run_game_pokemon_tests(void) {
    TEST_SUITE("game_pokemon");
    RUN_TEST(pokemon_decodes_all_24_substruct_orders);
    RUN_TEST(pokemon_checksum_mismatch_is_bad_and_raw_untouched);
    RUN_TEST(pokemon_empty_slot_is_not_present);
    RUN_TEST(pokemon_party_fields_and_egg_flag);
    RUN_TEST(pokemon_nature_and_effects);
    RUN_TEST(pokemon_gender_rules);
    RUN_TEST(pokemon_hidden_power);
}
