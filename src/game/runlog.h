#ifndef RUNLOG_H
#define RUNLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/nuzlocke_read.h"   /* NuzKey, NUZ_SPECIES_MAX, NUZ_SEC_NONE */
#include "game/runlog_read.h"     /* RUN_STORY_COUNT */

typedef enum {
    RUN_EV_START, RUN_EV_JOIN, RUN_EV_EVOLVE, RUN_EV_FAINT, RUN_EV_WHITEOUT, RUN_EV_BADGE,
    RUN_EV_STORY, RUN_EV_BATTLE, RUN_EV_RELOAD, RUN_EV_COUNT
} RunEventType;
typedef enum { RUN_HOW_CAUGHT, RUN_HOW_GIFT, RUN_HOW_HATCHED, RUN_HOW_TRADED, RUN_HOW_BACKFILL, RUN_HOW_COUNT } RunHow;
typedef enum { RUN_BATTLE_WILD, RUN_BATTLE_TRAINER, RUN_BATTLE_OTHER } RunBattleKind;

#define RUN_KO_SLOTS 6
#define RUN_LINE_MAX 512         /* longest formatted line (a full BATTLE) is ~320 chars */

typedef struct {
    NuzKey key;
    uint16_t count;
    uint8_t level;               /* attacker level at its last KO this battle */
} RunKo;

typedef struct {
    uint8_t type;                /* RunEventType */
    uint32_t pt;                 /* play time, game frames (60/s) */
    bool untimed;                /* JOIN/BADGE/STORY found on a resync, not seen happening */
    uint32_t seq;                /* set by run_apply: file/log order */
    /* JOIN, EVOLVE, FAINT */
    NuzKey key;
    uint16_t species;            /* JOIN/FAINT: species; EVOLVE: new species */
    uint16_t from;               /* EVOLVE: old species */
    uint8_t level;
    uint8_t sec;                 /* JOIN: met mapsec; FAINT/WHITEOUT: current mapsec */
    uint8_t how, ball;           /* JOIN */
    bool shiny, newdex;          /* JOIN; BATTLE uses shiny for a shiny wild foe */
    uint8_t nick[10];            /* JOIN */
    uint16_t foe;                /* FAINT: enemy species, 0 outside battle; BATTLE: lead foe species */
    /* BADGE (id 1-8) / STORY (id RunStory) */
    uint8_t id;
    uint8_t team_max, team_avg;  /* BADGE snapshot */
    uint16_t dex;
    uint32_t steps;
    /* BATTLE */
    uint8_t kind;                /* RunBattleKind */
    uint16_t trainer;            /* 0 unless kind == TRAINER */
    uint8_t outcome;             /* gBattleOutcome */
    uint8_t ko_count;
    RunKo kos[RUN_KO_SLOTS];
    uint8_t indirect;            /* foe faints not credited to a run member */
    uint8_t upset;               /* largest foe-minus-attacker level gap of a credited KO, 0 none */
    uint16_t low;                /* won battles: lowest end HP permille of a mon that fought and stayed up; 1000 none */
    NuzKey low_by;               /* the mon whose end HP gave low */
    uint8_t faints;              /* player mons that fainted this battle */
    bool has_ace;
    NuzKey ace_by;               /* credited KO on the trainer's last party slot */
    uint16_t ace_species;
    /* START / RELOAD */
    uint32_t tid, to_pt;         /* RELOAD: from = pt, to = to_pt */
} RunEvent;

#define RUN_EVENTS_MAX  2048     /* non-BATTLE live events */
#define RUN_BATTLES_MAX 4096     /* BATTLE live events */
#define RUN_MEMBERS_MAX 512
#define RUN_ORPHANS     (RUN_BADGES + RUN_STORY_COUNT)

typedef struct {
    NuzKey key;
    uint16_t species, join_species;
    uint8_t join_level, level;   /* level: latest known (JOIN, EVOLVE, KO entries) */
    uint8_t sec, how, ball;
    bool shiny, live;            /* live: joined while tracked (not backfill/untimed) */
    uint32_t join_pt;
    uint16_t kos, faints;
    uint16_t evo_species;        /* last evolution target, 0 none */
    uint32_t evo_pt;
    uint8_t nick[10];
} RunMember;

typedef struct {
    bool active;
    uint32_t tid, start_pt;
    bool started_new_game;       /* START pt < 60*60*60 (first hour) and no backfill members */
    uint32_t next_seq;
    uint16_t ev_count;
    RunEvent ev[RUN_EVENTS_MAX];
    uint16_t bt_count;
    RunEvent bt[RUN_BATTLES_MAX];
    /* R4 */
    bool orphan[RUN_ORPHANS];
    uint32_t orphan_pt[RUN_ORPHANS];   /* index: badge n-1, then RUN_BADGES + story */
    /* R9 folded wild battles (immune to truncation) */
    uint32_t folded_wild;
    uint16_t folded_ko_count;
    RunKo folded_kos[RUN_MEMBERS_MAX];
    uint16_t folded_low;         /* best closest call among folded battles, 1000 none */
    uint16_t folded_low_foe;
    NuzKey folded_low_by;
    uint32_t folded_low_seq;     /* seq of that battle, so a rebuild breaks ties like live apply */
    /* aggregates, rebuilt from the lists */
    uint16_t member_count;
    RunMember members[RUN_MEMBERS_MAX];
    bool badge[RUN_BADGES], badge_untimed[RUN_BADGES];
    uint32_t badge_pt[RUN_BADGES];
    bool story[RUN_STORY_COUNT], story_untimed[RUN_STORY_COUNT];
    uint32_t story_pt[RUN_STORY_COUNT];
    uint16_t faints, whiteouts, upsets, shinies_seen, shinies_caught, indirect;
    uint16_t low_permille;       /* best closest call, 1000 none */
    NuzKey low_key;
    uint16_t low_foe;
    uint16_t wild_count[NUZ_SPECIES_MAX];   /* includes folded */
    bool events_full, battles_full;         /* derived from counts */
    uint32_t version;                       /* bumped by every applied event */
} RunLog;

void run_log_init(RunLog* log);
/* The only mutator. RELOAD drops live events with pt > to_pt except START, how=BACKFILL
 * JOINs and untimed events, records orphan times for dropped BADGE/STORY, then rebuilds. */
void run_apply(RunLog* log, const RunEvent* ev);
int run_find_member(const RunLog* log, NuzKey key);
/* One line without newline; -1 if it did not fit. */
int run_format(const RunEvent* ev, char* out, size_t out_size);
bool run_parse(const char* line, RunEvent* ev);

#endif // RUNLOG_H
