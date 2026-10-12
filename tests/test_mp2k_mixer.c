#include "test_harness.h"
#include "gba.h"
#include "mp2k/mp2k_mixer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WAV 0x02000000u

static GBA* g;

static void w8(uint32_t a, uint8_t v) {
    if (a >= 0x03000000u) g->bus.iwram[a - 0x03000000u] = v;
    else g->bus.ewram[a - 0x02000000u] = v;
}
static void w32(uint32_t a, uint32_t v) {
    for (int i = 0; i < 4; i++) w8(a + (uint32_t)i, (uint8_t)(v >> (8 * i)));
}
static GameMem mem_of(void) {
    GameMem m = { g->bus.ewram, g->bus.iwram, g->cart.rom, g->cart.rom_size };
    return m;
}
static void make(void) {
    g = calloc(1, sizeof(GBA));
    gba_init(g);
}
/* WaveData at WAV: type 0 (or DPCM), loop flag, loopStart, size, then data. */
static void wave_header(uint32_t size, uint32_t loop_start, bool loops, bool dpcm) {
    w8(WAV, dpcm ? 1 : 0);
    w8(WAV + 3, loops ? 0x40 : 0);
    w32(WAV + 8, loop_start);
    w32(WAV + 12, size);
}

static Mp2kEnv env(uint8_t flags, uint8_t e, uint8_t echo_len) {
    Mp2kEnv x = { flags, e, echo_len };
    return x;
}

TEST(mp2k_env_start_runs_attack_same_frame) {
    Mp2kEnv e = mp2k_env_step(env(0x80, 77, 0), 0x40, 0, 0, 0, 0, false);
    ASSERT_EQ(e.flags, 0x03);
    ASSERT_EQ(e.env, 0x40);
    e = mp2k_env_step(env(0x80, 0, 0), 0x40, 0, 0, 0, 0, true);
    ASSERT_EQ(e.flags, 0x13);               /* LOOP from the wave */
    e = mp2k_env_step(env(0xC0, 50, 0), 0x40, 0, 0, 0, 0, false);
    ASSERT_EQ(e.flags, 0x00);               /* START + STOP: off */
}

TEST(mp2k_env_attack_decay_sustain) {
    Mp2kEnv e = mp2k_env_step(env(0x03, 0xF0, 0), 0x20, 0, 0, 0, 0, false);
    ASSERT_EQ(e.env, 255);
    ASSERT_EQ(e.flags & 3, 2);              /* attack done: decay */
    e = mp2k_env_step(env(0x02, 200, 0), 0, 128, 120, 0, 0, false);
    ASSERT_EQ(e.env, 120);                  /* 200*128>>8 = 100, clamped to sustain */
    ASSERT_EQ(e.flags & 3, 1);
    e = mp2k_env_step(env(0x02, 1, 0), 0, 128, 0, 0, 0, false);
    ASSERT_EQ(e.flags, 0);                  /* sustain 0, no echo: off */
    e = mp2k_env_step(env(0x02, 1, 0), 0, 128, 0, 0, 10, false);
    ASSERT_EQ(e.env, 10);                   /* sustain 0 with echo: echo starts */
    ASSERT_TRUE(e.flags & 0x04);
    e = mp2k_env_step(env(0x01, 99, 0), 0, 0, 0, 0, 0, false);
    ASSERT_EQ(e.env, 99);                   /* sustain holds */
}

TEST(mp2k_env_release_and_echo) {
    Mp2kEnv e = mp2k_env_step(env(0x41, 100, 0), 0, 0, 0, 128, 0, false);
    ASSERT_EQ(e.env, 50);
    ASSERT_EQ(e.flags, 0x41);
    e = mp2k_env_step(env(0x41, 1, 0), 0, 0, 0, 128, 0, false);
    ASSERT_EQ(e.flags, 0);                  /* released to 0, no echo: off */
    e = mp2k_env_step(env(0x41, 20, 0), 0, 0, 0, 128, 15, false);
    ASSERT_EQ(e.env, 15);                   /* 10 <= echo 15: echo at 15 */
    ASSERT_TRUE(e.flags & 0x04);
    e = mp2k_env_step(env(0x45, 15, 3), 0, 0, 0, 128, 15, false);
    ASSERT_EQ(e.echo_len, 2);
    ASSERT_EQ(e.flags, 0x45);               /* echo holds */
    e = mp2k_env_step(env(0x45, 15, 1), 0, 0, 0, 128, 15, false);
    ASSERT_EQ(e.flags, 0);                  /* echo ran out */
}

TEST(mp2k_wave_at_loop_and_end) {
    make();
    wave_header(100, 20, true, false);
    for (uint32_t i = 0; i < 100; i++) w8(WAV + 16 + i, (uint8_t)i);
    GameMem m = mem_of();
    Mp2kWave w;
    Mp2kVoice v; memset(&v, 0, sizeof(v)); v.dpcm_block = -1;
    ASSERT_TRUE(mp2k_wave_read(&m, WAV, &w));
    ASSERT_EQ(w.size, 100u);
    ASSERT_TRUE(w.loops);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 5) == 5.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 100) == 20.0f);   /* wraps to loopStart */
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, -1) == 0.0f);     /* clamps to sample 0 */
    w.loops = false;
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 100) == 0.0f);    /* past the end: silence */
    free(g);
}

TEST(mp2k_wave_at_reverse) {
    make();
    wave_header(10, 0, false, false);
    for (uint32_t i = 0; i < 10; i++) w8(WAV + 16 + i, (uint8_t)i);
    GameMem m = mem_of();
    Mp2kWave w;
    Mp2kVoice v; memset(&v, 0, sizeof(v)); v.dpcm_block = -1;
    ASSERT_TRUE(mp2k_wave_read(&m, WAV, &w));
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, true, 0) == 9.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, true, 2) == 7.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, true, 10) == 0.0f);
    free(g);
}

TEST(mp2k_wave_at_dpcm_block) {
    make();
    wave_header(64, 0, false, true);
    w8(WAV + 16, 10);      /* sample 0: raw 10 */
    w8(WAV + 17, 0x01);    /* sample 1: +1 -> 11 */
    w8(WAV + 18, 0x2F);    /* samples 2,3: +4 -> 15, -1 -> 14 */
    GameMem m = mem_of();
    Mp2kWave w;
    Mp2kVoice v; memset(&v, 0, sizeof(v)); v.dpcm_block = -1;
    ASSERT_TRUE(mp2k_wave_read(&m, WAV, &w));
    ASSERT_TRUE(w.dpcm);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 0) == 10.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 1) == 11.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 2) == 15.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 3) == 14.0f);
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 63) == 14.0f);   /* zero nibbles: delta 0 */
    free(g);
}

TEST(mp2k_wave_at_huge_size_stays_in_bounds) {
    make();
    wave_header(0xFFFFFFF0u, 0, false, false);
    GameMem m = mem_of();
    Mp2kWave w;
    Mp2kVoice v; memset(&v, 0, sizeof(v)); v.dpcm_block = -1;
    ASSERT_TRUE(mp2k_wave_read(&m, WAV, &w));
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 0x7FFFFFF0) == 0.0f);   /* unmapped: 0, no overread */
    w.dpcm = true;
    ASSERT_TRUE(mp2k_wave_at(&m, &v, &w, false, 0x7FFFFFF0) == 0.0f);
    free(g);
}

#define INFO 0x03000800u
#define CH0  (INFO + 0x50u)

static Mp2kMixer s_m;

/* SoundInfo with one channel, master volume 15, Emerald's rate; FIFO A to
 * the right speaker and B to the left, both at 100%. */
static void setup_mixer(void) {
    make();
    g->apu.soundcnt_x = 0x80;
    g->apu.soundcnt_h = (uint16_t)((1u << 2) | (1u << 3) | (1u << 8) | (1u << 13));
    Mp2kDriver d = { 0x08000000u, 0x03007FF0u, 0x03001AA8u };
    mp2k_mixer_init(&s_m, &d);
    w32(0x03007FF0u, INFO);
    w32(INFO, MP2K_ID_NUMBER + 1u);
    w8(INFO + 6, 1);          /* maxChans */
    w8(INFO + 7, 15);         /* masterVolume */
    w8(INFO + 0x0B, 7);       /* pcmDmaPeriod */
    w32(INFO + 0x10, 224);    /* pcmSamplesPerVBlank */
    w32(INFO + 0x14, 13379);  /* pcmFreq */
}

static void chan(uint8_t flags, uint8_t type, uint32_t freq, uint32_t count, uint8_t right, uint8_t left) {
    w8(CH0, flags);
    w8(CH0 + 1, type);
    w8(CH0 + 2, right);
    w8(CH0 + 3, left);
    w8(CH0 + 4, 255);         /* attack: full on the first frame */
    w8(CH0 + 5, 0);
    w8(CH0 + 6, 255);         /* sustain */
    w8(CH0 + 7, 0);
    w8(CH0 + 9, 0);
    w32(CH0 + 0x18, count);
    w32(CH0 + 0x20, freq);
    w32(CH0 + 0x24, WAV);
}

static void hook(void) {
    mp2k_mixer_on_hook(g, &s_m);
    g->apu.samples_produced += 549;
}

static int16_t s_out[MP2K_RING_FRAMES * 2];

TEST(mp2k_right_mix_plays_on_fifo_a_side) {
    setup_mixer();
    wave_header(64, 0, true, false);
    for (uint32_t i = 0; i < 64; i++) w8(WAV + 16 + i, 100);
    chan(0x80, 0, 32768, 0, 255, 0);           /* START, right only */
    hook();
    /* priming silence, then frame 1 (instant start) */
    ASSERT_EQ(mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES + 549), (uint32_t)MP2K_PRIME_FRAMES + 549u);
    w8(CH0, 0x11);                             /* what the game leaves: LOOP | sustain */
    w8(CH0 + 9, 255);
    hook();                                    /* ring was drained: cushion refilled, then frame 2 */
    ASSERT_EQ(mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES), (uint32_t)MP2K_PRIME_FRAMES);   /* refilled cushion */
    ASSERT_EQ(mp2k_mixer_pop(&s_m, s_out, 549), 549u);   /* frame 2: steady */
    /* 100 * (255*255>>8)/256 = 99.22, x4 (FIFO 100%) x32 = 12700 */
    ASSERT_TRUE(abs(s_out[2 * 100 + 1] - 12700) <= 2);
    ASSERT_EQ(s_out[2 * 100], 0);
    ASSERT_EQ(s_m.hooks, 2u);
    free(g);
}

TEST(mp2k_pitch_steps) {
    setup_mixer();
    wave_header(4000, 0, false, false);
    chan(0x80, 0, 16384, 0, 255, 255);
    hook();
    ASSERT_TRUE(fabs(s_m.voice[0].pos - 274.5) < 1e-6);
    free(g);

    setup_mixer();
    wave_header(4000, 0, false, false);
    chan(0x80, 0x08, 99999, 0, 255, 255);      /* fixed frequency ignores frequency */
    hook();
    ASSERT_TRUE(fabs(s_m.voice[0].pos - 549.0 * 13379.0 / 32768.0) < 1e-6);
    free(g);
}

TEST(mp2k_loop_wraps_and_oneshot_ends) {
    setup_mixer();
    wave_header(100, 20, true, false);
    chan(0x80, 0, 32768, 0, 255, 255);
    hook();
    ASSERT_TRUE(s_m.voice[0].active);
    ASSERT_TRUE(fabs(s_m.voice[0].pos - 69.0) < 1e-6);   /* 20 + (549-20) % 80 */
    free(g);

    setup_mixer();
    wave_header(100, 0, false, false);
    chan(0x80, 0, 32768, 0, 255, 255);
    hook();
    ASSERT_TRUE(!s_m.voice[0].active);
    free(g);
}

TEST(mp2k_adopts_playing_channel_after_reset) {
    setup_mixer();
    wave_header(100, 0, false, false);
    chan(0x01, 0, 0, 40, 255, 255);            /* sustaining, 40 samples left, no START */
    w8(CH0 + 9, 200);
    hook();
    ASSERT_TRUE(s_m.voice[0].active);
    ASSERT_TRUE(fabs(s_m.voice[0].pos - 60.0) < 1e-6);   /* size - count */
    free(g);
}

TEST(mp2k_reverb_taps) {
    setup_mixer();
    w8(INFO + 5, 64);                          /* reverb */
    /* Taps: 7*224 and 6*224 game samples = 3840 and 3291 at 32768 Hz. */
    s_m.hist_l[(0u - 3840u) & (MP2K_HIST_FRAMES - 1)] = 100.0f;
    s_m.hist_r[(0u - 3291u) & (MP2K_HIST_FRAMES - 1)] = 50.0f;
    hook();
    mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES);          /* priming silence */
    mp2k_mixer_pop(&s_m, s_out, 549);
    /* 64/512 * 150 = 18.75, x4 x32 = 2400 on both sides */
    ASSERT_EQ(s_out[0], 2400);
    ASSERT_EQ(s_out[1], 2400);
    ASSERT_EQ(s_out[2], 0);
    free(g);
}

TEST(mp2k_ident_mismatch_renders_silence_without_counting) {
    setup_mixer();
    w32(INFO, MP2K_ID_NUMBER);                 /* driver idle: SoundMain skipped */
    hook();
    ASSERT_EQ(s_m.hooks, 0u);
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, MP2K_PRIME_FRAMES + 549u);
    free(g);
}

TEST(mp2k_bad_wave_pointer_is_silent) {
    setup_mixer();
    chan(0x80, 0, 32768, 0, 255, 255);
    w32(CH0 + 0x24, 0x0E000000u);              /* not ROM or RAM */
    hook();
    ASSERT_TRUE(!s_m.voice[0].active);
    ASSERT_EQ(s_m.hooks, 1u);
    free(g);
}

TEST(mp2k_skip_drops_frames) {
    setup_mixer();
    mp2k_mixer_skip(&s_m, 100);
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, MP2K_PRIME_FRAMES - 100u);
    mp2k_mixer_skip(&s_m, 10000);
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, 0u);
    ASSERT_EQ(mp2k_mixer_pop(&s_m, s_out, 4), 0u);
    ASSERT_EQ(s_out[0], 0);
    free(g);
}

TEST(mp2k_too_many_chans_skips_frame) {
    setup_mixer();
    w8(INFO + 6, 13);
    hook();
    ASSERT_EQ(s_m.hooks, 0u);
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, MP2K_PRIME_FRAMES + 549u);
    free(g);
}

TEST(mp2k_start_is_instant) {
    setup_mixer();
    wave_header(64, 0, true, false);
    for (uint32_t i = 0; i < 64; i++) w8(WAV + 16 + i, 100);
    chan(0x80, 0, 32768, 0, 255, 0);
    hook();
    mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES);
    mp2k_mixer_pop(&s_m, s_out, 549);
    ASSERT_TRUE(abs(s_out[1] - 12700) <= 2);
    ASSERT_EQ(s_out[0], 0);
    free(g);
}

TEST(mp2k_frame_size_follows_apu) {
    setup_mixer();
    hook();
    mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES);          /* priming silence */
    g->apu.samples_produced = s_m.last_samples + 300;
    mp2k_mixer_on_hook(g, &s_m);               /* cushion refilled, then the 300-sample frame */
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, MP2K_PRIME_FRAMES + 300u);
    free(g);
}

TEST(mp2k_refills_cushion_after_drain) {
    setup_mixer();
    hook();
    mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES + 549);
    mp2k_mixer_pop(&s_m, s_out, 2000);         /* empty ring: zero-filled */
    mp2k_mixer_on_hook(g, &s_m);
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, MP2K_PRIME_FRAMES + 549u);
    free(g);
}

TEST(mp2k_resyncs_after_gap) {
    setup_mixer();
    hook();
    mp2k_mixer_pop(&s_m, s_out, MP2K_PRIME_FRAMES);          /* priming silence */
    g->apu.samples_produced = s_m.last_samples + 5000;
    mp2k_mixer_on_hook(g, &s_m);               /* long gap: n capped at MP2K_FRAME_MAX */
    g->apu.samples_produced = s_m.last_samples + 5000;
    mp2k_mixer_on_hook(g, &s_m);               /* second gap: ring now holds more than the cushion */
    mp2k_mixer_pop(&s_m, s_out, 549);
    g->apu.samples_produced += 549;
    mp2k_mixer_on_hook(g, &s_m);
    ASSERT_EQ(s_m.ring_write - s_m.ring_read, MP2K_PRIME_FRAMES + 549u);
    free(g);
}

void run_mp2k_mixer_tests(void) {
    TEST_SUITE("mp2k_mixer");
    RUN_TEST(mp2k_env_start_runs_attack_same_frame);
    RUN_TEST(mp2k_env_attack_decay_sustain);
    RUN_TEST(mp2k_env_release_and_echo);
    RUN_TEST(mp2k_wave_at_loop_and_end);
    RUN_TEST(mp2k_wave_at_reverse);
    RUN_TEST(mp2k_wave_at_dpcm_block);
    RUN_TEST(mp2k_wave_at_huge_size_stays_in_bounds);
    RUN_TEST(mp2k_right_mix_plays_on_fifo_a_side);
    RUN_TEST(mp2k_pitch_steps);
    RUN_TEST(mp2k_loop_wraps_and_oneshot_ends);
    RUN_TEST(mp2k_adopts_playing_channel_after_reset);
    RUN_TEST(mp2k_reverb_taps);
    RUN_TEST(mp2k_ident_mismatch_renders_silence_without_counting);
    RUN_TEST(mp2k_bad_wave_pointer_is_silent);
    RUN_TEST(mp2k_skip_drops_frames);
    RUN_TEST(mp2k_too_many_chans_skips_frame);
    RUN_TEST(mp2k_start_is_instant);
    RUN_TEST(mp2k_frame_size_follows_apu);
    RUN_TEST(mp2k_refills_cushion_after_drain);
    RUN_TEST(mp2k_resyncs_after_gap);
}
