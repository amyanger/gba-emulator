#include "frontend/follower_talk_ui.h"
#include "input/input.h"
#include <string.h>

#define EMOTE_POP_N 7
#define EMOTE_POP_TICKS 14
#define EMOTE_TOTAL (EMOTE_POP_TICKS + 8 + 16 + 24 + 4)

static const int8_t emote_pop[EMOTE_POP_N] = {6, 10, 12, 12, 10, 6, 0};

void talk_start(TalkPlayer* t, const HgssTalkScript* s) {
    memset(t, 0, sizeof(*t));
    t->script = *s;
    t->phase = TALK_ANIM;
    t->mood_delta = s->mood_delta;
}

void talk_set_step_data(TalkPlayer* t, const TalkStepData* d) {
    memcpy(t->anim, d->anim, sizeof(t->anim));
    t->anim_n = d->anim_n;
    memcpy(t->pages, d->pages, sizeof(t->pages));
    t->page_n = d->page_n;
    t->anim_i = 0;
    t->anim_t = 0;
    t->off_x = 0;
    t->off_y = 0;
    t->emote = t->step < HGSS_STEPS ? t->script.step[t->step].emote : 0;
    t->emote_t = 0;
    t->page_i = 0;
    t->chars = 0;
    t->wait_t = 0;
}

static void talk_next_step(TalkPlayer* t) {
    t->step++;
    if (t->step >= t->script.step_count || t->step >= HGSS_STEPS) {
        t->phase = TALK_DONE;
        return;
    }
    t->phase = TALK_ANIM;
    t->anim_n = 0;
    t->page_n = 0;
}

static uint32_t page_len(const HgssTextPage* p) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < HGSS_TEXT_LINES; i++)
        n += p->len[i];
    return n;
}

void talk_tick(TalkPlayer* t, uint16_t keys_new, uint16_t keys_held) {
    const uint16_t ab = KEY_A | KEY_B;

    if (t->phase == TALK_ANIM) {
        if (t->anim_i < t->anim_n) {
            const HgssAnimFrame* f = &t->anim[t->anim_i];
            if (t->anim_t == 0) {
                t->off_x = (int8_t)(t->off_x + f->dx);
                t->off_y = (int8_t)(t->off_y - f->dy);
                if (f->facing)
                    t->facing = f->facing;
                if (f->sound && t->script.step[t->step].sound >= HGSS_SOUND_CRY)
                    t->want_cry = true;
            }
            t->anim_t++;
            if (t->anim_t < 2u * f->dur)
                return;
            t->anim_i++;
            t->anim_t = 0;
            if (t->anim_i < t->anim_n)
                return;
        }
        t->off_x = 0;
        t->off_y = 0;
        t->phase = t->emote ? TALK_EMOTE : TALK_TEXT;
        t->emote_t = 0;
        t->chars = 0;
        if (t->phase == TALK_EMOTE)
            return;
    } else if (t->phase == TALK_EMOTE) {
        t->emote_t++;
        if (t->emote_t < EMOTE_TOTAL)
            return;
        t->phase = TALK_TEXT;
        t->chars = 0;
    }

    if (t->phase == TALK_TEXT) {
        if (t->page_i >= t->page_n) {
            t->phase = TALK_WAIT;
            t->wait_t = 0;
        } else {
            uint32_t len = page_len(&t->pages[t->page_i]);
            if (t->chars < len) {
                t->chars = (keys_held & ab) ? len : t->chars + 1;
                return;
            }
            if (keys_new & ab) {
                t->page_i++;
                t->chars = 0;
                if (t->page_i >= t->page_n) {
                    t->phase = TALK_WAIT;
                    t->wait_t = 0;
                }
            }
            return;
        }
    }

    if (t->phase == TALK_WAIT) {
        t->wait_t++;
        if (t->wait_t >= 2u * t->script.step[t->step].wait)
            talk_next_step(t);
    }
}

void talk_abort(TalkPlayer* t) {
    t->phase = TALK_DONE;
    t->off_x = 0;
    t->off_y = 0;
    t->mood_delta = 0;
}

bool talk_active(const TalkPlayer* t) {
    return t->phase != TALK_IDLE && t->phase != TALK_DONE;
}

bool talk_emote_pose(const TalkPlayer* t, int8_t* dy, uint8_t* frame) {
    if (t->phase != TALK_EMOTE)
        return false;
    uint16_t e = t->emote_t;
    if (e < EMOTE_POP_TICKS) {
        *dy = emote_pop[e / 2];
        *frame = 0;
        return true;
    }
    e = (uint16_t)(e - EMOTE_POP_TICKS);
    *dy = 0;
    *frame = (e < 8 || e >= 24) ? 1 : 0;
    return true;
}
