#ifndef NUZLOCKE_SESSION_H
#define NUZLOCKE_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "game/game.h"
#include "game/nuzlocke.h"
#include "frontend/session_log.h"

/* Owns a Nuzlocke run's file: <rom>.<trainer id>.nuzlocke next to the ROM, or a
 * fixed path in headless runs. Loads and replays it when a save with that
 * trainer ID shows up, appends every new event, and keeps a display-ready
 * NuzView for the panel. No SDL. */

typedef enum { NUZ_VIEW_OFF, NUZ_VIEW_NO_RUN, NUZ_VIEW_RUN } NuzViewState;
typedef enum {
    NUZ_PILL_HERE, NUZ_PILL_PENDING, NUZ_PILL_CAUGHT, NUZ_PILL_MISSED, NUZ_PILL_DEAD,
    NUZ_PILL_OWNED, NUZ_PILL_GIFT
} NuzPill;
typedef enum { NUZ_WHERE_PARTY, NUZ_WHERE_PC, NUZ_WHERE_AWAY, NUZ_WHERE_DEAD } NuzWhere;

#define NUZ_VIEW_ROUTES 96
#define NUZ_VIEW_TEAM   36
#define NUZ_VIEW_GRAVES 12

typedef struct {
    char place[GAME_MAP_NAME_LEN];
    char who[GAME_NAME_LEN];   /* empty: no encounter yet */
    uint8_t pill;              /* NuzPill */
    bool here, reroll;
} NuzViewRoute;

typedef struct {
    char nick[GAME_NAME_LEN], species[GAME_NAME_LEN];
    uint8_t level;             /* 0 when unknown */
    uint8_t where;             /* NuzWhere */
} NuzViewMon;

typedef struct {
    char nick[GAME_NAME_LEN], species[GAME_NAME_LEN];
    uint8_t level;
    char caught_at[GAME_MAP_NAME_LEN], died_at[GAME_MAP_NAME_LEN];
    char foe[GAME_NAME_LEN];   /* empty outside battle */
    char time[12];
    bool reloaded_after, used_dead;
} NuzViewGrave;

typedef struct {
    uint8_t state;             /* NuzViewState */
    char reason[48];           /* OFF / NO_RUN explanation */
    uint16_t alive, dead, routes_used, reloads, deadused, rerolls;
    bool wiped, reloaded_after_death, write_failed;
    char reroll_place[GAME_MAP_NAME_LEN];
    char start_time[12];
    uint8_t route_count;
    int16_t here_row;
    NuzViewRoute routes[NUZ_VIEW_ROUTES];
    uint16_t team_total;
    uint8_t team_count;
    NuzViewMon team[NUZ_VIEW_TEAM];
    uint16_t grave_total;
    uint8_t grave_count;
    NuzViewGrave graves[NUZ_VIEW_GRAVES];
} NuzView;

typedef struct {
    const GameProfile* profile;
    char rom_path[1024];
    char fixed_path[1024];       /* headless: track into this file, start a run at once */
    bool families_ok;
    uint16_t family[NUZ_SPECIES_MAX];
    NuzFacts facts;
    NuzRun run;
    NuzTracker tracker;
    bool attached;
    uint32_t tid;
    SessionTidGate tid_gate;     /* a new trainer ID must hold for a while before we switch */
    char path[1100];
    bool path_ok;                /* false: path too long or the file isn't ours; run stays in memory */
    bool file_has_header;
    bool write_failed;
    bool start_requested;
    uint32_t frame;
    NuzView view;
    uint32_t view_version;       /* bumped when view changes */
    uint32_t view_run_version;
    uint8_t view_mapsec;
} NuzSession;

/* fixed_path may be NULL. A NULL profile keeps the session off. */
void nuz_session_init(NuzSession* s, const GameProfile* p, const char* rom_path, const char* fixed_path);
/* Call once per emulated frame. */
void nuz_session_frame(NuzSession* s, const GameMem* m);
/* The start key. False when there is nothing to start (no save yet, or a run exists). */
bool nuz_session_request_start(NuzSession* s);
bool nuz_session_dump(const NuzSession* s, FILE* out);

#endif // NUZLOCKE_SESSION_H
