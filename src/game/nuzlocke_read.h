#ifndef NUZLOCKE_READ_H
#define NUZLOCKE_READ_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/emerald_profile.h"

/* Raw facts the Nuzlocke tracker needs, read straight from memory each frame.
 * Kept apart from GameSnapshot: play time changes every frame and the panel
 * redraws whenever the snapshot changes. */

#define NUZ_PARTY_MAX   6
#define NUZ_BOX_MAX     420  /* 14 boxes x 30 */
#define NUZ_SPECIES_MAX 512  /* family table size; Emerald has 412 species ids */
#define NUZ_SEC_NONE    0xFF /* no usable region map section */
#define NUZ_SPECIES_SHEDINJA 303

/* gBattleTypeFlags bits (include/constants/battle.h) */
#define NUZ_BATTLE_TRAINER        (1u << 3)
#define NUZ_BATTLE_FIRST_BATTLE   (1u << 4)
#define NUZ_BATTLE_WALLY_TUTORIAL (1u << 9)

/* gBattleOutcome */
#define NUZ_OUTCOME_CAUGHT 7

/* A Pokemon's identity: personality + OT ID. Shedinja is created as a copy of
 * Nincada (same personality and OT), so it gets variant 1. */
typedef struct {
    uint32_t pid, ot;
    uint8_t variant;
} NuzKey;

typedef struct {
    NuzKey key;
    uint16_t species;
    uint8_t level;
    uint8_t met;          /* met location (mapsec) */
    bool egg;
    uint16_t hp;          /* party only */
    uint8_t nick[10];     /* Gen 3 encoded */
} NuzMon;

typedef struct {
    bool valid;               /* save blocks readable */
    uint32_t trainer_id;      /* SaveBlock2 playerTrainerId, public | secret << 16 */
    uint32_t play_time;       /* in frames */
    uint8_t mapsec;
    bool in_battle;
    uint32_t battle_flags;
    uint8_t battle_outcome;
    uint16_t enemy_species;   /* gEnemyParty[0]; 0 when none */
    uint8_t enemy_level;
    uint32_t enemy_pid, enemy_ot;
    int8_t active_party;      /* party slot of the player's active battler, -1 when unknown */
    uint8_t party_count;
    NuzMon party[NUZ_PARTY_MAX];
    bool has_boxes;           /* boxes[] read this time (otherwise they are from an earlier read) */
    uint16_t box_count;
    NuzMon boxes[NUZ_BOX_MAX];
} NuzFacts;

bool nuz_key_eq(NuzKey a, NuzKey b);
/* Fills f; f->valid is false while the save blocks aren't readable. Boxes are
 * read only when with_boxes is set; otherwise f keeps the previous box list. */
bool nuz_read(const GameProfile* p, const GameMem* m, bool with_boxes, NuzFacts* f);
/* family[s] = the species at the root of s's evolution line (s itself when
 * nothing evolves into it). Returns false when the table can't be read. */
bool nuz_families(const GameProfile* p, const GameMem* m, uint16_t family[NUZ_SPECIES_MAX]);
/* Shiny check with the Pokemon's own OT ID (wild ones carry the player's). */
bool nuz_is_shiny(uint32_t pid, uint32_t ot);
/* Play time in frames -> "h:mm:ss". */
void nuz_format_time(uint32_t frames, char* out, uint32_t out_size);

#endif // NUZLOCKE_READ_H
