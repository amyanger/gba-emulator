#ifndef MP2K_MIXER_H
#define MP2K_MIXER_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "mp2k/mp2k_detect.h"

/* HQ music: re-renders the MP2K driver's PCM channels in float at the APU's
 * 32768 Hz rate, from the channel state the game's own mixer is about to use.
 * Pure observer: reads emulated memory through game_mem_*, never writes it.
 * Host-side only: not in savestates; reset on load, rewind and toggle. */

struct GBA;

#define MP2K_MAX_CHANS   12
#define MP2K_RATE        32768
#define MP2K_RING_FRAMES 8192   /* HQ output ring, stereo frames, power of two */
#define MP2K_HIST_FRAMES 8192   /* reverb history, power of two, > 7 frames */
#define MP2K_FRAME_MAX   2048   /* samples rendered per hook, at most */
#define MP2K_PRIME_FRAMES 2063  /* HQ ring cushion: the game's measured output latency, ~63 ms */

typedef struct {
    uint8_t flags;
    uint8_t env;
    uint8_t echo_len;
} Mp2kEnv;

typedef struct {
    uint32_t data;        /* address of sample 0 */
    uint32_t size;        /* samples */
    uint32_t loop_start;
    bool loops;
    bool dpcm;            /* 33-byte blocks of 64 delta-coded samples */
} Mp2kWave;

typedef struct {
    bool active;
    uint32_t wav;         /* WaveData address being played */
    double pos;           /* playback position in source samples (forward) */
    float gain_l, gain_r; /* gains at the end of the last frame */
    int32_t dpcm_block;   /* block held in dpcm[], -1 for none */
    int8_t dpcm[64];
} Mp2kVoice;

typedef struct Mp2kMixer {
    bool detected;
    Mp2kDriver drv;
    bool adopt;               /* next frame picks up channels already playing */
    bool have_last;
    uint32_t last_samples;    /* APU samples_produced at the previous hook */
    uint32_t hooks;           /* frames rendered from game state */
    Mp2kVoice voice[MP2K_MAX_CHANS];
    float mix_l[MP2K_FRAME_MAX], mix_r[MP2K_FRAME_MAX];
    float hist_l[MP2K_HIST_FRAMES], hist_r[MP2K_HIST_FRAMES];
    uint32_t hist_pos;
    int16_t ring[MP2K_RING_FRAMES * 2];
    uint32_t ring_read, ring_write;   /* free-running frame counters */
} Mp2kMixer;

/* One frame of SoundMainRAM's envelope step, START handling included. */
Mp2kEnv mp2k_env_step(Mp2kEnv e, uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release,
                      uint8_t echo_vol, bool wav_loops);

/* Reads a WaveData header; false when unreadable or empty. */
bool mp2k_wave_read(const GameMem* mem, uint32_t wav, Mp2kWave* w);

/* Sample i of the wave (reverse counts from the end), looping or 0 past the
 * end, 0 when unreadable. DPCM blocks are cached in the voice. */
float mp2k_wave_at(const GameMem* mem, Mp2kVoice* v, const Mp2kWave* w, bool reverse, int64_t i);

void mp2k_mixer_init(Mp2kMixer* m, const Mp2kDriver* drv);   /* drv NULL: no driver */
void mp2k_mixer_reset(Mp2kMixer* m);
/* GbaPcHook, installed at drv.hook_addr with m as ctx. */
void mp2k_mixer_on_hook(const struct GBA* gba, void* ctx);
/* Copies up to frames stereo frames from the ring, zero-filling the rest.
 * Returns how many came from the ring. */
uint32_t mp2k_mixer_pop(Mp2kMixer* m, int16_t* out, uint32_t frames);
/* Drops up to frames frames, matching samples the frontend threw away. */
void mp2k_mixer_skip(Mp2kMixer* m, uint32_t frames);

#endif // MP2K_MIXER_H
