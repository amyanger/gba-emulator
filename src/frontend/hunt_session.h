#ifndef HUNT_SESSION_H
#define HUNT_SESSION_H

#include "game/game.h"
#include "game/hunt.h"
#include "game/hunt_read.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Owns a Shiny Hunt's file: <rom>.<trainer id>.hunt next to the ROM, or a fixed path in headless
 * runs. Replays it when a save with that trainer ID shows up, appends every event (queued and
 * retried when a write fails, never latched off), and keeps a display-ready HuntView for the
 * panel. No SDL. File format (text, one line per event):
 *   huntlog 1 / E <species> <pid hex8> <shiny> <method> / R / T <species> / N <nature> */

#define HUNT_QUEUE_MAX 64
#define HUNT_LINE_MAX 48

typedef struct {
    bool on;
    char reason[48]; /* when !on */
    char map[GAME_MAP_NAME_LEN];
    char method[16];            /* "Grass" when the map is predictable, else empty */
    uint32_t advance;           /* live RNG advance number (sRandCount) */
    char target[GAME_NAME_LEN]; /* "Any" or the species */
    uint8_t target_percent;     /* the target's share of this map's land slots, 0 when absent */
    uint8_t target_types[2];    /* type ids from the slot table, 0xFF when unknown */
    bool any_target;
    uint32_t total, session;
    double chance;         /* hunt_chance of the target's count, or total for Any (R14) */
    uint32_t target_count; /* encounters of the target species (0 when Any) */
    char nature_filter[GAME_NAME_LEN]; /* "Any" or the nature */
    bool predict;                      /* Next frames rows and Next match are meaningful */
    char why_not[40];
    bool match_found;
    uint32_t match_in;
    float match_seconds;
    char match_nature[GAME_NAME_LEN];
    int8_t match_up, match_down; /* pokemon_nature_effect indexes */
    uint8_t match_ivs[6];
    bool scanning; /* search still running (no match yet, limit not reached) */
    uint8_t tally_n;
    char tally_name[HUNT_TALLY_MAX][GAME_NAME_LEN];
    uint32_t tally_count[HUNT_TALLY_MAX];
    uint16_t tally_species[HUNT_TALLY_MAX];
    WildPreview rows[HUNT_NEXT_ROWS];
    char row_name[HUNT_NEXT_ROWS][GAME_NAME_LEN];
    char row_nature[HUNT_NEXT_ROWS][GAME_NAME_LEN];
    bool reset_armed;
    bool write_failed; /* lines are waiting to be written */
    bool file_foreign; /* the hunt file isn't ours (or the path is too long): nothing is saved */
    uint32_t version;  /* bumped when visible content changes */
} HuntView;

struct HuntSession {
    const GameProfile* profile;
    char rom_path[1024];
    char fixed_path[1024];
    HuntState hunt;
    HuntFacts facts;
    /* Land slots of the last map that was predictable. hunt_read clears the slots when a step
     * gate fails (off grass, immunity steps, battle), but the map's table doesn't change. */
    bool slots_valid;
    uint8_t slots_group, slots_num;
    WildSlot slots[WILD_SLOTS];
    uint8_t mapsec;
    /* Shown prediction state, debounced so a one-frame misread (party mid-write, save blocks
     * mid-move) never reaches the view. pred_facts is the last frame that could predict. */
    bool gate_known, gate_predict;
    char gate_why[40];
    bool cand_predict;
    char cand_why[40];
    uint8_t cand_frames;
    HuntFacts pred_facts;
    bool shown; /* the Hunt page is on screen: only then are predictions read */
    bool attached;
    uint32_t tid;
    uint32_t pending_tid; /* a new trainer ID must hold for a while before we switch */
    uint8_t pending_count;
    char path[1100];
    bool path_ok; /* false: path too long or the file isn't ours */
    bool file_has_header;
    char queue[HUNT_QUEUE_MAX][HUNT_LINE_MAX];
    uint8_t queue_n;
    bool write_warned;
    uint32_t frame;
    bool reset_armed;
    uint32_t reset_armed_ms;
    bool built, dirty;
    uint32_t built_ms;
    float rate; /* advances per second, EMA */
    bool rate_valid;
    uint32_t rate_ms, rate_count;
    HuntView view;
};
typedef struct HuntSession HuntSession;

/* fixed_path may be NULL. A NULL profile keeps the session off. */
void hunt_session_init(HuntSession* s, const GameProfile* p, const char* rom_path,
                       const char* fixed_path);
/* Per emulated frame (from nuz_tick). Returns true and fills the toast (each buffer 48 bytes)
 * when a shiny was counted. */
bool hunt_session_frame(HuntSession* s, const GameMem* m, const NuzFacts* nf, bool rewinding,
                        char* toast_title, char* toast_detail);
void hunt_session_discontinuity(HuntSession* s);
/* Whether the Hunt page is on screen. Hidden, the session only counts; shown again, the next
 * frame's prediction state shows at once. */
void hunt_session_set_shown(HuntSession* s, bool shown);
/* Render-time work while the page is shown: scan with budget and rebuild the view at most 4x/s. */
void hunt_session_present(HuntSession* s, const GameMem* m, uint32_t now_ms, uint32_t budget);
void hunt_session_target(HuntSession* s, int delta); /* cycles Any + this map's land species */
void hunt_session_nature(HuntSession* s, int delta); /* cycles Any + the 25 natures */
/* R pressed: the first press arms (3 s), the second resets. True when the reset happened. */
bool hunt_session_reset_key(HuntSession* s, uint32_t now_ms);
const HuntView* hunt_session_view(const HuntSession* s);
bool hunt_session_dump(const HuntSession* s, FILE* out); /* --game-dump lines */

#endif // HUNT_SESSION_H
