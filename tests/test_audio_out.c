#include "test_harness.h"
#include "frontend/audio_out.h"
#include <math.h>
#include <stdlib.h>

#define IN_RATE 32768u
#define PI 3.14159265358979323846

static AudioOut s_ao;
static int16_t s_in[IN_RATE * 2];
static int16_t s_out[IN_RATE * 6 * 2];

static void fill_sine(double hz, double amp) {
    for (uint32_t i = 0; i < IN_RATE; i++) {
        int16_t v = (int16_t)lrint(amp * sin(2.0 * PI * hz * i / IN_RATE));
        s_in[i * 2] = v;
        s_in[i * 2 + 1] = v;
    }
}

static int32_t peak(const int16_t* buf, uint32_t from, uint32_t to) {
    int32_t p = 0;
    for (uint32_t i = from; i < to; i++) {
        int32_t v = abs(buf[i * 2]);
        if (v > p) p = v;
    }
    return p;
}

TEST(audio_out_resamples_to_device_rate) {
    audio_out_init(&s_ao, IN_RATE, 48000);
    fill_sine(440.0, 1000.0);
    uint32_t n = audio_out_process(&s_ao, s_in, IN_RATE, s_out);
    ASSERT_TRUE(n >= 47999 && n <= 48001);
    ASSERT_TRUE(n <= audio_out_max_frames(&s_ao, IN_RATE));
}

TEST(audio_out_keeps_tone_level) {
    /* A 1 kHz tone passes the DC blocker and the resampler at full level. */
    audio_out_init(&s_ao, IN_RATE, 44100);
    fill_sine(1000.0, 10000.0);
    uint32_t n = audio_out_process(&s_ao, s_in, IN_RATE, s_out);
    int32_t p = peak(s_out, n / 2, n);
    ASSERT_TRUE(p > 9700 && p < 10300);
}

TEST(audio_out_removes_dc_offset) {
    audio_out_init(&s_ao, IN_RATE, 48000);
    for (uint32_t i = 0; i < IN_RATE * 2; i++) s_in[i] = 8000;
    uint32_t n = audio_out_process(&s_ao, s_in, IN_RATE, s_out);
    ASSERT_TRUE(peak(s_out, n - 1000, n) < 20);
}

TEST(audio_out_fade_out_starts_at_last_frame) {
    audio_out_init(&s_ao, IN_RATE, 48000);
    fill_sine(100.0, 12000.0);
    /* 12.5 ms in (past the fade-in): 1.25 periods, a positive peak. */
    uint32_t n = audio_out_process(&s_ao, s_in, IN_RATE * 5 / 400, s_out);
    int16_t last = s_out[(n - 1) * 2];
    ASSERT_TRUE(last > 5000);

    uint32_t f = audio_out_fade_out(&s_ao, s_out);
    ASSERT_EQ(f, 240u); /* 5 ms at 48 kHz */
    ASSERT_TRUE(abs(s_out[0] - last) < 200);
    for (uint32_t i = 1; i < f; i++) ASSERT_TRUE(s_out[i * 2] <= s_out[(i - 1) * 2]);
    ASSERT_EQ(s_out[(f - 1) * 2], 0);
}

TEST(audio_out_fades_in_after_stop) {
    audio_out_init(&s_ao, IN_RATE, 48000);
    fill_sine(1000.0, 10000.0);
    audio_out_process(&s_ao, s_in, IN_RATE / 10, s_out);
    audio_out_fade_out(&s_ao, s_out);

    uint32_t n = audio_out_process(&s_ao, s_in, IN_RATE / 10, s_out);
    ASSERT_TRUE(peak(s_out, 0, 24) < 1500);    /* first 0.5 ms: still quiet */
    ASSERT_TRUE(peak(s_out, n / 2, n) > 9700); /* back to full level */
}

void run_audio_out_tests(void) {
    TEST_SUITE("audio_out");
    RUN_TEST(audio_out_resamples_to_device_rate);
    RUN_TEST(audio_out_keeps_tone_level);
    RUN_TEST(audio_out_removes_dc_offset);
    RUN_TEST(audio_out_fade_out_starts_at_last_frame);
    RUN_TEST(audio_out_fades_in_after_stop);
}
