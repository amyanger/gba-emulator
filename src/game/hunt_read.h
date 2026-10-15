#ifndef HUNT_READ_H
#define HUNT_READ_H

#include "game/emerald_profile.h"
#include "game/game_mem.h"
#include "game/nuzlocke_read.h"
#include "game/wildgen.h"
#include <stdbool.h>
#include <stdint.h>

/* What the Shiny Hunt page reads each frame on top of NuzFacts: the RNG, the battle kind and the
 * inputs of wildgen_step. Facts: docs/superpowers/specs/2026-10-13-shiny-hunt-factcheck.md. */

typedef enum {
    HUNT_BATTLE_NONE,
    HUNT_BATTLE_WILD,   /* CB2_EndWildBattle, not Pike/Pyramid (roamers too; tell by species) */
    HUNT_BATTLE_SAFARI, /* CB2_EndSafariBattle */
    HUNT_BATTLE_STATIC, /* CB2_EndScriptedWildBattle */
    HUNT_BATTLE_OTHER   /* trainer, link, frontier, tutorial: not counted */
} HuntBattleKind;

typedef struct {
    bool valid;               /* save blocks readable (NuzFacts.valid) */
    uint32_t rng, rand_count; /* gRngValue, sRandCount */
    uint32_t otid;
    uint8_t map_group, map_num;
    bool in_battle;
    uint8_t battle_kind; /* HuntBattleKind, from gMain.savedCallback + flags (R7) */
    bool enemy_complete; /* gEnemyParty[0] decodes with a valid checksum */
    uint16_t enemy_species;
    uint32_t enemy_pid, enemy_ot;
    uint8_t enemy_level;
    bool predictable;  /* land, not Safari, no outbreak on map, lead readable (R4) */
    char why_not[40];  /* card text when !predictable */
    WildInput wild;    /* filled when predictable */
    bool on_overworld; /* callback2 == cb2_overworld */
} HuntFacts;

/* Reads everything except what NuzFacts already has; nf supplies valid, otid, in_battle, the
 * party and the enemy fields. predict false skips the prediction inputs (predictable stays
 * false): counting needs none of them. Returns out->valid. */
bool hunt_read(const GameProfile* p, const GameMem* m, const NuzFacts* nf, bool predict,
               HuntFacts* out);

/* GetCurrentMapWildMonHeaderId: the map's wild header index, with Altering Cave's
 * VAR_ALTERING_CAVE_WILD_SET added. sb1 is the SaveBlock1 address. False when the map has none. */
bool hunt_wild_header(const GameProfile* p, const GameMem* m, uint32_t sb1, uint8_t group, uint8_t num,
                      uint16_t* index);

#endif // HUNT_READ_H
