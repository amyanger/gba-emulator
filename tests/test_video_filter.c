#include "test_harness.h"
#include "common.h"
#include "frontend/super_xbr.h"
#include "frontend/video_filter.h"
#include "frontend/xbr.h"

#define PW 32
#define PH 24

static uint32_t s_pat[PW * PH];
static uint32_t s_big[PW * 4 * PH * 4];
static uint16_t s_fb[SCREEN_WIDTH * SCREEN_HEIGHT];

/* Diagonal line, disc, thick stroke and a dither patch: the shapes the scalers treat differently. */
static void make_pattern(void) {
    for (int y = 0; y < PH; y++) {
        for (int x = 0; x < PW; x++) {
            uint32_t c = 0xFF2850A0u;
            if (x - y == 4 || x - y == 5) c = 0xFFF8F8F8u;
            int dx = x - 22, dy = y - 12;
            if (dx * dx + dy * dy <= 36) c = 0xFFE8C020u;
            if ((x >= 3 && x <= 4 && y >= 12 && y <= 21) || (y >= 20 && y <= 21 && x >= 3 && x <= 10)) c = 0xFF101010u;
            if (x >= 26 && y >= 18 && ((x + y) & 1)) c = 0xFF60A060u;
            s_pat[y * PW + x] = c;
        }
    }
}

static uint32_t hash32(const uint32_t* px, int n) {
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < n; i++) {
        for (int b = 0; b < 32; b += 8) {
            h ^= (px[i] >> b) & 0xFF;
            h *= 0x01000193u;
        }
    }
    return h;
}

TEST(video_filter_names_round_trip) {
    for (int i = 0; i < VIDEO_FILTER_COUNT; i++) {
        VideoFilter f = VIDEO_FILTER_SHARP;
        ASSERT_TRUE(video_filter_from_name(video_filter_name((VideoFilter)i), &f));
        ASSERT_EQ(f, i);
    }
    VideoFilter f = VIDEO_FILTER_SOFT;
    ASSERT_TRUE(!video_filter_from_name("blurry", &f));
    ASSERT_TRUE(!video_filter_from_name(NULL, &f));
    ASSERT_EQ(f, VIDEO_FILTER_SOFT);
}

TEST(video_filter_scale_table) {
    /* k: game size in GBA pixels; Smooth picks the largest xBR factor dividing k. */
    static const int smooth_f[9] = {1, 1, 2, 3, 4, 4, 3, 4, 4};
    static const bool smooth_lin[9] = {false, false, false, false, false, true, false, true, false};
    for (int k = 0; k <= 8; k++) {
        VideoScale s = video_filter_scale(VIDEO_FILTER_SMOOTH, k);
        ASSERT_EQ(s.factor, smooth_f[k]);
        ASSERT_EQ(s.linear, smooth_lin[k]);
        s = video_filter_scale(VIDEO_FILTER_SHARP, k);
        ASSERT_EQ(s.factor, 1);
        ASSERT_TRUE(!s.linear);
        s = video_filter_scale(VIDEO_FILTER_SOFT, k);
        ASSERT_EQ(s.factor, k < 2 ? 1 : 2);
        ASSERT_EQ(s.linear, k > 2);
    }
}

TEST(video_color_plain_and_corrected) {
    ASSERT_EQ_HEX(video_color(0x0000, false), 0xFF000000u);
    ASSERT_EQ_HEX(video_color(0x7FFF, false), 0xFFFFFFFFu);
    ASSERT_EQ_HEX(video_color(0x001F, false), 0xFFFF0000u);
    ASSERT_EQ_HEX(video_color(0x8010, false), 0xFF840000u);   /* bit 15 ignored */
    ASSERT_EQ_HEX(video_color(0x0000, true), 0xFF000000u);
    /* Pokefan531 gba-color: white dims to 0.94 luminance, red bleeds into green and blue. */
    ASSERT_EQ_HEX(video_color(0x7FFF, true), 0xFFF8F8F8u);
    ASSERT_EQ_HEX(video_color(0x001F, true), 0xFFE36076u);
}

TEST(xbr_flat_image_stays_flat) {
    for (int i = 0; i < PW * PH; i++) s_pat[i] = 0xFF336699u;
    for (int f = 2; f <= 4; f++) {
        xbr_scale(s_pat, PW, PH, s_big, f);
        for (int i = 0; i < PW * PH * f * f; i++) ASSERT_EQ_HEX(s_big[i], 0xFF336699u);
    }
    super_xbr_scale2x(s_pat, PW, PH, s_big);
    for (int i = 0; i < PW * PH * 4; i++) ASSERT_EQ_HEX(s_big[i], 0xFF336699u);
}

/* Pinned from this port; on this pattern it matches xBRjs byte for byte (checked when ported). */
TEST(xbr_pattern_hashes) {
    make_pattern();
    static const uint32_t want[5] = {0, 0, 0xFBA51CA8u, 0x23A8BB4Du, 0x97C89F98u};
    for (int f = 2; f <= 4; f++) {
        xbr_scale(s_pat, PW, PH, s_big, f);
        ASSERT_EQ_HEX(hash32(s_big, PW * PH * f * f), want[f]);
    }
}

/* Super-xBR uses floats, so check its shape rather than pin bits: the anti-ringing clamp keeps
 * every output channel within the source pixels it can reach (two either way, over the passes). */
TEST(super_xbr_stays_within_neighbours) {
    make_pattern();
    super_xbr_scale2x(s_pat, PW, PH, s_big);
    int blended = 0;
    for (int y = 0; y < PH * 2; y++) {
        for (int x = 0; x < PW * 2; x++) {
            uint32_t o = s_big[y * PW * 2 + x];
            ASSERT_EQ_HEX(o >> 24, 0xFFu);
            int cx = x / 2, cy = y / 2;
            for (int sh = 0; sh < 24; sh += 8) {
                int lo = 255, hi = 0;
                for (int j = -2; j <= 2; j++) {
                    for (int i = -2; i <= 2; i++) {
                        int sx = cx + i < 0 ? 0 : cx + i >= PW ? PW - 1 : cx + i;
                        int sy = cy + j < 0 ? 0 : cy + j >= PH ? PH - 1 : cy + j;
                        int v = (int)((s_pat[sy * PW + sx] >> sh) & 0xFF);
                        if (v < lo) lo = v;
                        if (v > hi) hi = v;
                    }
                }
                int v = (int)((o >> sh) & 0xFF);
                ASSERT_TRUE(v >= lo && v <= hi);
            }
            if (o != s_pat[cy * PW + cx]) blended++;
        }
    }
    ASSERT_TRUE(blended > 0);
}

TEST(video_filter_run_skips_unchanged_frames) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) s_fb[i] = (uint16_t)(i * 37);
    VideoFrame fr;
    video_filter_invalidate();
    ASSERT_TRUE(video_filter_run(s_fb, VIDEO_FILTER_SMOOTH, false, 3, &fr));
    ASSERT_EQ(fr.w, SCREEN_WIDTH * 3);
    ASSERT_EQ(fr.h, SCREEN_HEIGHT * 3);
    ASSERT_TRUE(!video_filter_run(s_fb, VIDEO_FILTER_SMOOTH, false, 3, &fr));
    ASSERT_EQ(fr.w, SCREEN_WIDTH * 3);

    s_fb[100] ^= 1;
    ASSERT_TRUE(video_filter_run(s_fb, VIDEO_FILTER_SMOOTH, false, 3, &fr));
    ASSERT_TRUE(video_filter_run(s_fb, VIDEO_FILTER_SMOOTH, true, 3, &fr));    /* color correction */
    ASSERT_TRUE(video_filter_run(s_fb, VIDEO_FILTER_SMOOTH, true, 4, &fr));    /* new factor */
    ASSERT_EQ(fr.w, SCREEN_WIDTH * 4);

    /* Soft is 2x for both; only the stretch differs, and a cached frame must report it. */
    ASSERT_TRUE(video_filter_run(s_fb, VIDEO_FILTER_SOFT, true, 2, &fr));
    ASSERT_TRUE(!fr.linear);
    ASSERT_TRUE(!video_filter_run(s_fb, VIDEO_FILTER_SOFT, true, 4, &fr));
    ASSERT_TRUE(fr.linear);

    video_filter_invalidate();
    ASSERT_TRUE(video_filter_run(s_fb, VIDEO_FILTER_SOFT, true, 4, &fr));
}

TEST(video_filter_sharp_corrected_is_lut_only) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) s_fb[i] = (uint16_t)(i & 0x7FFF);
    VideoFrame fr;
    video_filter_invalidate();
    ASSERT_TRUE(!video_filter_active(VIDEO_FILTER_SHARP, false));
    ASSERT_TRUE(video_filter_active(VIDEO_FILTER_SHARP, true));
    video_filter_run(s_fb, VIDEO_FILTER_SHARP, true, 3, &fr);
    ASSERT_EQ(fr.w, SCREEN_WIDTH);
    ASSERT_TRUE(!fr.linear);
    ASSERT_EQ_HEX(fr.px[1234], video_color(1234, true));
}

void run_video_filter_tests(void) {
    TEST_SUITE("video_filter");
    RUN_TEST(video_filter_names_round_trip);
    RUN_TEST(video_filter_scale_table);
    RUN_TEST(video_color_plain_and_corrected);
    RUN_TEST(xbr_flat_image_stays_flat);
    RUN_TEST(xbr_pattern_hashes);
    RUN_TEST(super_xbr_stays_within_neighbours);
    RUN_TEST(video_filter_run_skips_unchanged_frames);
    RUN_TEST(video_filter_sharp_corrected_is_lut_only);
}
