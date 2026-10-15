#ifndef RUN_SESSION_H
#define RUN_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "game/game.h"
#include "game/run_track.h"
#include "frontend/session_log.h"

/* Owns the run log of a normal playthrough: <rom>.<trainer id>.run next to the ROM, or a
 * fixed path in headless runs. Replays it when a save with that trainer ID shows up,
 * appends every new event, and keeps a display-ready RunView for the panel. No SDL.
 * View strings are ready to draw (names already title-cased). */

typedef enum { RUN_VIEW_OFF, RUN_VIEW_WAITING, RUN_VIEW_RUN } RunViewState;
typedef enum { RUN_ICON_BADGE, RUN_ICON_STORY, RUN_ICON_CATCH, RUN_ICON_GIFT, RUN_ICON_FAINT,
               RUN_ICON_WHITEOUT, RUN_ICON_EVOLVE, RUN_ICON_ACE } RunFeedIcon;

#define RUN_VIEW_FEED 64
#define RUN_VIEW_TEAM 48

typedef struct {
    char time[12];               /* "6:41", or "·" when untimed */
    uint8_t icon;                /* RunFeedIcon */
    char text[48];               /* "Balance Badge, beat Norman" */
    char where[GAME_MAP_NAME_LEN];
} RunViewFeed;

typedef struct {
    char name[GAME_NAME_LEN], species[GAME_NAME_LEN];
    char joined[12];             /* play time or "·" */
    char where[GAME_MAP_NAME_LEN];
    uint8_t lv_from, lv_to;
    uint16_t kos, faints;
    char evolved[32];            /* "Combusken 1:44", empty none */
    bool mvp, in_party, away;
} RunViewMon;

typedef struct {
    uint8_t state;               /* RunViewState */
    char reason[48];
    char player[GAME_NAME_LEN];
    char play_time[12];          /* "9:12" (h:mm) */
    uint8_t badge_count;
    uint16_t dex_owned, dex_seen;
    struct { bool have, untimed; char split[12]; char name[12]; } badges[RUN_BADGES];
    uint8_t feed_count;          /* newest first */
    RunViewFeed feed[RUN_VIEW_FEED];
    uint32_t caught, faints, steps, battles;
    bool faints_since_tracking;
    char mvp[64], closest[64], fun[64];   /* empty: card hidden */
    char tracking_since[40];
    uint8_t team_count;          /* party first, then PC, then away */
    uint16_t team_total;         /* every member that would be listed; > team_count: "+N more" */
    RunViewMon team[RUN_VIEW_TEAM];
    uint32_t wild, trainer_battles, whiteouts, upsets, shiny_seen, shiny_caught, eggs, evolutions,
             centers, shops, fishing, ledges, rained, clocks, splash;
    char most_enc[40], caught_in[48];
    bool write_failed, log_full;
} RunView;

typedef struct {
    const GameProfile* profile;
    char rom_path[1024];
    char fixed_path[1024];       /* headless --run-file */
    RunFacts rf;
    RunLog log;
    RunTracker tracker;
    bool attached;
    uint32_t tid;
    SessionTidGate tid_gate;     /* a new trainer ID must hold for a while before we switch */
    char path[1100];
    bool path_ok, file_has_header, write_failed;
    uint32_t frame;
    RunView view;
    uint32_t view_version, view_log_version;
} RunSession;

/* fixed_path may be NULL. A NULL profile keeps the session off. */
void run_session_init(RunSession* s, const GameProfile* p, const char* rom_path, const char* fixed_path);
/* Once per emulated frame, after nuz_session_frame filled nf. */
void run_session_frame(RunSession* s, const GameMem* m, const NuzFacts* nf);
bool run_session_dump(const RunSession* s, FILE* out);

#endif // RUN_SESSION_H
