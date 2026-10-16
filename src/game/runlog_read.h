#ifndef RUNLOG_READ_H
#define RUNLOG_READ_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/game_profile.h"

#define RUN_STATS        52   /* NUM_USED_GAME_STATS */
#define RUN_BADGES       8
#define RUN_TRAINER_ACE_NONE 0

/* GAME_STAT_* indexes used by the page (include/constants/game_stat.h). */
enum {
    RUN_STAT_FIRST_HOF_TIME = 1, RUN_STAT_STEPS = 5, RUN_STAT_TOTAL_BATTLES = 7, RUN_STAT_WILD_BATTLES = 8,
    RUN_STAT_TRAINER_BATTLES = 9, RUN_STAT_ENTERED_HOF = 10, RUN_STAT_CAPTURES = 11, RUN_STAT_FISHING = 12,
    RUN_STAT_HATCHED = 13, RUN_STAT_EVOLVED = 14, RUN_STAT_POKECENTER = 15, RUN_STAT_SPLASH = 26,
    RUN_STAT_STRUGGLE = 27, RUN_STAT_SLOT_JACKPOTS = 28, RUN_STAT_SHOPPED = 38, RUN_STAT_RAINED_ON = 40,
    RUN_STAT_CHECKED_DEX = 41, RUN_STAT_LEDGES = 43, RUN_STAT_WATCHED_TV = 44, RUN_STAT_CHECKED_CLOCK = 45
};

/* Emerald story indices. Every game's GameRules.story table is indexed the same way;
 * RUN_STORY_COUNT is the capacity. */
typedef enum {
    RUN_STORY_STARTER, RUN_STORY_POKEDEX, RUN_STORY_CHIMNEY, RUN_STORY_WEATHER, RUN_STORY_SURF,
    RUN_STORY_GROUDON, RUN_STORY_KYOGRE, RUN_STORY_SOOTOPOLIS, RUN_STORY_E4, RUN_STORY_CHAMPION,
    RUN_STORY_HOF, RUN_STORY_NATDEX, RUN_STORY_COUNT
} RunStory;

typedef struct {
    uint16_t hp, max_hp, species;
    uint8_t level, pos;          /* pos: gBattlerPositions, side = pos & 1 (0 = player) */
    uint16_t party_index;        /* gBattlerPartyIndexes */
} RunBattler;

typedef struct {
    bool valid;                  /* save block pointers readable */
    uint32_t sb1, sb2;           /* this frame's gSaveBlock1Ptr / gSaveBlock2Ptr */
    uint8_t player_name[8];      /* SB2+0x00, Gen 3 encoded */
    uint8_t badges;              /* bit n-1 = badge n */
    uint16_t story;              /* bit = index in GameRules.story */
    uint8_t starter;             /* VAR_STARTER_MON value */
    uint32_t stats[RUN_STATS];   /* decrypted (stored ^ SB2.encryptionKey) */
    uint16_t dex_owned, dex_seen;
    uint32_t callback2;          /* gMain.callback2 */
    bool whiteout;               /* callback2 == profile CB2_WhiteOut */
    uint8_t map_group, map_num;  /* SB1 location */
    bool facility;               /* R6 gate: frontier/tent/link/partner battle flags or a facility map */
    bool partner_battle;         /* rules->partner_battle_mask: player slots 3+ are the partner's */
    uint32_t partner_otid;       /* rules->partner_otid: mons with this OT are not the run's */
    uint8_t hof_story;           /* rules->story_hof */
    /* battle, valid when battlers > 0 */
    uint8_t battlers;            /* gBattlersCount, 0 outside battle */
    RunBattler b[4];
    uint8_t attacker;            /* gBattlerAttacker */
    uint16_t opponent;           /* gTrainerBattleOpponent_A, 0 unless a plain trainer battle (R8) */
    bool opponent_leader;        /* that trainer is a gym leader, Elite Four member or Champion */
    uint16_t ace_species;        /* species of that trainer's last party slot from gTrainers, 0 none */
    bool enemy_owned;            /* dex owned bit of NuzFacts.enemy_species at read time */
} RunFacts;

/* Fills f; f->valid false while save blocks are unreadable. enemy_species is
 * NuzFacts.enemy_species (0 outside battle). */
bool run_read(const GameProfile* p, const GameMem* m, uint16_t enemy_species, RunFacts* f);
/* Is trainer id one of the family's gym leaders, Elite Four or Champions? */
bool run_is_leader(const GameRules* r, uint16_t trainer);

#endif // RUNLOG_READ_H
