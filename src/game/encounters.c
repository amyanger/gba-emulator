#include "game/encounters.h"
#include <string.h>

#define WILD_HEADER_SIZE 20

/* Slot odds are compiled into src/wild_encounter.c, not stored in a table. */
static const uint8_t k_land_odds[12] = {20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1};
static const uint8_t k_water_odds[5] = {60, 30, 5, 4, 1};
static const uint8_t k_fish_odds[10] = {70, 30, 60, 20, 20, 40, 40, 15, 4, 1};

static void enc_add(EncounterList* list, uint16_t species, uint8_t lo, uint8_t hi,
                    uint8_t percent) {
    for (uint8_t i = 0; i < list->count; i++) {
        EncounterEntry* e = &list->entries[i];
        if (e->species != species) continue;
        e->percent = (uint8_t)(e->percent + percent);
        if (lo < e->min_level) e->min_level = lo;
        if (hi > e->max_level) e->max_level = hi;
        return;
    }
    if (list->count >= 12) return;
    EncounterEntry* e = &list->entries[list->count++];
    e->species = species;
    e->min_level = lo;
    e->max_level = hi;
    e->percent = percent;
}

typedef struct {
    uint8_t info_offset;   /* pointer offset inside the 20-byte header */
    uint8_t first, count;
    const uint8_t* odds;
} MethodSpec;

static const MethodSpec k_methods[ENC_METHOD_COUNT] = {
    [ENC_LAND] = {4, 0, 12, k_land_odds},
    [ENC_WATER] = {8, 0, 5, k_water_odds},
    [ENC_ROCK_SMASH] = {12, 0, 5, k_water_odds},
    [ENC_OLD_ROD] = {16, 0, 2, k_fish_odds},
    [ENC_GOOD_ROD] = {16, 2, 3, k_fish_odds + 2},
    [ENC_SUPER_ROD] = {16, 5, 5, k_fish_odds + 5},
};

/* info_ptr -> struct WildPokemonInfo {u8 rate; pad; const WildPokemon* mons}. */
int encounters_header_slots(const GameMem* mem, uint32_t headers, uint16_t index,
                            uint16_t species_count, EncMethod method,
                            uint8_t* group, uint8_t* num, EncSlot out[12]) {
    if ((unsigned)method >= ENC_METHOD_COUNT) return -1;
    uint32_t h = headers + (uint32_t)index * WILD_HEADER_SIZE;
    if (!game_mem_read8(mem, h, group) || !game_mem_read8(mem, h + 1, num)) return -1;
    if (*group == 0xFF) return -1;

    const MethodSpec* ms = &k_methods[method];
    uint32_t info = 0, mons;
    game_mem_read32(mem, h + ms->info_offset, &info);
    if (info == 0 || !game_mem_read32(mem, info + 4, &mons)) return 0;
    int n = 0;
    for (uint8_t i = 0; i < ms->count; i++) {
        uint8_t raw[4];
        if (!game_mem_copy(mem, mons + 4u * (ms->first + i), raw, 4)) break;
        uint16_t species = (uint16_t)(raw[2] | (raw[3] << 8));
        if (species == 0 || species >= species_count) continue;
        out[n].species = species;
        out[n].min_level = raw[0];
        out[n].max_level = raw[1];
        out[n].percent = ms->odds[i];
        n++;
    }
    return n;
}

bool encounters_find(const GameMem* mem, uint32_t headers, uint16_t cap,
                     uint16_t species_count, uint8_t map_group, uint8_t map_num,
                     EncounterList out[ENC_METHOD_COUNT]) {
    memset(out, 0, sizeof(EncounterList) * ENC_METHOD_COUNT);
    for (uint16_t i = 0; i < cap; i++) {
        uint8_t group, num;
        EncSlot slots[12];
        if (encounters_header_slots(mem, headers, i, species_count, ENC_LAND, &group, &num,
                                    slots) < 0)
            return false;
        if (group != map_group || num != map_num) continue;
        for (int m = 0; m < ENC_METHOD_COUNT; m++) {
            int n = encounters_header_slots(mem, headers, i, species_count, (EncMethod)m,
                                            &group, &num, slots);
            for (int k = 0; k < n; k++)
                enc_add(&out[m], slots[k].species, slots[k].min_level, slots[k].max_level,
                        slots[k].percent);
        }
        return true;
    }
    return false;
}
