/*
 * xBR 2x/3x/4x by Hyllian, ported to C from xBRjs (https://github.com/joseprio/xBRjs).
 *
 * Copyright 2020 Josep del Rio
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Changes from xBRjs: opaque pixels only (no alpha cases), ARGB channel order, colors always
 * blended, and the YUV distance in exact integers (coefficients x 1e6) instead of doubles, so
 * results do not depend on the compiler's floating-point contraction.
 */
#include "frontend/xbr.h"

#include <stdbool.h>
#include <stddef.h>

#define FIX 1000000   /* YUV coefficients have six decimals */
#define THR_Y 48
#define THR_U 7
#define THR_V 6

typedef struct { int32_t y, u, v; } Yuv;

static Yuv yuv(uint32_t p) {
    int32_t r = (int32_t)((p >> 16) & 0xFF), g = (int32_t)((p >> 8) & 0xFF), b = (int32_t)(p & 0xFF);
    Yuv c = {r * 299000 + g * 587000 + b * 114000, r * -168736 + g * -331264 + b * 500000,
             r * 500000 + g * -418688 + b * -81312};
    return c;
}

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* xBRjs yuvDifference, times FIX. */
static int64_t ydiff(uint32_t a, uint32_t b) {
    Yuv x = yuv(a), y = yuv(b);
    return (int64_t)iabs(x.y - y.y) * THR_Y + (int64_t)iabs(x.u - y.u) * THR_U + (int64_t)iabs(x.v - y.v) * THR_V;
}

/* xBRjs applies << to the double distance, which truncates it to an integer first. */
static int64_t ytrunc_shl(int64_t d, int s) { return (d / FIX << s) * FIX; }

static bool yeq(uint32_t a, uint32_t b) {
    Yuv x = yuv(a), y = yuv(b);
    return iabs(x.y - y.y) <= THR_Y * FIX && iabs(x.u - y.u) <= THR_U * FIX && iabs(x.v - y.v) <= THR_V * FIX;
}

/* (q2 * src + q1 * dst) / (q1 + q2), per channel, floored. */
static uint32_t mix(uint32_t dst, uint32_t src, uint32_t q1, uint32_t q2) {
    uint32_t s = q1 + q2;
    uint32_t r = (q2 * ((src >> 16) & 0xFF) + q1 * ((dst >> 16) & 0xFF)) / s;
    uint32_t g = (q2 * ((src >> 8) & 0xFF) + q1 * ((dst >> 8) & 0xFF)) / s;
    uint32_t b = (q2 * (src & 0xFF) + q1 * (dst & 0xFF)) / s;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

#define B32(d, s)  mix(d, s, 7, 1)
#define B64(d, s)  mix(d, s, 3, 1)
#define B128(d, s) mix(d, s, 1, 1)
#define B192(d, s) mix(d, s, 1, 3)
#define B224(d, s) mix(d, s, 1, 7)

/* Edge weights shared by every kernel. Returns false when pe is not on an edge. */
static bool edge(uint32_t pe, uint32_t pi, uint32_t ph, uint32_t pf, uint32_t pg, uint32_t pc, uint32_t pd,
                 uint32_t pb, uint32_t f4, uint32_t i4, uint32_t h5, uint32_t i5, int64_t* e, int64_t* i,
                 uint32_t* px) {
    if (!(pe != ph && pe != pf)) return false;
    *e = ydiff(pe, pc) + ydiff(pe, pg) + ydiff(pi, h5) + ydiff(pi, f4) + ytrunc_shl(ydiff(ph, pf), 2);
    *i = ydiff(ph, pd) + ydiff(ph, i5) + ydiff(pf, i4) + ydiff(pf, pb) + ytrunc_shl(ydiff(pe, pi), 2);
    *px = ydiff(pe, pf) <= ydiff(pe, ph) ? pf : ph;
    return true;
}

static bool corner(uint32_t pe, uint32_t pi, uint32_t ph, uint32_t pf, uint32_t pd, uint32_t pb, uint32_t pg,
                   uint32_t pc, uint32_t f4, uint32_t i4, uint32_t h5, uint32_t i5) {
    (void)f4; (void)h5;
    return (!yeq(pf, pb) && !yeq(ph, pd)) || (yeq(pe, pi) && !yeq(pf, i4) && !yeq(ph, i5)) || yeq(pe, pg) ||
           yeq(pe, pc);
}

typedef struct { bool left, up; } Slope;

static Slope slope(uint32_t pe, uint32_t pf, uint32_t pg, uint32_t ph, uint32_t pc, uint32_t pb, uint32_t pd) {
    int64_t ke = ydiff(pf, pg), ki = ydiff(ph, pc);
    bool ex2 = pe != pc && pb != pc, ex3 = pe != pg && pd != pg;
    Slope s = {ytrunc_shl(ke, 1) <= ki && ex3, ke >= ytrunc_shl(ki, 1) && ex2};
    return s;
}

static void kernel2x(uint32_t pe, uint32_t pi, uint32_t ph, uint32_t pf, uint32_t pg, uint32_t pc, uint32_t pd,
                     uint32_t pb, uint32_t f4, uint32_t i4, uint32_t h5, uint32_t i5, uint32_t* n1, uint32_t* n2,
                     uint32_t* n3) {
    int64_t e, i;
    uint32_t px;
    if (!edge(pe, pi, ph, pf, pg, pc, pd, pb, f4, i4, h5, i5, &e, &i, &px)) return;
    if (e < i && corner(pe, pi, ph, pf, pd, pb, pg, pc, f4, i4, h5, i5)) {
        Slope s = slope(pe, pf, pg, ph, pc, pb, pd);
        if (s.left || s.up) {
            if (s.left) { *n3 = B192(*n3, px); *n2 = B64(*n2, px); }
            if (s.up) { *n3 = B192(*n3, px); *n1 = B64(*n1, px); }
        } else {
            *n3 = B128(*n3, px);
        }
    } else if (e <= i) {
        *n3 = B64(*n3, px);
    }
}

static void kernel3x(uint32_t pe, uint32_t pi, uint32_t ph, uint32_t pf, uint32_t pg, uint32_t pc, uint32_t pd,
                     uint32_t pb, uint32_t f4, uint32_t i4, uint32_t h5, uint32_t i5, uint32_t* n2, uint32_t* n5,
                     uint32_t* n6, uint32_t* n7, uint32_t* n8) {
    int64_t e, i;
    uint32_t px;
    if (!edge(pe, pi, ph, pf, pg, pc, pd, pb, f4, i4, h5, i5, &e, &i, &px)) return;
    /* xBRjs's default (improved) 3x corner test. */
    bool state = e < i && ((!yeq(pf, pb) && !yeq(pf, pc)) || (!yeq(ph, pd) && !yeq(ph, pg)) ||
                           (yeq(pe, pi) && ((!yeq(pf, f4) && !yeq(pf, i4)) || (!yeq(ph, h5) && !yeq(ph, i5)))) ||
                           yeq(pe, pg) || yeq(pe, pc));
    if (state) {
        Slope s = slope(pe, pf, pg, ph, pc, pb, pd);
        if (s.left && s.up) {
            uint32_t a = B192(*n7, px), b = B64(*n6, px);
            *n7 = a; *n5 = a; *n6 = b; *n2 = b; *n8 = px;
        } else if (s.left) {
            *n7 = B192(*n7, px); *n5 = B64(*n5, px); *n6 = B64(*n6, px); *n8 = px;
        } else if (s.up) {
            *n5 = B192(*n5, px); *n7 = B64(*n7, px); *n2 = B64(*n2, px); *n8 = px;
        } else {
            *n8 = B224(*n8, px); *n5 = B32(*n5, px); *n7 = B32(*n7, px);
        }
    } else if (e <= i) {
        *n8 = B128(*n8, px);
    }
}

static void kernel4x(uint32_t pe, uint32_t pi, uint32_t ph, uint32_t pf, uint32_t pg, uint32_t pc, uint32_t pd,
                     uint32_t pb, uint32_t f4, uint32_t i4, uint32_t h5, uint32_t i5, uint32_t* n15, uint32_t* n14,
                     uint32_t* n11, uint32_t* n3, uint32_t* n7, uint32_t* n10, uint32_t* n13, uint32_t* n12) {
    int64_t e, i;
    uint32_t px;
    if (!edge(pe, pi, ph, pf, pg, pc, pd, pb, f4, i4, h5, i5, &e, &i, &px)) return;
    if (e < i && corner(pe, pi, ph, pf, pd, pb, pg, pc, f4, i4, h5, i5)) {
        Slope s = slope(pe, pf, pg, ph, pc, pb, pd);
        if (s.left || s.up) {
            if (s.left) {
                *n15 = px; *n14 = px; *n11 = B192(*n11, px); *n13 = B192(*n13, px);
                *n12 = B64(*n12, px); *n10 = B64(*n10, px);
            }
            if (s.up) {
                *n15 = px; *n14 = B192(*n14, px); *n11 = px; *n3 = B64(*n3, px);
                *n7 = B192(*n7, px); *n10 = B64(*n10, px);
            }
        } else {
            *n15 = px; *n14 = B128(*n14, px); *n11 = B128(*n11, px);
        }
    } else if (e <= i) {
        *n15 = B128(*n15, px);
    }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void xbr_scale(const uint32_t* in, int w, int h, uint32_t* out, int factor) {
    int ow = w * factor;
    for (int y = 0; y < h; y++) {
        int ym2 = clampi(y - 2, 0, h - 1) * w, ym1 = clampi(y - 1, 0, h - 1) * w, y0 = y * w;
        int yp1 = clampi(y + 1, 0, h - 1) * w, yp2 = clampi(y + 2, 0, h - 1) * w;
        for (int x = 0; x < w; x++) {
            int xm2 = clampi(x - 2, 0, w - 1), xm1 = clampi(x - 1, 0, w - 1);
            int xp1 = clampi(x + 1, 0, w - 1), xp2 = clampi(x + 2, 0, w - 1);
            uint32_t a1 = in[xm1 + ym2], b1 = in[x + ym2], c1 = in[xp1 + ym2];
            uint32_t a0 = in[xm2 + ym1], pa = in[xm1 + ym1], pb = in[x + ym1], pc = in[xp1 + ym1], c4 = in[xp2 + ym1];
            uint32_t d0 = in[xm2 + y0], pd = in[xm1 + y0], pe = in[x + y0], pf = in[xp1 + y0], f4 = in[xp2 + y0];
            uint32_t g0 = in[xm2 + yp1], pg = in[xm1 + yp1], ph = in[x + yp1], pi = in[xp1 + yp1], i4 = in[xp2 + yp1];
            uint32_t g5 = in[xm1 + yp2], h5 = in[x + yp2], i5 = in[xp1 + yp2];
            uint32_t e[16];
            for (int k = 0; k < factor * factor; k++) e[k] = pe;

            /* The same kernel rotated to each of the four corners. */
            if (factor == 2) {
                kernel2x(pe, pi, ph, pf, pg, pc, pd, pb, f4, i4, h5, i5, &e[1], &e[2], &e[3]);
                kernel2x(pe, pc, pf, pb, pi, pa, ph, pd, b1, c1, f4, c4, &e[0], &e[3], &e[1]);
                kernel2x(pe, pa, pb, pd, pc, pg, pf, ph, d0, a0, b1, a1, &e[2], &e[1], &e[0]);
                kernel2x(pe, pg, pd, ph, pa, pi, pb, pf, h5, g5, d0, g0, &e[3], &e[0], &e[2]);
            } else if (factor == 3) {
                kernel3x(pe, pi, ph, pf, pg, pc, pd, pb, f4, i4, h5, i5, &e[2], &e[5], &e[6], &e[7], &e[8]);
                kernel3x(pe, pc, pf, pb, pi, pa, ph, pd, b1, c1, f4, c4, &e[0], &e[1], &e[8], &e[5], &e[2]);
                kernel3x(pe, pa, pb, pd, pc, pg, pf, ph, d0, a0, b1, a1, &e[6], &e[3], &e[2], &e[1], &e[0]);
                kernel3x(pe, pg, pd, ph, pa, pi, pb, pf, h5, g5, d0, g0, &e[8], &e[7], &e[0], &e[3], &e[6]);
            } else {
                kernel4x(pe, pi, ph, pf, pg, pc, pd, pb, f4, i4, h5, i5, &e[15], &e[14], &e[11], &e[3], &e[7],
                         &e[10], &e[13], &e[12]);
                kernel4x(pe, pc, pf, pb, pi, pa, ph, pd, b1, c1, f4, c4, &e[3], &e[7], &e[2], &e[0], &e[1], &e[6],
                         &e[11], &e[15]);
                kernel4x(pe, pa, pb, pd, pc, pg, pf, ph, d0, a0, b1, a1, &e[0], &e[1], &e[4], &e[12], &e[8], &e[5],
                         &e[2], &e[3]);
                kernel4x(pe, pg, pd, ph, pa, pi, pb, pf, h5, g5, d0, g0, &e[12], &e[8], &e[13], &e[15], &e[14],
                         &e[9], &e[4], &e[0]);
            }
            uint32_t* o = out + (size_t)y * factor * ow + (size_t)x * factor;
            for (int r = 0; r < factor; r++)
                for (int c = 0; c < factor; c++) o[(size_t)r * ow + c] = e[r * factor + c];
        }
    }
}
