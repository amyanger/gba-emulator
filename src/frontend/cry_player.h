#ifndef CRY_PLAYER_H
#define CRY_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#define CRY_MAX_SAMPLES 32768        /* ~3.1 s at 10512 Hz */

typedef struct {
    int8_t pcm[CRY_MAX_SAMPLES];
    uint32_t len;
    uint32_t rate;                   /* source Hz */
    uint64_t pos_fp;                 /* playback position, 32.32 fixed point in source samples */
    bool playing;
} CryPlayer;

/* Decodes a compressed WaveData (16-byte header + 33-byte blocks) from ROM bytes. */
bool cry_decode(const uint8_t* wave, uint32_t avail, int8_t* out, uint32_t cap, uint32_t* len, uint32_t* rate);
void cry_start(CryPlayer* c);
/* Adds the cry into interleaved stereo s16 output at out_rate; advances playback. */
void cry_mix(CryPlayer* c, int16_t* stereo, uint32_t frames, uint32_t out_rate);

#endif // CRY_PLAYER_H
