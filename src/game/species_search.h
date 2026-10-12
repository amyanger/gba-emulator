#ifndef SPECIES_SEARCH_H
#define SPECIES_SEARCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/emerald_profile.h"
#include "game/encounters.h"

#define SPECIES_MAX 412
#define SPECIES_KEY_LEN 12
#define SEARCH_QUERY_MAX 16
#define SPECIES_NAME_LEN 16 /* equals GAME_NAME_LEN, asserted in species_search.c */
#define SPECIES_MAP_NAME_LEN 24 /* equals GAME_MAP_NAME_LEN, asserted in species_search.c */
#define SPECIES_ROWS_MAX 128

typedef struct {
    bool ok;
    uint16_t count;                         /* real species (national 1..386) */
    uint16_t order[SPECIES_MAX];            /* species ids in national order, [0, count) */
    char key[SPECIES_MAX][SPECIES_KEY_LEN]; /* a-z0-9 only */
    char name[SPECIES_MAX][SPECIES_NAME_LEN];/* decoded uppercase, Nidoran gender letter dropped */
    uint8_t gender_mark[SPECIES_MAX];       /* 0 none, 1 female (raw 0xB6), 2 male (raw 0xB5) */
    uint16_t national[SPECIES_MAX];         /* 0 for non-real species */
    uint16_t parent[SPECIES_MAX];           /* 0 = none */
    uint8_t wild[(SPECIES_MAX + 7) / 8];    /* reachable header slot, or Feebas */
} SpeciesIndex;

void species_index_build(SpeciesIndex* ix, const GameProfile* p, const GameMem* rom);
bool species_index_is_wild(const SpeciesIndex* ix, uint16_t species);
/* Lowercase, keep a-z0-9, drop everything else; out has SPECIES_KEY_LEN+4 room. */
void species_search_normalize(const char* in, char* out, size_t out_size);
/* Fills out with species ids: exact, then prefix, then substring, each in national order.
 * Empty (after normalizing) query returns all real species. Returns the count. */
uint16_t species_search_query(const SpeciesIndex* ix, const char* query, uint16_t out[SPECIES_MAX]);

typedef enum { SPECIES_WILD, SPECIES_EVOLVE, SPECIES_BREED, SPECIES_EVOLVE_NOT_WILD, SPECIES_NOT_WILD } SpeciesKind;
/* Row methods: EncMethod values plus these. */
typedef enum { SROW_DIVE = ENC_METHOD_COUNT, SROW_ANY_ROD } SpeciesRowMethodExtra;
enum { SROW_MIRAGE = 1, SROW_FEEBAS_TILES = 2, SROW_POSTGAME = 4, SROW_LOW_TIDE = 8 };

typedef struct {
    uint8_t method;                   /* EncMethod, SROW_DIVE or SROW_ANY_ROD */
    uint8_t mapsec;                   /* merge key; for Dive the surface route's mapsec */
    char place[SPECIES_MAP_NAME_LEN]; /* uppercase ROM name */
    uint8_t min_level, max_level, pct_lo, pct_hi, flags;
} SpeciesRow;

typedef struct {
    uint16_t from;                    /* species that evolves */
    uint16_t method, param;           /* raw evolution method 1..15 and param */
    char item[SPECIES_NAME_LEN];      /* item name for methods 6/7, else empty */
} SpeciesStep;

typedef struct {
    uint16_t species;
    SpeciesKind kind;
    uint16_t rows_species;            /* whose rows are listed (self, ancestor or breed target) */
    uint8_t step_count;               /* evolve steps, nearest first: steps[0].from = parent */
    SpeciesStep steps[3];
    bool steps_truncated;             /* 3-hop cap hit before a root or wild ancestor */
    char incense[SPECIES_NAME_LEN];   /* BREED with an incense rule, else empty */
    uint16_t row_count, row_total;    /* row_total > row_count when truncated */
    SpeciesRow rows[SPECIES_ROWS_MAX];
} SpeciesDetail;

/* How to get `species`: its kind, evolve steps and the wild rows of rows_species. ROM only. */
void species_search_detail(const SpeciesIndex* ix, const GameProfile* p, const GameMem* rom,
                           uint16_t species, SpeciesDetail* out);

#endif // SPECIES_SEARCH_H
