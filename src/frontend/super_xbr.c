/*
 * Super-xBR 2x, ported to C from the CPU version in pixelscalers
 * (https://github.com/janert/pixelscalers, src/xbr.cc, adapted there from https://pastebin.com/cbH8ZQQT).
 *
 * Copyright (c) 2016 Hyllian - sergiogdb@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Changes from the original: plain C, opaque pixels only (alpha dropped), ARGB channel order,
 * the three passes share one blend helper.
 */
#include "frontend/super_xbr.h"

#include <math.h>

#define WGT1 0.129633f
#define WGT2 0.175068f

static float df(float a, float b) { return fabsf(a - b); }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static float diagonal_edge(const float m[4][4], const float* wp) {
    float dw1 = wp[0] * (df(m[0][2], m[1][1]) + df(m[1][1], m[2][0]) + df(m[1][3], m[2][2]) + df(m[2][2], m[3][1])) +
                wp[1] * (df(m[0][3], m[1][2]) + df(m[2][1], m[3][0])) +
                wp[2] * (df(m[0][3], m[2][1]) + df(m[1][2], m[3][0])) + wp[3] * df(m[1][2], m[2][1]) +
                wp[4] * (df(m[0][2], m[2][0]) + df(m[1][3], m[3][1])) +
                wp[5] * (df(m[0][1], m[1][0]) + df(m[2][3], m[3][2]));
    float dw2 = wp[0] * (df(m[0][1], m[1][2]) + df(m[1][2], m[2][3]) + df(m[1][0], m[2][1]) + df(m[2][1], m[3][2])) +
                wp[1] * (df(m[0][0], m[1][1]) + df(m[2][2], m[3][3])) +
                wp[2] * (df(m[0][0], m[2][2]) + df(m[1][1], m[3][3])) + wp[3] * df(m[1][1], m[2][2]) +
                wp[4] * (df(m[1][0], m[3][2]) + df(m[0][1], m[2][3])) +
                wp[5] * (df(m[0][2], m[1][3]) + df(m[2][0], m[3][1]));
    return dw1 - dw2;
}

/* A 4x4 window of samples split into channels, as [sx][sy]. */
typedef struct { float c[3][4][4]; float y[4][4]; } Window;

static void window_set(Window* win, int i, int j, uint32_t p) {
    float r = (float)((p >> 16) & 0xFF), g = (float)((p >> 8) & 0xFF), b = (float)(p & 0xFF);
    win->c[0][i][j] = r;
    win->c[1][i][j] = g;
    win->c[2][i][j] = b;
    win->y[i][j] = (float)(0.2126 * r + 0.7152 * g + 0.0722 * b);
}

/* Interpolates along the weaker diagonal of win, clamped to the 4 centre samples of
 * range (anti-ringing). range is win except where the original reuses an earlier window. */
static uint32_t window_blend(const Window* win, const Window* range, const float* wp, float wa, float wb) {
    float d_edge = diagonal_edge(win->y, wp);
    uint32_t out = 0xFF000000u;
    for (int ch = 0; ch < 3; ch++) {
        const float (*m)[4] = win->c[ch];
        const float (*k)[4] = range->c[ch];
        float lo = minf(minf(k[1][1], k[2][1]), minf(k[1][2], k[2][2]));
        float hi = maxf(maxf(k[1][1], k[2][1]), maxf(k[1][2], k[2][2]));
        float v = d_edge <= 0.0f ? wa * (m[0][3] + m[3][0]) + wb * (m[1][2] + m[2][1])
                                 : wa * (m[0][0] + m[3][3]) + wb * (m[1][1] + m[2][2]);
        v = maxf(minf(v, hi), lo);
        int iv = clampi((int)ceilf(v), 0, 255);
        out |= (uint32_t)iv << (16 - 8 * ch);
    }
    return out;
}

void super_xbr_scale2x(const uint32_t* data, int w, int h, uint32_t* out) {
    const int outw = w * 2, outh = h * 2;
    const float wp1[6] = {2.0f, 1.0f, -1.0f, 4.0f, -1.0f, 1.0f};
    const float wp2[6] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Window win, first;

    /* First pass: the source pixel in 3 corners, a diagonal blend in the 4th. */
    for (int y = 0; y < outh; y += 2) {
        for (int x = 0; x < outw; x += 2) {
            int cx = x / 2, cy = y / 2;
            for (int sx = -1; sx <= 2; ++sx)
                for (int sy = -1; sy <= 2; ++sy)
                    window_set(&win, sx + 1, sy + 1,
                               data[clampi(sy + cy, 0, h - 1) * w + clampi(sx + cx, 0, w - 1)]);
            uint32_t p = data[cy * w + cx];
            out[y * outw + x] = out[y * outw + x + 1] = out[(y + 1) * outw + x] = p;
            out[(y + 1) * outw + x + 1] = window_blend(&win, &win, wp1, -WGT1, WGT1 + 0.5f);
        }
    }

    /* Second pass: fill the remaining two pixels from the rotated neighbourhood. */
    for (int y = 0; y < outh; y += 2) {
        for (int x = 0; x < outw; x += 2) {
            for (int sx = -1; sx <= 2; ++sx)
                for (int sy = -1; sy <= 2; ++sy)
                    window_set(&win, sx + 1, sy + 1,
                               out[clampi(sx - sy + y, 0, outh - 1) * outw + clampi(sx + sy + x, 0, outw - 1)]);
            out[y * outw + x + 1] = window_blend(&win, &win, wp2, -WGT2, WGT2 + 0.5f);
            first = win;

            for (int sx = -1; sx <= 2; ++sx)
                for (int sy = -1; sy <= 2; ++sy)
                    window_set(&win, sx + 1, sy + 1,
                               out[clampi(sx - sy + 1 + y, 0, outh - 1) * outw +
                                   clampi(sx + sy - 1 + x, 0, outw - 1)]);
            /* The original clamps this pixel to the previous window's range; kept for the same look. */
            out[(y + 1) * outw + x] = window_blend(&win, &first, wp2, -WGT2, WGT2 + 0.5f);
        }
    }

    /* Third pass: the first pass's weights over every pixel, in place, bottom-right first as in the original. */
    for (int y = outh - 1; y >= 0; --y) {
        for (int x = outw - 1; x >= 0; --x) {
            for (int sx = -2; sx <= 1; ++sx)
                for (int sy = -2; sy <= 1; ++sy)
                    window_set(&win, sx + 2, sy + 2,
                               out[clampi(sy + y, 0, outh - 1) * outw + clampi(sx + x, 0, outw - 1)]);
            out[y * outw + x] = window_blend(&win, &win, wp1, -WGT1, WGT1 + 0.5f);
        }
    }
}
