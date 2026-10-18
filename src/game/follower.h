#ifndef FOLLOWER_H
#define FOLLOWER_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_profile.h"
#include "game/game_mem.h"
#include "game/overworld.h"
#include "game/pokemon.h"

typedef struct GBA GBA;

/* Raw per-frame Emerald state at line 160. Filled by follower_read. */
typedef struct {
    bool ok;
    bool overworld;            /* callback2 == CB2_Overworld */
    bool free;                 /* overworld, lock 0, no fade (y==0 and not active), no transition */
    uint8_t avatar_flags, tile_state;
    int16_t cur_x, cur_y, prev_x, prev_y;   /* player VMap coords */
    uint8_t facing;            /* 1 S, 2 N, 3 W, 4 E (game values) */
    uint8_t move_action, obj_flags0;
    uint8_t loc_group, loc_num, map_type, mapsec;
    bool cam_active;
    int32_t cam_x, cam_y;
    OwScreenOrigin origin;
    uint16_t new_keys;
    uint8_t lock, script_status;
    uint8_t fol_behavior, fol_elevation;    /* at the follower tile passed to follower_read */
    int16_t fol_x, fol_y;      /* that tile */
    uint8_t player_elevation;  /* player object's currentElevation */
    bool player_invisible;     /* player object hidden, e.g. after walking into a door */
    bool fol_blocked;          /* another visible object stands on the follower tile */
    uint32_t pokecenter_heals; /* GAME_STAT_USED_POKECENTER */
    bool lead_ok;
    uint8_t lead_slot;
    MonData lead;
    uint8_t party_hp_zero;     /* bitmask of present, non-egg party slots with hp 0 */
    bool in_transition;        /* battle transition running (sTransitionData non-zero) */
    uint32_t fade_mask;
    uint8_t fade_y;
    uint16_t fade_color;
    bool fade_active;
    uint8_t weather_state, weather_counter, weather;
    uint16_t weather_color;
} FollowerInput;

typedef enum { FOL_OFF, FOL_HIDDEN, FOL_VISIBLE } FollowerVis;

#define FOL_BALL_NONE 0
#define FOL_BALL_IN 1
#define FOL_BALL_OUT 2
/* 60 Hz frames (HGSS ticks x2). */
#define FOL_BALL_IN_SHRINK 8       /* shrink into the ball, then the ball closes */
#define FOL_BALL_IN_TOTAL 16
#define FOL_BALL_OUT_BALL 4        /* ball frames, then the burst */
#define FOL_BALL_OUT_TOTAL 20

typedef struct {
    FollowerVis vis;
    bool reveal_pending;       /* appear on the player's next step */
    bool in_ball;              /* bike/surf/dive/too tall: hidden until reveal */
    int16_t tx, ty;            /* follower tile (VMap) */
    int16_t from_x, from_y;    /* tile the current step started on */
    uint8_t step_len, step_t;  /* frames per tile, frames done; idle when step_t >= step_len */
    uint8_t facing;            /* game value 1..4 */
    uint8_t elevation;         /* kept like ObjectEventUpdateElevation: tiles at 0 or 15 leave it */
    uint16_t anim_tick;
    int8_t hop_y;              /* extra upward offset during a ledge hop */
    bool jump_pending;
    uint8_t jump_dir;          /* game value, 0 none */
    uint8_t ball_fx;           /* FOL_BALL_*: Poke Ball effect in progress */
    uint8_t ball_t;            /* frames since the effect started */
    bool reveal_from_ball;     /* the pending reveal follows leaving the ball */
    bool recalled;             /* in the ball for a heal or an NPC on its tile, until the player steps */
    bool talk_request;         /* set for one frame when A is pressed on the follower */
    uint32_t lead_pid;
    uint8_t lead_level;
    bool lead_changed;         /* lead pid changed this frame */
    int16_t mood_delta;        /* accumulated mood events this frame, consumed by the frontend */
    uint32_t faint_pid;        /* set for one frame: personality of a lead that fainted in battle, 0 none */
    uint16_t steps;            /* player steps this frame (mood decay) */
    /* edge detection */
    bool p_valid, p_free, p_cam_active, p_in_battle;
    int16_t p_cur_x, p_cur_y;
    uint8_t p_loc_group, p_loc_num;
    uint8_t p_party_hp_zero;   /* bitmask of slots with hp 0 on the last overworld frame */
    uint8_t p_lead_slot;       /* lead slot on the last overworld frame */
    uint32_t p_heals;          /* pokecenter_heals on the last overworld frame */
} Follower;

void follower_init(Follower* f);
/* Hide on the player's tile and arm reveal; call on savestate load, rewind, toggle. */
void follower_reset(Follower* f);
bool follower_read(const GameProfile* p, const GameMem* m, int16_t fol_x, int16_t fol_y, FollowerInput* out);
/* One frame of simulation. too_tall: lead uses a 64x64 HGSS sprite. */
void follower_step(Follower* f, const FollowerInput* in, bool too_tall);
/* Feet point (bottom-centre of the tile, plus walk interpolation and hop), GBA px. */
void follower_feet(const Follower* f, const FollowerInput* in, int16_t* x, int16_t* y);
/* Sprite frame 0..7 for the current facing and animation tick. */
uint8_t follower_frame(const Follower* f);

/* VBlank observer glue: ctx is a FollowerFeed. */
typedef struct {
    const GameProfile* profile;
    Follower sim;
    FollowerInput in;          /* last read */
    bool too_tall;             /* set by the frontend when it loads the sprite */
    uint32_t fade_mask;        /* last non-zero gPaletteFade mask and its y, see follower_capture */
    uint8_t fade_y;
} FollowerFeed;
void follower_feed_init(FollowerFeed* fd, const GameProfile* p);
void follower_capture(const GBA* gba, void* feed);

#endif // FOLLOWER_H
