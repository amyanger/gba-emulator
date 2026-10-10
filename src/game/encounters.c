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

/* info_ptr -> struct WildPokemonInfo {u8 rate; pad; const WildPokemon* mons}.
 * Reads slots [first, first+count) using odds[0..count). */
static void enc_read(const GameMem* mem, uint32_t info_ptr, uint8_t first, uint8_t count,
                     const uint8_t* odds, uint16_t species_count, EncounterList* out) {
    uint32_t mons;
    if (info_ptr == 0 || !game_mem_read32(mem, info_ptr + 4, &mons)) return;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t raw[4];
        if (!game_mem_copy(mem, mons + 4u * (first + i), raw, 4)) return;
        uint16_t species = (uint16_t)(raw[2] | (raw[3] << 8));
        if (species == 0 || species >= species_count) continue;
        enc_add(out, species, raw[0], raw[1], odds[i]);
    }
}

bool encounters_find(const GameMem* mem, uint32_t headers, uint16_t cap,
                     uint16_t species_count, uint8_t map_group, uint8_t map_num,
                     EncounterList out[ENC_METHOD_COUNT]) {
    memset(out, 0, sizeof(EncounterList) * ENC_METHOD_COUNT);
    for (uint16_t i = 0; i < cap; i++) {
        uint32_t h = headers + (uint32_t)i * WILD_HEADER_SIZE;
        uint8_t group, num;
        if (!game_mem_read8(mem, h, &group) || !game_mem_read8(mem, h + 1, &num)) return false;
        if (group == 0xFF) return false;
        if (group != map_group || num != map_num) continue;

        uint32_t land = 0, water = 0, rock = 0, fish = 0;
        game_mem_read32(mem, h + 4, &land);
        game_mem_read32(mem, h + 8, &water);
        game_mem_read32(mem, h + 12, &rock);
        game_mem_read32(mem, h + 16, &fish);
        enc_read(mem, land, 0, 12, k_land_odds, species_count, &out[ENC_LAND]);
        enc_read(mem, water, 0, 5, k_water_odds, species_count, &out[ENC_WATER]);
        enc_read(mem, rock, 0, 5, k_water_odds, species_count, &out[ENC_ROCK_SMASH]);
        enc_read(mem, fish, 0, 2, k_fish_odds, species_count, &out[ENC_OLD_ROD]);
        enc_read(mem, fish, 2, 3, k_fish_odds + 2, species_count, &out[ENC_GOOD_ROD]);
        enc_read(mem, fish, 5, 5, k_fish_odds + 5, species_count, &out[ENC_SUPER_ROD]);
        return true;
    }
    return false;
}
