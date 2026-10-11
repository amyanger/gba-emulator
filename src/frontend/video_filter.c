#include "frontend/video_filter.h"

#include "common.h"
#include "frontend/super_xbr.h"
#include "frontend/xbr.h"

#include <math.h>
#include <string.h>

static const char* const k_names[VIDEO_FILTER_COUNT] = {"sharp", "smooth", "soft"};
static const char* const k_labels[VIDEO_FILTER_COUNT] = {"Sharp", "Smooth", "Soft"};

static uint32_t s_lut[2][32768];
static bool s_lut_ready;

static uint32_t s_src[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint32_t s_out[SCREEN_WIDTH * VIDEO_MAX_FACTOR * SCREEN_HEIGHT * VIDEO_MAX_FACTOR];

/* What the buffers hold, to skip unchanged frames (pause redraws every frame). */
static uint16_t s_last_fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static VideoFilter s_last_filter;
static bool s_last_cc;
static int s_last_factor = -1;
static VideoFrame s_last_frame;

const char* video_filter_name(VideoFilter f) { return f < VIDEO_FILTER_COUNT ? k_names[f] : k_names[0]; }
const char* video_filter_label(VideoFilter f) { return f < VIDEO_FILTER_COUNT ? k_labels[f] : k_labels[0]; }

bool video_filter_from_name(const char* name, VideoFilter* out) {
    for (int i = 0; i < VIDEO_FILTER_COUNT; i++) {
        if (name && strcmp(name, k_names[i]) == 0) {
            *out = (VideoFilter)i;
            return true;
        }
    }
    return false;
}

VideoScale video_filter_scale(VideoFilter f, int k) {
    VideoScale s = {1, false};
    if (k < 2) return s;
    if (f == VIDEO_FILTER_SOFT) {
        s.factor = 2;
        s.linear = k != 2;
    } else if (f == VIDEO_FILTER_SMOOTH) {
        /* The largest xBR factor that divides k keeps the stretch an exact multiple. */
        s.factor = VIDEO_MAX_FACTOR;
        s.linear = true;
        for (int n = VIDEO_MAX_FACTOR; n >= 2; n--) {
            if (k % n == 0) {
                s.factor = n;
                s.linear = false;
                break;
            }
        }
    }
    return s;
}

static uint8_t expand5(uint32_t c) { return (uint8_t)((c << 3) | (c >> 2)); }

/* GBA LCD colors from Pokefan531's gba-color shader (public domain, libretro glsl-shaders
 * handheld/shaders/color/gba-color.glsl): LCD gamma 2.2 + 1.0 darken, luminance 0.94,
 * channel cross-talk matrix, display gamma 2.2. */
static uint32_t lcd_color(uint32_t r5, uint32_t g5, uint32_t b5) {
    double r = pow(expand5(r5) / 255.0, 3.2) * 0.94;
    double g = pow(expand5(g5) / 255.0, 3.2) * 0.94;
    double b = pow(expand5(b5) / 255.0, 3.2) * 0.94;
    double out[3] = {0.82 * r + 0.24 * g - 0.06 * b, 0.125 * r + 0.665 * g + 0.21 * b,
                     0.195 * r + 0.075 * g + 0.73 * b};
    uint32_t px = 0xFF000000u;
    for (int i = 0; i < 3; i++) {
        double v = out[i] < 0.0 ? 0.0 : out[i] > 1.0 ? 1.0 : out[i];
        px |= (uint32_t)(pow(v, 1.0 / 2.2) * 255.0 + 0.5) << (16 - 8 * i);
    }
    return px;
}

static void lut_build(void) {
    for (uint32_t c = 0; c < 32768; c++) {
        uint32_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
        s_lut[0][c] = 0xFF000000u | ((uint32_t)expand5(r) << 16) | ((uint32_t)expand5(g) << 8) | expand5(b);
        s_lut[1][c] = lcd_color(r, g, b);
    }
    s_lut_ready = true;
}

uint32_t video_color(uint16_t c, bool color_correct) {
    if (!s_lut_ready) lut_build();
    return s_lut[color_correct ? 1 : 0][c & 0x7FFF];
}

bool video_filter_active(VideoFilter f, bool color_correct) { return f != VIDEO_FILTER_SHARP || color_correct; }

void video_filter_invalidate(void) { s_last_factor = -1; }

bool video_filter_run(const uint16_t* fb, VideoFilter f, bool color_correct, int k, VideoFrame* out) {
    VideoScale sc = video_filter_scale(f, k);
    if (s_last_factor == sc.factor && s_last_filter == f && s_last_cc == color_correct &&
        memcmp(s_last_fb, fb, sizeof(s_last_fb)) == 0) {
        *out = s_last_frame;
        out->linear = sc.linear;   /* the same factor can be stretched either way */
        return false;
    }

    if (!s_lut_ready) lut_build();
    const uint32_t* lut = s_lut[color_correct ? 1 : 0];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) s_src[i] = lut[fb[i] & 0x7FFF];

    VideoFrame fr = {s_src, SCREEN_WIDTH, SCREEN_HEIGHT, sc.linear};
    if (sc.factor > 1) {
        if (f == VIDEO_FILTER_SOFT)
            super_xbr_scale2x(s_src, SCREEN_WIDTH, SCREEN_HEIGHT, s_out);
        else
            xbr_scale(s_src, SCREEN_WIDTH, SCREEN_HEIGHT, s_out, sc.factor);
        fr.px = s_out;
        fr.w = SCREEN_WIDTH * sc.factor;
        fr.h = SCREEN_HEIGHT * sc.factor;
    }

    memcpy(s_last_fb, fb, sizeof(s_last_fb));
    s_last_filter = f;
    s_last_cc = color_correct;
    s_last_factor = sc.factor;
    s_last_frame = fr;
    *out = fr;
    return true;
}
