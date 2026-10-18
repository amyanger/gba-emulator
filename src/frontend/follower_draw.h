#ifndef FOLLOWER_DRAW_H
#define FOLLOWER_DRAW_H

#include "frontend/cry_player.h"
#include "frontend/follower_ambient.h"
#include "frontend/follower_talk_ui.h"
#include "game/follower.h"
#include "hgss/hgss_assets.h"
#include "ppu/ppu.h"

#define FOLLOW_MOOD_SLOTS 16

typedef struct { uint32_t pid; int8_t mood; } FollowMood;

typedef struct {
    bool enabled;
    HgssAssets* assets;            /* NULL or !ok: feature unavailable */
    HgssFollowerGfx gfx;
    uint32_t gfx_pid;              /* pid the sprite was loaded for, 0 none */
    bool shiny;
    uint8_t split_px[2][HGSS_FOLLOW_MAX * HGSS_FOLLOW_MAX];   /* top / bottom halves for long grass */
    uint8_t grass_px[5][16 * 16];
    uint16_t grass_pal[16];
    bool grass_ok;
    uint8_t elev_prio[16];         /* sElevationToPriority from the Emerald ROM */
    FollowMood mood[FOLLOW_MOOD_SLOTS];
    char mood_path[300];
    TalkPlayer talk;
    FollowerAmbient ambient;       /* idle actions and event bubbles, when no A-talk runs */
    CryPlayer cry;
    uint32_t rng;
    /* Private to follower_draw.c */
    uint32_t tried_pid;            /* last pid follower_view_sync tried to load, 0 none */
    bool rom_loaded;               /* elev_prio and grass_px read from the Emerald ROM */
    uint8_t mood_n;                /* used mood entries, most recently changed first */
    bool grass_in;                 /* follower tile was tall grass last frame */
    int16_t grass_tx, grass_ty;    /* that tile */
    uint8_t grass_t;               /* frames since entering it, saturates */
    uint8_t shrink_px[HGSS_FOLLOW_MAX * HGSS_FOLLOW_MAX];   /* Poke Ball shrink frame */
    uint8_t burst_px[4][8 * 7];    /* the 4 burst sparkle crops, cut at init */
} FollowerView;

void follower_view_init(FollowerView* v, HgssAssets* assets, const char* save_path);
/* Lead sprite (re)load when the pid changes. Returns false if the species has no sprite. */
bool follower_view_sync(FollowerView* v, const GameMem* m, const GameProfile* p, FollowerFeed* fd);
/* Fill extra OBJs for the next frame from the follower state and current hardware OAM. */
void follower_view_objs(FollowerView* v, const FollowerFeed* fd, const uint8_t* oam,
                        PpuExtraObj out[PPU_EXTRA_OBJ_MAX]);
/* Palette with Emerald's fade applied. */
void follower_fade_palette(const uint16_t in[16], const FollowerInput* fi, uint16_t out[16]);
/* OAM slot for a sprite whose feet are at feet_y with this priority. */
int16_t follower_oam_slot(const uint8_t* oam, uint8_t priority, int16_t feet_y);
int8_t follower_mood_get(const FollowerView* v, uint32_t pid);
void follower_mood_add(FollowerView* v, uint32_t pid, int16_t delta);   /* clamps to -127..127 */
bool follower_mood_save(const FollowerView* v);
bool follower_mood_load(FollowerView* v);
/* Composite text box and emote onto a copy of the frame. */
void follower_view_overlay(const FollowerView* v, const FollowerFeed* fd, uint16_t* fb);

#endif // FOLLOWER_DRAW_H
