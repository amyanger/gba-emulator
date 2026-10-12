#ifndef RUN_TRACK_H
#define RUN_TRACK_H

#include "game/runlog.h"

#define RUN_EVENTS_FRAME_MAX 24
#define RUN_CONFIRM_FRAMES   6    /* R5: frames a save-block value must hold (play time advancing) */
#define RUN_FAINT_FRAMES     30
#define RUN_RELOAD_SETTLE    60   /* consecutive behind frames before a dip counts as a reload */
#define RUN_RELOAD_SLACK     2    /* play time may run this far ahead of one per frame while behind */
#define RUN_JUMP_FRAMES      120
#define RUN_INVALID_FRAMES   60   /* invalid facts this long are a discontinuity, shorter is frozen */
/* OT ID of Steven's partner mons in the Mossdeep multi battle.
 * pret pokeemerald 731ad5b src/battle_tower.c: #define STEVEN_OTID 61226 */
#define RUN_STEVEN_OTID      61226u

typedef struct { NuzKey key; bool alive_seen; uint8_t zero_frames; } RunHpWatch;
typedef struct { NuzKey key; bool credited; uint32_t pt; uint8_t foe_level, att_level, foe_slot; bool on_ace; } RunKoEntry;
typedef struct { NuzKey key; uint16_t species; uint8_t frames; } RunEvoWatch;

typedef struct {
    bool have_prev;
    uint8_t invalid_frames;       /* consecutive invalid-facts frames; over 60 is a discontinuity */
    bool seeded;                  /* high taken from the log on the first step after reset */
    uint32_t prev_pt, high, prev_tid;
    uint32_t prev_sb1, prev_sb2;
    uint8_t sb_stable;            /* frames the save block pointers have not changed */
    /* R5 confirm: candidate values and how many play-time-advancing frames they held */
    uint8_t cand_badges; uint16_t cand_story; uint8_t cand_frames;
    bool conf_fresh;              /* badges/story confirmed since the last discontinuity */
    uint8_t conf_badges; uint16_t conf_story;
    uint32_t conf_steps;          /* latest steps read with stable save block pointers, 0 after a discontinuity */
    /* R2: while behind, everything is frozen until the dip settles (reload) or cancels (glitch) */
    bool behind; uint32_t low; uint8_t behind_frames;
    uint32_t dip_frames;          /* all frames frozen in this dip, for the jump check on resume */
    bool resync;                  /* in the window after a discontinuity: difference events are untimed / backfill */
    bool boxes_fresh;             /* a full box scan happened since the last discontinuity */
    bool box_rescan;              /* the last box scan ran out of room; continue on the kept list */
    /* edges */
    RunHpWatch hp[6];
    RunEvoWatch evo[6];
    bool prev_whiteout;
    bool was_facility;            /* last frame's R6 gate */
    bool prev_in_battle;          /* last frame's NuzFacts.in_battle */
    /* open battle (R7) */
    bool in_battle; uint32_t battle_pt;
    uint8_t battle_kind; uint16_t battle_trainer, battle_ace; uint8_t battle_outcome;
    uint16_t battle_foe; bool battle_shiny;
    uint8_t battle_faints; uint32_t faint_pts[12];   /* FAINTs in this battle, filtered on reload */
    uint32_t battle_pid, battle_ot;
    uint16_t foe_hp[4]; bool foe_armed[4];
    uint8_t foe_live[4];          /* consecutive in-battle frames the slot read hp > 0 */
    bool foe_ok[4];               /* slot already held this battle's data (stale-data guard) */
    uint8_t fought_mask;          /* party indexes that appeared as player battlers */
    NuzKey fought_key[6];         /* key at that party index when it fought */
    uint16_t fought_max[6];       /* its max HP from gBattleMons */
    uint8_t ko_count; RunKoEntry kos[12];
    bool pending; uint32_t pending_pid, pending_ot; bool pending_newdex;
    bool start_owned;             /* enemy_owned when the foe was first seen */
} RunTracker;

void run_tracker_reset(RunTracker* t);
/* Events for one frame. nf: shared NuzFacts (valid, same trainer as the log), rf: this
 * frame's RunFacts. The caller applies each returned event to log in order and appends it
 * to the file. Call after replaying the log: the first step takes the log's latest play
 * time as the reload reference, like nuz_tracker_reset does. */
uint8_t run_track_step(RunTracker* t, const RunLog* log, const NuzFacts* nf, const RunFacts* rf,
                       RunEvent out[RUN_EVENTS_FRAME_MAX]);

#endif // RUN_TRACK_H
