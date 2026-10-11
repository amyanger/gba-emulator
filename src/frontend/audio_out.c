#include "frontend/audio_out.h"

#include <math.h>
#include <string.h>

#define AO_PI 3.14159265358979323846
#define AO_DC_HZ 20.0
/* Sinc cutoff as a fraction of the lower Nyquist rate; the rest of the band
 * is the window's transition. */
#define AO_CUTOFF 0.9

/* Output times sit between hist[AO_CENTRE] and hist[AO_CENTRE + 1]. */
#define AO_CENTRE (AUDIO_OUT_TAPS / 2 - 1)

static double sinc(double x) {
    return x == 0.0 ? 1.0 : sin(AO_PI * x) / (AO_PI * x);
}

void audio_out_init(AudioOut* ao, uint32_t in_rate, uint32_t out_rate) {
    memset(ao, 0, sizeof(*ao));
    ao->step = (double)in_rate / (double)out_rate;
    ao->frac = 1.0;
    ao->dc_r = (float)(1.0 - 2.0 * AO_PI * AO_DC_HZ / (double)in_rate);
    ao->fade_frames = out_rate * AUDIO_OUT_FADE_MS / 1000u;
    if (ao->fade_frames < 2) ao->fade_frames = 2;
    ao->gain_step = 1.0f / (float)ao->fade_frames;

    /* Blackman-windowed sinc, one row per fractional phase. Cutoff in cycles
     * per input sample: below the input Nyquist when upsampling, below the
     * output Nyquist when downsampling. */
    double fc = 0.5 * AO_CUTOFF * (ao->step > 1.0 ? 1.0 / ao->step : 1.0);
    double half = AUDIO_OUT_TAPS / 2.0;
    for (int p = 0; p <= AUDIO_OUT_PHASES; p++) {
        double t = AO_CENTRE + (double)p / AUDIO_OUT_PHASES;
        double sum = 0.0;
        double row[AUDIO_OUT_TAPS];
        for (int k = 0; k < AUDIO_OUT_TAPS; k++) {
            double x = (double)k - t;
            double u = (x + half) / (2.0 * half);
            double w = 0.42 - 0.5 * cos(2.0 * AO_PI * u) + 0.08 * cos(4.0 * AO_PI * u);
            row[k] = 2.0 * fc * sinc(2.0 * fc * x) * w;
            sum += row[k];
        }
        for (int k = 0; k < AUDIO_OUT_TAPS; k++)  /* unity gain at DC */
            ao->kernel[p][k] = (float)(row[k] / sum);
    }
}

uint32_t audio_out_max_frames(const AudioOut* ao, uint32_t in_frames) {
    return (uint32_t)ceil((double)in_frames / ao->step) + 1u;
}

static int16_t to_s16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

uint32_t audio_out_process(AudioOut* ao, const int16_t* in, uint32_t in_frames,
                           int16_t* out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < in_frames; i++) {
        memmove(ao->hist[0], ao->hist[1], sizeof(ao->hist) - sizeof(ao->hist[0]));
        for (int c = 0; c < 2; c++) {
            float x = (float)in[i * 2 + c];
            float y = x - ao->dc_x[c] + ao->dc_r * ao->dc_y[c];
            ao->dc_x[c] = x;
            ao->dc_y[c] = y;
            ao->hist[AUDIO_OUT_TAPS - 1][c] = y;
        }

        ao->frac -= 1.0;
        while (ao->frac < 1.0) {
            /* Linear blend between the two nearest kernel phases. */
            double pos = ao->frac * AUDIO_OUT_PHASES;
            int p = (int)pos;
            float mix = (float)(pos - p);
            const float* k0 = ao->kernel[p];
            const float* k1 = ao->kernel[p + 1];
            float acc[2] = {0.0f, 0.0f};
            for (int k = 0; k < AUDIO_OUT_TAPS; k++) {
                float w = k0[k] + (k1[k] - k0[k]) * mix;
                acc[0] += ao->hist[k][0] * w;
                acc[1] += ao->hist[k][1] * w;
            }
            for (int c = 0; c < 2; c++) {
                ao->last[c] = acc[c] * ao->gain;
                out[n * 2 + c] = to_s16(ao->last[c]);
            }
            if (ao->gain < 1.0f) {
                ao->gain += ao->gain_step;
                if (ao->gain > 1.0f) ao->gain = 1.0f;
            }
            n++;
            ao->frac += ao->step;
        }
    }
    return n;
}

uint32_t audio_out_fade_out(AudioOut* ao, int16_t* out) {
    uint32_t f = ao->fade_frames;
    for (uint32_t i = 0; i < f; i++) {
        float g = (float)(f - 1 - i) / (float)(f - 1);
        out[i * 2] = to_s16(ao->last[0] * g);
        out[i * 2 + 1] = to_s16(ao->last[1] * g);
    }
    ao->last[0] = ao->last[1] = 0.0f;
    ao->gain = 0.0f;
    return f;
}
