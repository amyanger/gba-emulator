#include "game/daycare.h"

uint8_t daycare_compat(const DaycareParent* a, const DaycareParent* b, DaycareCompatReason* reason) {
    *reason = DAYCARE_COMPAT_OK;
    if (a->egg_group[0] == EGG_GROUP_UNDISCOVERED || b->egg_group[0] == EGG_GROUP_UNDISCOVERED) {
        *reason = DAYCARE_UNDISCOVERED;
        return 0;
    }
    bool da = a->egg_group[0] == EGG_GROUP_DITTO, db = b->egg_group[0] == EGG_GROUP_DITTO;
    if (da && db) {
        *reason = DAYCARE_TWO_DITTO;
        return 0;
    }
    bool same_ot = a->ot_id == b->ot_id;
    if (da || db) return same_ot ? 20 : 50;
    MonGender ga = pokemon_gender(a->gender_ratio, a->personality);
    MonGender gb = pokemon_gender(b->gender_ratio, b->personality);
    if (ga == gb) {
        *reason = ga == MON_GENDER_NONE ? DAYCARE_GENDERLESS : DAYCARE_SAME_GENDER;
        return 0;
    }
    if (ga == MON_GENDER_NONE || gb == MON_GENDER_NONE) {
        *reason = DAYCARE_GENDERLESS;
        return 0;
    }
    bool shared = false;
    for (uint8_t i = 0; i < 2; i++)
        for (uint8_t j = 0; j < 2; j++)
            if (a->egg_group[i] == b->egg_group[j]) shared = true;
    if (!shared) {
        *reason = DAYCARE_NO_SHARED_GROUP;
        return 0;
    }
    if (a->species == b->species) return same_ot ? 50 : 70;
    return same_ot ? 20 : 50;
}

/* Rolls happen when slot 1's already-incremented steps end in 0xFF (daycare.c:890). */
uint16_t daycare_next_roll(uint32_t steps1) {
    uint16_t k = (uint16_t)(0xFFu - (steps1 & 0xFFu));
    return k == 0 ? 256 : k;
}

/* TryProduceOrHatchEgg's hatch check, simulated check by check (daycare.c:898-925). */
void daycare_hatch_steps(const DaycareSlot slots[6], uint8_t count, uint8_t step_counter,
                         uint32_t out[6]) {
    bool egg[6], fire[6];
    uint8_t cycles[6];
    uint8_t live = 0;
    if (count > 6) count = 6;
    for (uint8_t i = 0; i < 6; i++) {
        out[i] = 0;
        egg[i] = i < count && slots[i].egg && !slots[i].bad;
        fire[i] = i < count && !slots[i].bad && slots[i].fire;
        cycles[i] = i < count ? slots[i].cycles : 0;
        live += egg[i];
    }
    uint32_t to_next = step_counter == 255 ? 256u : 255u - step_counter;
    /* At most 6 eggs x 256 checks each. */
    for (uint32_t check = 1; live > 0 && check <= 6u * 257u; check++) {
        uint8_t sub = 1;
        for (uint8_t i = 0; i < count; i++)
            if (!egg[i] && fire[i]) sub = 2;
        for (uint8_t i = 0; i < count; i++) {
            if (!egg[i]) continue;
            if (cycles[i] != 0) {
                cycles[i] = (uint8_t)(cycles[i] - (cycles[i] >= sub ? sub : 1));
            } else {
                out[i] = to_next + 256u * (check - 1u);
                egg[i] = false;
                live--;
                break;
            }
        }
    }
}

uint8_t daycare_level_for_exp(const uint32_t table[101], uint32_t exp) {
    uint8_t lv = 1;
    while (lv <= 100 && table[lv] <= exp) lv++;
    return (uint8_t)(lv - 1);
}

uint32_t daycare_cost(uint8_t level_now, uint8_t level_deposit) {
    return 100u + 100u * (level_now > level_deposit ? (uint32_t)(level_now - level_deposit) : 0u);
}

/* DetermineEggSpeciesAndParentSlots + AlterEggSpeciesWithIncenseItem (daycare.c). */
uint16_t daycare_egg_species(const uint16_t species[2], const MonGender gender[2],
                             const uint16_t held_item[2], uint32_t pid, const uint16_t* parent,
                             uint16_t count, const GameProfile* p) {
    int8_t mother = -1;
    for (uint8_t i = 0; i < 2; i++) {
        if (species[i] == p->species_ditto)
            mother = (int8_t)(i ^ 1);
        else if (gender[i] == MON_GENDER_FEMALE)
            mother = (int8_t)i;
    }
    if (mother < 0) return 0;

    uint16_t baby = species[mother];
    for (uint8_t hop = 0; hop < 3 && baby < count && parent[baby] != 0; hop++) baby = parent[baby];

    if (baby == p->species_nidoran_f && (pid & 0x8000u)) baby = p->species_nidoran_m;
    if (baby == p->species_illumise && (pid & 0x8000u)) baby = p->species_volbeat;

    for (uint8_t k = 0; k < 2; k++) {
        if (baby != p->incense_baby[k]) continue;
        if (held_item[0] == p->incense_item[k] || held_item[1] == p->incense_item[k]) break;
        for (uint16_t s = 1; s < count; s++)
            if (parent[s] == baby) return s;
        break;
    }
    return baby;
}
