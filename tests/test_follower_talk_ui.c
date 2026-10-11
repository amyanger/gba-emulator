#include "frontend/follower_talk_ui.h"
#include "input/input.h"
#include "test_harness.h"
#include <string.h>

static HgssTalkScript one_step(uint8_t emote, uint8_t wait) {
    HgssTalkScript s;
    memset(&s, 0, sizeof(s));
    s.step_count = 1;
    s.step[0].emote = emote;
    s.step[0].msg = 1;
    s.step[0].wait = wait;
    s.mood_delta = 10;
    return s;
}

static TalkStepData text_data(uint8_t chars) {
    TalkStepData d;
    memset(&d, 0, sizeof(d));
    d.page_n = 1;
    d.pages[0].len[0] = chars;
    return d;
}

TEST(talk_text_reveals_and_waits_for_a) {
    TalkPlayer t;
    HgssTalkScript s = one_step(0, 1);
    talk_start(&t, &s);
    TalkStepData d = text_data(3);
    talk_set_step_data(&t, &d);
    for (int i = 0; i < 3; i++) talk_tick(&t, 0, 0);
    ASSERT_EQ(t.phase, TALK_TEXT);
    ASSERT_EQ(t.chars, 3);
    talk_tick(&t, 0, 0);
    ASSERT_EQ(t.phase, TALK_TEXT);       /* waits for a press */
    talk_tick(&t, KEY_A, KEY_A);
    ASSERT_EQ(t.phase, TALK_WAIT);
    talk_tick(&t, 0, 0);
    talk_tick(&t, 0, 0);
    ASSERT_EQ(t.phase, TALK_DONE);
    ASSERT_EQ(t.mood_delta, 10);
}

TEST(talk_emote_runs_before_text) {
    TalkPlayer t;
    HgssTalkScript s = one_step(1, 0);
    talk_start(&t, &s);
    TalkStepData d = text_data(1);
    talk_set_step_data(&t, &d);
    talk_tick(&t, 0, 0);
    ASSERT_EQ(t.phase, TALK_EMOTE);
    int8_t dy; uint8_t fr;
    ASSERT_TRUE(talk_emote_pose(&t, &dy, &fr));
    ASSERT_EQ(dy, 6);
    for (int i = 0; i < 14 + 8 + 16 + 24 + 4; i++) talk_tick(&t, 0, 0);
    ASSERT_EQ(t.phase, TALK_TEXT);
}

TEST(talk_anim_offsets_and_cry) {
    TalkPlayer t;
    HgssTalkScript s = one_step(0, 0);
    s.step[0].sound = HGSS_SOUND_CRY;
    talk_start(&t, &s);
    TalkStepData d = text_data(0);
    d.page_n = 0;
    d.anim_n = 2;
    d.anim[0] = (HgssAnimFrame){.dur = 1, .dy = 5, .sound = true};
    d.anim[1] = (HgssAnimFrame){.dur = 1, .dy = -5};
    talk_set_step_data(&t, &d);
    talk_tick(&t, 0, 0);
    ASSERT_EQ(t.off_y, -5);
    ASSERT_TRUE(t.want_cry);
    talk_tick(&t, 0, 0);
    talk_tick(&t, 0, 0);
    ASSERT_EQ(t.off_y, 0);
}

TEST(talk_abort_releases_input) {
    TalkPlayer t;
    HgssTalkScript s = one_step(0, 0);
    talk_start(&t, &s);
    TalkStepData d = text_data(5);
    talk_set_step_data(&t, &d);
    talk_tick(&t, 0, 0);
    ASSERT_TRUE(talk_active(&t));
    talk_abort(&t);
    ASSERT_TRUE(!talk_active(&t));
    ASSERT_EQ(t.mood_delta, 0);
}

void run_follower_talk_ui_tests(void) {
    RUN_TEST(talk_text_reveals_and_waits_for_a);
    RUN_TEST(talk_emote_runs_before_text);
    RUN_TEST(talk_anim_offsets_and_cry);
    RUN_TEST(talk_abort_releases_input);
}
