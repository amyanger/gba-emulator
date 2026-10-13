#ifndef HUNT_H
#define HUNT_H

#include "game/hunt_read.h"
#include "game/wildgen.h"
#include <stdbool.h>
#include <stdint.h>

/* Shiny Hunt tracker and Next match search. Pure: per-frame HuntFacts in, events out. The
 * session (frontend) appends each event to the hunt file and replays it through hunt_apply.
 * Rulings R6-R10, R14: docs/superpowers/specs/2026-10-13-shiny-hunt-design.md. */

#define HUNT_TALLY_MAX 16
#define HUNT_NEXT_ROWS 6
#define HUNT_SCAN_LIMIT (1u << 20) /* advances searched ahead of now */
#define HUNT_NATURE_ANY 25
#define HUNT_SEEN_RING 16 /* R8 dedupe ring of recent enemy PIDs */

typedef enum { HUNT_EV_NONE, HUNT_EV_ENCOUNTER } HuntEventKind;
typedef enum { HUNT_METHOD_WILD, HUNT_METHOD_SAFARI, HUNT_METHOD_STATIC } HuntMethod;

typedef struct {
    uint8_t kind; /* HuntEventKind */
    uint16_t species;
    uint32_t pid;
    bool shiny;
    uint8_t method; /* HuntMethod */
} HuntEvent;

typedef struct {
    /* persisted (replayed from the file) */
    uint32_t total, shiny_total;
    uint16_t tally_species[HUNT_TALLY_MAX]; /* most seen first; a 17th species is not tallied */
    uint32_t tally_count[HUNT_TALLY_MAX];
    uint8_t tally_n;
    uint16_t target; /* species id, 0 = any (R14) */
    uint8_t nature;  /* 0-24, HUNT_NATURE_ANY = any */
    /* session only */
    uint32_t session;
    uint32_t seen_pids[HUNT_SEEN_RING]; /* refilled by replay */
    uint8_t seen_head;
    bool was_in_battle, counted_this_battle;
    uint32_t stable_pid;
    uint8_t stable_frames;
    /* prediction cache (R6, R10) */
    uint32_t anchor_count, anchor_rng; /* rand_count/rng at scan start */
    uint32_t scanned;                  /* advances scanned from the anchor */
    bool found;
    uint32_t match_at; /* absolute rand_count of the match */
    WildPreview match;
    uint32_t key;     /* hash of map, wild inputs, filter, otid: change => rescan */
    uint32_t version; /* bumps when anything shown changes */
    /* private */
    uint8_t seen_n;
    uint8_t armed_method; /* HuntMethod + 1 while the current battle may still count, else 0 */
    bool resync;          /* next valid frame re-baselines was_in_battle (discontinuity) */
    bool anchored;
    uint32_t cursor_rng; /* state at anchor + scanned */
} HuntState;

void hunt_init(HuntState* h);          /* zero, nature filter "any", resync on the first frame */
void hunt_reset_counts(HuntState* h);  /* does not clear target/nature or the dedupe ring */
void hunt_discontinuity(HuntState* h); /* savestate load / rewind release (R8) */
/* One emulated frame. rewinding: no edge detection. Returns true and fills *ev when an
 * encounter is counted; hunt_frame has already applied it (caller appends to the file, toasts
 * if shiny, and must not call hunt_apply again). */
bool hunt_frame(HuntState* h, const HuntFacts* f, bool rewinding, HuntEvent* ev);
void hunt_apply(HuntState* h, const HuntEvent* ev); /* counts an event (replay) */
/* Spend up to budget candidate advances on the Next match search (render only, R10). */
void hunt_scan(HuntState* h, const HuntFacts* f, uint32_t budget);
/* Advances from now to the match (UINT32_MAX when none found yet / not found within the limit,
 * or the cache is not for these facts). */
uint32_t hunt_match_in(const HuntState* h, const HuntFacts* f);
/* Rows for now..+5: what a step completing on each upcoming advance gives (WG_NONE rows when
 * the facts are not predictable). */
void hunt_next_rows(const HuntFacts* f, WildPreview out[HUNT_NEXT_ROWS]);
/* Battle, certain, shiny, target, nature. */
bool hunt_matches(const HuntState* h, const WildPreview* p);
double hunt_chance(uint32_t n); /* 1 - (8191/8192)^n */

#endif // HUNT_H
