#include "game/pokemon.h"
#include <string.h>

#define MON_PERSONALITY 0x00
#define MON_OT_ID       0x04
#define MON_NICKNAME    0x08
#define MON_FLAGS       0x13
#define MON_CHECKSUM    0x1C
#define MON_SECURE      0x20
#define MON_STATUS      0x50
#define MON_LEVEL       0x54
#define MON_HP          0x56
#define MON_MAX_HP      0x58

#define MON_FLAG_BAD_EGG     0x01
#define MON_FLAG_HAS_SPECIES 0x02
#define MON_FLAG_IS_EGG      0x04

/* Slot (0-3) of substruct type Growth, Attacks, EVs, Misc for each
 * personality % 24 (pokemon.c GetSubstruct). */
static const uint8_t k_substruct_slot[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2}, {0, 2, 3, 1}, {0, 3, 2, 1},
    {1, 0, 2, 3}, {1, 0, 3, 2}, {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2}, {2, 3, 0, 1}, {3, 2, 0, 1},
    {1, 2, 3, 0}, {1, 3, 2, 0}, {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

bool pokemon_decode(const uint8_t* raw, bool party, MonData* out) {
    uint8_t secure[48];
    uint16_t sum = 0;

    memset(out, 0, sizeof(*out));
    out->personality = rd32(raw + MON_PERSONALITY);
    out->ot_id = rd32(raw + MON_OT_ID);
    if (!(raw[MON_FLAGS] & MON_FLAG_HAS_SPECIES)) return false;
    out->present = true;
    memcpy(out->nickname_raw, raw + MON_NICKNAME, sizeof(out->nickname_raw));

    uint32_t key = out->personality ^ out->ot_id;
    for (int i = 0; i < 48; i += 4) {
        uint32_t w = rd32(raw + MON_SECURE + i) ^ key;
        secure[i] = (uint8_t)w;
        secure[i + 1] = (uint8_t)(w >> 8);
        secure[i + 2] = (uint8_t)(w >> 16);
        secure[i + 3] = (uint8_t)(w >> 24);
    }
    for (int i = 0; i < 48; i += 2) sum = (uint16_t)(sum + rd16(secure + i));
    if (sum != rd16(raw + MON_CHECKSUM) || (raw[MON_FLAGS] & MON_FLAG_BAD_EGG)) {
        out->bad = true;
        return true;
    }

    const uint8_t* slot = k_substruct_slot[out->personality % 24];
    const uint8_t* growth = secure + 12 * slot[0];
    const uint8_t* attacks = secure + 12 * slot[1];
    const uint8_t* evs = secure + 12 * slot[2];
    const uint8_t* misc = secure + 12 * slot[3];

    out->species = rd16(growth);
    out->held_item = rd16(growth + 2);
    out->experience = rd32(growth + 4);
    out->friendship = growth[9];
    for (int i = 0; i < 4; i++) {
        out->moves[i] = rd16(attacks + 2 * i);
        out->pp[i] = attacks[8 + i];
    }
    memcpy(out->evs, evs, 6);
    out->iv_word = rd32(misc + 4);
    pokemon_unpack_ivs(out->iv_word, out->ivs);
    out->is_egg = ((out->iv_word >> 30) & 1) || (raw[MON_FLAGS] & MON_FLAG_IS_EGG);
    out->ability_num = (uint8_t)((out->iv_word >> 31) & 1);

    if (party) {
        out->status = rd32(raw + MON_STATUS);
        out->level = raw[MON_LEVEL];
        out->hp = rd16(raw + MON_HP);
        out->max_hp = rd16(raw + MON_MAX_HP);
    }
    return true;
}

void pokemon_unpack_ivs(uint32_t iv_word, uint8_t ivs[6]) {
    for (int i = 0; i < 6; i++) ivs[i] = (uint8_t)((iv_word >> (5 * i)) & 0x1F);
}

uint8_t pokemon_nature(uint32_t personality) {
    return (uint8_t)(personality % 25);
}

void pokemon_nature_effect(uint8_t nature, int8_t* up, int8_t* down) {
    int8_t u = (int8_t)(nature / 5), d = (int8_t)(nature % 5);
    *up = (u == d) ? -1 : u;
    *down = (u == d) ? -1 : d;
}

MonGender pokemon_gender(uint8_t gender_ratio, uint32_t personality) {
    if (gender_ratio == 0xFF) return MON_GENDER_NONE;
    if (gender_ratio == 0xFE) return MON_GENDER_FEMALE;
    if (gender_ratio == 0x00) return MON_GENDER_MALE;
    return gender_ratio > (personality & 0xFF) ? MON_GENDER_FEMALE : MON_GENDER_MALE;
}

/* battle_script_commands.c Cmd_hiddenpowercalc */
uint8_t pokemon_hidden_power_type(const uint8_t ivs[6]) {
    uint32_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint32_t)(ivs[i] & 1) << i;
    uint32_t type = 15 * bits / 63 + 1;
    if (type >= 9) type++;  /* skip TYPE_MYSTERY */
    return (uint8_t)type;
}

uint8_t pokemon_hidden_power_power(const uint8_t ivs[6]) {
    uint32_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint32_t)((ivs[i] >> 1) & 1) << i;
    return (uint8_t)(40 * bits / 63 + 30);
}
