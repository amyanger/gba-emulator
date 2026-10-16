#ifndef FEEBAS_H
#define FEEBAS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "game/game_mem.h"
#include "game/game_profile.h"

/* The Route 119 tiles that give Feebas, worked out the way the game does
 * (pret src/wild_encounter.c CheckFeebas, src/item_use.c CanFish). */

#define FEEBAS_SPOT_COUNT 6
#define FEEBAS_MAX_TILES  32

typedef struct {
    int16_t x, y;       /* map coords (VMap - 7) */
    uint16_t spot;      /* the game's fishing spot id */
    bool fishable;      /* false: collision set, nowhere to cast from */
    bool bridge;        /* bridge over water, fished while surfing beside it */
} FeebasTile;

typedef struct {
    bool active;        /* on Route 119 with a readable map */
    uint16_t seed;
    uint16_t spots[FEEBAS_SPOT_COUNT];
    uint8_t count;
    FeebasTile tile[FEEBAS_MAX_TILES];
} FeebasState;

/* The six spot ids the game draws from this seed, duplicates included. */
void feebas_spot_ids(uint16_t seed, uint16_t out[FEEBAS_SPOT_COUNT]);
/* Tiles for a given seed on the current map. Zeroes out first; false when inactive. */
bool feebas_compute(const GameProfile* p, const GameMem* m, uint16_t seed, FeebasState* out);
/* Same, with the seed the game uses now (SaveBlock1 dewfordTrends[0].rand). */
bool feebas_read(const GameProfile* p, const GameMem* m, FeebasState* out);
/* "[feebas]" section for --game-dump. */
bool feebas_dump(const FeebasState* s, FILE* out);

#endif // FEEBAS_H
