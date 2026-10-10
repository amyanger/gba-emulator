#ifndef ENCOUNTERS_H
#define ENCOUNTERS_H

#include "game/game_mem.h"

typedef enum {
    ENC_LAND,
    ENC_WATER,
    ENC_ROCK_SMASH,
    ENC_OLD_ROD,
    ENC_GOOD_ROD,
    ENC_SUPER_ROD,
    ENC_METHOD_COUNT
} EncMethod;

typedef struct {
    uint16_t species;
    uint8_t min_level, max_level;
    uint8_t percent;
} EncounterEntry;

typedef struct {
    uint8_t count;
    EncounterEntry entries[12];
} EncounterList;

/* Look up the wild encounter header for a map in the ROM's
 * gWildMonHeaders and merge slots of the same species within each method.
 * Returns false when the map has no header. */
bool encounters_find(const GameMem* mem, uint32_t headers, uint16_t cap,
                     uint16_t species_count, uint8_t map_group, uint8_t map_num,
                     EncounterList out[ENC_METHOD_COUNT]);

#endif // ENCOUNTERS_H
