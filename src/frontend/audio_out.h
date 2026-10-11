#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdint.h>

/* Host-side audio post-processing between the APU ring (32768 Hz stereo
 * int16) and the SDL queue. Pure C, no SDL, no heap:
 *   - 20 Hz DC blocker, standing in for the GBA's output coupling capacitor
 *     (removes the SOUNDBIAS/unipolar-PSG offset and its on/off pops)
 *   - windowed-sinc resampler to the device's native rate, so playback does
 *     not depend on SDL's built-in converter
 *   - short gain ramps so stops and restarts do not click
 * Output only; emulation state and the golden hashes are untouched. */

#define AUDIO_OUT_TAPS    16   /* sinc kernel length in input samples */
#define AUDIO_OUT_PHASES  256  /* kernel table resolution between input samples */
#define AUDIO_OUT_FADE_MS 5

typedef struct {
    double step;  /* input frames per output frame */
    double frac;  /* output position past the kernel centre, in input frames */
    float hist[AUDIO_OUT_TAPS][2];
    float kernel[AUDIO_OUT_PHASES + 1][AUDIO_OUT_TAPS];
    float dc_r;
    float dc_x[2], dc_y[2];
    float gain;        /* 0 after a stop; ramps back to 1 */
    float gain_step;
    float last[2];     /* last emitted frame, where a fade-out starts */
    uint32_t fade_frames;
} AudioOut;

void audio_out_init(AudioOut* ao, uint32_t in_rate, uint32_t out_rate);

/* Upper bound on output frames produced by in_frames input frames. */
uint32_t audio_out_max_frames(const AudioOut* ao, uint32_t in_frames);

/* Process interleaved stereo input. out must hold audio_out_max_frames()
 * frames. Returns frames written. */
uint32_t audio_out_process(AudioOut* ao, const int16_t* in, uint32_t in_frames,
                           int16_t* out);

/* Ramp from the last emitted frame to silence (fade_frames frames, out must
 * hold that many) and arm a fade-in for the next audio_out_process call.
 * Returns frames written. */
uint32_t audio_out_fade_out(AudioOut* ao, int16_t* out);

#endif // AUDIO_OUT_H
