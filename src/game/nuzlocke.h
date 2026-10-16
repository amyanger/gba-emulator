#ifndef NUZLOCKE_H
#define NUZLOCKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/nuzlocke_read.h"

/* Nuzlocke run tracking. Pure logic: the tracker turns per-frame facts into
 * events, nuz_apply folds events into the run, and the session (frontend)
 * appends each event to the run's log file. Replaying the log rebuilds the run,
 * so nothing that happened can be undone by loading an older state. */

typedef enum {
    NUZ_EV_START, NUZ_EV_OWNED, NUZ_EV_ENC, NUZ_EV_OUT, NUZ_EV_CAUGHT, NUZ_EV_GIFT,
    NUZ_EV_DEATH, NUZ_EV_DEADUSED, NUZ_EV_WIPE, NUZ_EV_RELOAD, NUZ_EV_COUNT
} NuzEventType;

/* How a wild encounter counts. */
typedef enum { NUZ_KIND_FIRST, NUZ_KIND_DUPE, NUZ_KIND_SHINY, NUZ_KIND_EXTRA, NUZ_KIND_REROLL,
               NUZ_KIND_COUNT } NuzKind;

typedef struct {
    uint8_t type;        /* NuzEventType */
    uint32_t pt;         /* play time, frames */
    NuzKey key;
    uint16_t species;
    uint8_t level;
    uint8_t sec;         /* mapsec, NUZ_SEC_NONE when unknown */
    uint8_t kind;        /* enc, caught: NuzKind */
    bool shiny;          /* enc */
    bool caught;         /* out: caught (true) or missed */
    uint16_t foe;        /* death: enemy species, 0 outside battle */
    uint32_t tid;        /* start */
    uint32_t from, to;   /* reload */
    uint8_t nick[10];    /* caught, gift, owned */
} NuzEvent;

#define NUZ_ROUTES      256
#define NUZ_MEMBERS_MAX 512

typedef enum { NUZ_RES_NONE, NUZ_RES_PENDING, NUZ_RES_CAUGHT, NUZ_RES_MISSED, NUZ_RES_OWNED } NuzResult;
typedef enum { NUZ_JOIN_OWNED, NUZ_JOIN_CAUGHT, NUZ_JOIN_GIFT } NuzJoin;

typedef struct {
    bool used;
    uint8_t result;       /* NuzResult */
    uint16_t species;     /* the encounter that used the route */
    uint32_t pt;          /* when it was used */
    int16_t member;       /* run member it produced, -1 none */
    int16_t gift;         /* first gift met here, -1 none */
    bool rerolled;        /* encountered again after a reload */
} NuzRoute;

typedef struct {
    NuzKey key;
    uint16_t species;     /* species when it joined */
    uint8_t sec;
    uint8_t join;         /* NuzJoin */
    uint8_t nick[10];
    bool reroll;          /* caught from a re-rolled encounter */
    bool dead;
    bool used_dead;
    bool reloaded_after;  /* a reload went back past this death */
    uint32_t death_pt;
    uint8_t death_sec, death_level;
    uint16_t foe;
} NuzMember;

typedef struct {
    bool active;
    uint32_t tid;
    uint32_t start_pt, max_pt;
    NuzRoute routes[NUZ_ROUTES];
    uint16_t member_count;
    NuzMember members[NUZ_MEMBERS_MAX];
    uint8_t last_enc_sec;     /* mapsec of the newest enc, NUZ_SEC_NONE before any */
    uint8_t last_enc_kind;
    uint16_t deaths, reloads, deadused, rerolls;
    bool wiped, reloaded_after_death;
    uint32_t version;         /* bumped by every applied event */
} NuzRun;

void nuz_run_init(NuzRun* run);
/* The only function that changes a run. Events for another trainer are the
 * session's problem; apply trusts what it is given. */
void nuz_apply(NuzRun* run, const NuzEvent* ev);
int nuz_find_member(const NuzRun* run, NuzKey key);

/* One log line, without the newline. Returns the length, or -1 if it didn't fit. */
int nuz_format(const NuzEvent* ev, char* out, size_t out_size);
/* Parses a line written by nuz_format; false for anything malformed or unknown. */
bool nuz_parse(const char* line, NuzEvent* ev);
const char* nuz_kind_name(uint8_t kind);

/* Transient detection state (debounce, pending catch, reload in progress). */
typedef struct {
    NuzKey key;
    bool alive_seen;
    uint8_t zero_frames;
} NuzHpWatch;

typedef struct {
    bool have_prev;
    bool prev_in_battle;
    bool battle_ignored;      /* NuzFacts.ignore_battle at the start, or no foe */
    uint8_t outcome;          /* last non-zero gBattleOutcome this battle */
    uint8_t enc_sec, enc_kind;
    uint32_t enc_pid;
    bool deadused_logged;
    bool pending;             /* caught, waiting for it to show up in the party or PC */
    uint32_t pending_pid;
    uint8_t pending_sec, pending_kind;
    NuzHpWatch hp[NUZ_PARTY_MAX];
    uint32_t high;            /* highest play time seen */
    bool behind;              /* play time went back; reload not logged yet */
    uint32_t low;
    uint8_t behind_frames;
    NuzPartyBase party_base;  /* R16 Teachy TV gate */
} NuzTracker;

#define NUZ_DEATH_FRAMES  30
#define NUZ_RELOAD_SETTLE 60
#define NUZ_GLITCH_FRAMES 2   /* a dip this short that comes back is a misread, not a reload */
#define NUZ_EVENTS_MAX    16

void nuz_tracker_reset(NuzTracker* t, const NuzRun* run);
/* Events for one frame of facts (valid, same trainer as the run). family maps
 * species to their evolution line; sec_count is the number of real mapsecs. */
uint8_t nuz_track_step(NuzTracker* t, const NuzRun* run, const NuzFacts* f, const uint16_t* family,
                       uint8_t sec_count, NuzEvent out[NUZ_EVENTS_MAX]);
/* The start event plus one owned event per Pokemon already in the party or PC
 * (f must have boxes). Returns how many were written to out. */
uint16_t nuz_backfill(const NuzFacts* f, uint8_t sec_count, NuzEvent* out, uint16_t max);

#endif // NUZLOCKE_H
