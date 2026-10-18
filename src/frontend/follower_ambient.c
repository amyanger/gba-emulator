#include "frontend/follower_ambient.h"
#include <string.h>

/* Emotes, 1-based (sheet index + 1). */
enum {
    EM_HEART = 1, EM_DELIGHTED = 2, EM_SAD = 4, EM_HAPPY = 5, EM_GRUMPY = 6, EM_NOTE = 7,
    EM_QUESTION = 8, EM_EXCLAIM = 9, EM_SWEAT = 10, EM_FLUSTERED = 11, EM_POISON = 12,
    EM_DOTS = 13, EM_ZZ = 14
};

#define TRAINER_DELAY 18     /* gNoOfApproachingTrainers leads the game's own ! by this much */
#define EVENT_COOLDOWN 180
#define ZZ_STILL 2700
#define FACE_PLAYER 0xFF     /* table marker, resolved at start */

/* Our own choreography (R11). facing: game 1 S, 2 N, 3 W, 4 E; each frame lasts 2 * dur. */
static const HgssAnimFrame anim_look[] = {
    {3, 8, 0, 0, 0, false}, {4, 8, 0, 0, 0, false}, {2, 6, 0, 0, 0, false}, {FACE_PLAYER, 4, 0, 0, 0, false},
};
static const HgssAnimFrame anim_face[] = {{FACE_PLAYER, 20, 0, 0, 0, false}};
static const HgssAnimFrame anim_hop[] = {
    {0, 2, 0, 4, 0, false}, {0, 2, 0, -4, 0, false}, {0, 2, 0, 4, 0, false}, {0, 2, 0, -4, 0, false},
};
static const HgssAnimFrame anim_spin[] = {
    {1, 3, 0, 0, 0, false}, {3, 3, 0, 0, 0, false}, {2, 3, 0, 0, 0, false}, {4, 3, 0, 0, 0, false},
    {FACE_PLAYER, 3, 0, 0, 0, false},
};

typedef struct { const HgssAnimFrame* f; uint8_t n; } AnimRef;
static const AnimRef idle_anims[] = {
    {anim_look, 4}, {anim_face, 1}, {anim_hop, 4}, {anim_spin, 5},
};

static uint8_t face_player(const Follower* s, const FollowerInput* in) {
    int32_t dx = in->cur_x - s->tx, dy = in->cur_y - s->ty;
    if (dx == 0 && dy == 0) return s->facing;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) return dx > 0 ? 4 : 3;
    return dy > 0 ? 1 : 2;
}

static void play(FollowerAmbient* a, const Follower* s, const FollowerInput* in, const AnimRef* anim,
                 uint8_t emote, bool hold) {
    HgssTalkScript sc;
    memset(&sc, 0, sizeof(sc));
    sc.step_count = 1;
    sc.step[0].emote = emote;
    talk_start(&a->player, &sc);
    TalkStepData d;
    memset(&d, 0, sizeof(d));
    if (anim) {
        for (uint8_t i = 0; i < anim->n; i++) {
            d.anim[i] = anim->f[i];
            if (d.anim[i].facing == FACE_PLAYER) d.anim[i].facing = face_player(s, in);
        }
        d.anim_n = anim->n;
    }
    talk_set_step_data(&a->player, &d);
    a->hold = hold;
}

static void stop(FollowerAmbient* a) {
    talk_abort(&a->player);
    a->player.phase = TALK_IDLE;
    a->hold = false;
}

static uint8_t weather_emote(uint8_t w, bool fire) {
    switch (w) {
    case 3: case 5: case 13: return fire ? EM_SWEAT : EM_NOTE;  /* rain, thunderstorm, downpour */
    case 2: case 12: return EM_HAPPY;                            /* sunny, drought */
    case 8: return EM_FLUSTERED;                                 /* sandstorm */
    case 6: case 9: return EM_QUESTION;                          /* fog */
    case 7: return EM_DOTS;                                      /* ash */
    default: return 0;
    }
}

static uint8_t idle_emote(const AmbientCtx* c, uint32_t r) {
    if (r % 100 >= 40) return 0;
    if (c->poisoned) return EM_POISON;
    if (c->low_hp) return EM_SWEAT;
    r /= 100;
    if (c->mood >= 60) {
        static const uint8_t hi[] = {EM_HEART, EM_NOTE, EM_HAPPY, EM_DELIGHTED};
        return hi[r % 4];
    }
    if (c->mood <= -40) {
        static const uint8_t lo[] = {EM_SAD, EM_GRUMPY, EM_DOTS};
        return lo[r % 3];
    }
    return (c->friendship >= 150 || (r & 1)) ? EM_NOTE : 0;
}

void ambient_reset(FollowerAmbient* a) {
    memset(a, 0, sizeof(*a));
}

bool ambient_active(const FollowerAmbient* a) {
    return talk_active(&a->player);
}

static void on_event(FollowerAmbient* a, const Follower* s, const FollowerInput* in, const AmbientCtx* c,
                     uint32_t (*rand)(void*), void* ud) {
    static const AnimRef item_hop = {anim_hop, 4};
    uint8_t emote = 0;
    const AnimRef* anim = NULL;
    switch (s->event) {
    case FOL_EV_TRAINER:
        a->pending_emote = EM_EXCLAIM;
        a->pending_t = TRAINER_DELAY;
        a->cooldown = EVENT_COOLDOWN;
        return;
    case FOL_EV_NPC_TALK:
        if (s->event_arg == OBJ_EVENT_GFX_ITEM_BALL || s->event_arg == OBJ_EVENT_GFX_BERRY_TREE) return;
        if (c->mood >= 40 && rand(ud) % 100 < 30) emote = EM_NOTE;
        else emote = c->mood < 0 ? EM_DOTS : EM_QUESTION;
        break;
    case FOL_EV_ITEM:
        emote = c->mood >= 60 ? EM_HEART : EM_NOTE;
        anim = &item_hop;
        break;
    case FOL_EV_WEATHER:
        emote = weather_emote(s->event_arg, c->fire_type);
        break;
    default:
        return;
    }
    if (!emote) return;
    play(a, s, in, anim, emote, s->event != FOL_EV_WEATHER);
    a->cooldown = EVENT_COOLDOWN;
}

void ambient_frame(FollowerAmbient* a, Follower* s, const FollowerInput* in, const AmbientCtx* c,
                   uint32_t (*rand)(void*), void* ud) {
    bool gone = !c->shown || c->talking;
    if (ambient_active(a) && (gone || s->steps || (!in->free && !a->hold))) stop(a);
    if (gone) a->pending_emote = 0;

    if (gone || s->steps || !in->free) {
        a->still = 0;
        a->next_idle = 0;
        a->slept = false;
    } else if (a->still < 0xFFFF) {
        a->still++;
    }
    if (a->cooldown) a->cooldown--;

    if (!gone && s->event != FOL_EV_NONE && !a->cooldown) {
        on_event(a, s, in, c, rand, ud);
    } else if (a->pending_emote && --a->pending_t == 0) {
        play(a, s, in, NULL, a->pending_emote, true);
        a->pending_emote = 0;
    } else if (a->still && !ambient_active(a) && !a->pending_emote) {
        if (!a->next_idle) a->next_idle = (uint16_t)(240 + rand(ud) % 241);
        if (a->still >= ZZ_STILL && !a->slept) {
            play(a, s, in, NULL, EM_ZZ, false);
            a->slept = true;
            a->next_idle = (uint16_t)(a->still + 480u + rand(ud) % 361);
        } else if (a->still >= a->next_idle && (a->slept || a->still + 480u <= ZZ_STILL)) {
            /* idles due in the 480 frames before Zz wait for it, so the two never run back to back */
            uint32_t r = rand(ud);
            play(a, s, in, &idle_anims[r % 4], idle_emote(c, r), false);
            /* Near saturation, wind still back past Zz (slept stays set) so idles keep coming. */
            if (a->still > 0xFFFF - 900) a->still = ZZ_STILL;
            a->next_idle = (uint16_t)(a->still + 480u + rand(ud) % 361);
        }
    }

    if (ambient_active(a)) {
        talk_tick(&a->player, 0, 0);
        a->player.want_cry = false;
        if (a->player.phase == TALK_DONE) {
            if (a->player.facing) s->facing = a->player.facing;   /* hold the pose until the next step */
            a->player.phase = TALK_IDLE;
            a->hold = false;
        }
    }
}
