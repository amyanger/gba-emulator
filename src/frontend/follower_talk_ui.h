#ifndef FOLLOWER_TALK_UI_H
#define FOLLOWER_TALK_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "hgss/hgss_assets.h"

typedef enum { TALK_IDLE, TALK_ANIM, TALK_EMOTE, TALK_TEXT, TALK_WAIT, TALK_DONE } TalkPhase;

typedef struct {
    TalkPhase phase;
    HgssTalkScript script;
    uint8_t step;
    HgssAnimFrame anim[HGSS_ANIM_FRAMES];
    uint8_t anim_n, anim_i;
    uint16_t anim_t;
    int8_t off_x, off_y;          /* follower pixel offset from the hop animation */
    uint8_t facing;               /* 0 keep, else game 1..4 */
    uint8_t emote;                /* 1-based, 0 none */
    uint16_t emote_t;
    HgssTextPage pages[HGSS_PAGE_MAX];
    uint8_t page_n, page_i;
    uint32_t chars;
    uint16_t wait_t;
    bool want_cry;                /* frontend starts the cry and clears this */
    int8_t mood_delta;            /* applied by the frontend when phase becomes TALK_DONE */
} TalkPlayer;

/* Prepared step data: anims and text pages resolved by the caller from assets. */
typedef struct {
    HgssAnimFrame anim[HGSS_ANIM_FRAMES];
    uint8_t anim_n;
    HgssTextPage pages[HGSS_PAGE_MAX];
    uint8_t page_n;
} TalkStepData;

void talk_start(TalkPlayer* t, const HgssTalkScript* s);
/* Called when phase needs data for t->step (phase == TALK_ANIM at step start). */
void talk_set_step_data(TalkPlayer* t, const TalkStepData* d);
/* One 60 Hz frame. keys_new: host GBA-key bits newly pressed (KEY_A | KEY_B). */
void talk_tick(TalkPlayer* t, uint16_t keys_new, uint16_t keys_held);
void talk_abort(TalkPlayer* t);
bool talk_active(const TalkPlayer* t);
/* Emote vertical offset and frame (0/1) for the current emote_t; false when not shown. */
bool talk_emote_pose(const TalkPlayer* t, int8_t* dy, uint8_t* frame);

#endif // FOLLOWER_TALK_UI_H
