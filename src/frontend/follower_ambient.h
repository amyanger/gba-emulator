#ifndef FOLLOWER_AMBIENT_H
#define FOLLOWER_AMBIENT_H

#include <stdbool.h>
#include <stdint.h>
#include "frontend/follower_talk_ui.h"
#include "game/follower.h"

typedef struct {
    TalkPlayer player;
    uint16_t still;        /* frames still on the free overworld, saturating */
    uint16_t next_idle;    /* still-frame count that triggers the next idle action */
    uint16_t cooldown;     /* frames until another event bubble may start */
    uint8_t pending_emote; /* 1-based, waiting out pending_t (trainer delay) */
    uint8_t pending_t;
    bool slept;            /* Zz shown this still period */
    bool hold;             /* item/NPC/trainer bubble: keeps playing through the game's script lock */
} FollowerAmbient;

typedef struct {
    bool shown;            /* glue's `shown` */
    bool talking;          /* A-talk active */
    int8_t mood;           /* follower_mood_get for the lead */
    uint8_t friendship;
    bool poisoned;         /* lead.status & 0x88 */
    bool low_hp;           /* lead.hp * 4 < lead.max_hp */
    bool fire_type;
} AmbientCtx;

void ambient_reset(FollowerAmbient* a);
/* One frame, after follower_step. rand: follower_rand. Starts, advances or aborts the bubble;
 * a finished action leaves its last facing in s->facing. */
void ambient_frame(FollowerAmbient* a, Follower* s, const FollowerInput* in, const AmbientCtx* c,
                   uint32_t (*rand)(void*), void* ud);
bool ambient_active(const FollowerAmbient* a);

#endif // FOLLOWER_AMBIENT_H
