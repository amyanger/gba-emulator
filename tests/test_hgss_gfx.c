#include "test_harness.h"
#include "hgss/hgss_gfx.h"

static void w16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* One dictionary at `o` (TEX0-relative) with `count` entries of `unit` bytes.
 * Header 8 bytes (dummy, count, size, then the unknown block header u16 hsize=8, u16 size),
 * data block header at o + 8: u16 unit, u16 size. Entries follow at o + 12. Returns entry ptr. */
static uint8_t* dict(uint8_t* tex0, uint32_t o, uint8_t count, uint16_t unit) {
    uint8_t* d = tex0 + o;
    d[1] = count;
    w16(d + 6, 8);                /* data block starts at o + 8 */
    w16(d + 8, unit);
    w16(d + 10, (uint16_t)(4 + unit * count));
    return d + 12;
}

/* BTX0 with `nframes` 4bpp textures of size s x s (frame k: every pixel = k+1),
 * palettes: npal x 16 colours (palette p colour c = p*16 + c). Returns length. */
static uint32_t build_btx(uint8_t* b, uint32_t nframes, uint32_t s, uint32_t npal, bool reverse_order) {
    memset(b, 0, 0x8000);
    memcpy(b, "BTX0", 4);
    w32(b + 0x10, 0x14);
    uint8_t* t = b + 0x14;
    uint32_t tex_dict = 0x40, pal_dict = 0x100, tex_data = 0x200;
    uint32_t frame_bytes = s * s / 2;
    uint32_t pal_data = tex_data + nframes * frame_bytes;
    w16(t + 0x0E, (uint16_t)tex_dict);
    w32(t + 0x14, tex_data);
    w32(t + 0x34, pal_dict);
    w32(t + 0x38, pal_data);
    uint8_t* te = dict(t, tex_dict, (uint8_t)nframes, 8);
    uint16_t lg = (uint16_t)(s == 32 ? 2 : 3);
    for (uint32_t k = 0; k < nframes; k++) {
        uint32_t slot = reverse_order ? nframes - 1 - k : k;      /* dict order differs from offset order */
        uint16_t params = (uint16_t)((lg << 4) | (lg << 7) | (3u << 10) | (1u << 13));
        w16(te + 8 * slot, (uint16_t)((k * frame_bytes) >> 3));
        w16(te + 8 * slot + 2, params);
        memset(t + tex_data + k * frame_bytes, (int)(((k + 1) & 0xF) * 0x11), frame_bytes);
    }
    uint8_t* pe = dict(t, pal_dict, (uint8_t)npal, 4);
    for (uint32_t p = 0; p < npal; p++) {
        w16(pe + 4 * p, (uint16_t)((p * 32) >> 3));
        for (uint32_t c = 0; c < 16; c++) w16(t + pal_data + p * 32 + c * 2, (uint16_t)(p * 16 + c));
    }
    return 0x14 + pal_data + npal * 32;
}

TEST(follower_btx_decodes_frames_in_offset_order) {
    static uint8_t b[0x8000];
    static HgssFollowerGfx g;
    uint32_t len = build_btx(b, 8, 32, 2, true);
    ASSERT_TRUE(hgss_gfx_follower(b, len, &g));
    ASSERT_EQ(g.size, 32);
    ASSERT_EQ(g.px[0][0], 1);
    ASSERT_EQ(g.px[7][31 * 32 + 31], 8);
    ASSERT_EQ_HEX(g.pal[1][3], 19);
}

TEST(follower_btx_rejects_bad_input) {
    static uint8_t b[0x8000];
    static HgssFollowerGfx g;
    uint32_t len = build_btx(b, 7, 32, 2, false);       /* 7 frames: not a follower */
    ASSERT_TRUE(!hgss_gfx_follower(b, len, &g));
    len = build_btx(b, 8, 32, 2, false);
    ASSERT_TRUE(!hgss_gfx_follower(b, len - 40, &g));   /* palette cut off */
    b[0] = 'X';
    ASSERT_TRUE(!hgss_gfx_follower(b, len, &g));
}

TEST(emote_btx_decodes_two_frames) {
    static uint8_t b[0x8000];
    static HgssEmoteGfx e;
    uint32_t len = build_btx(b, 2, 32, 1, false);
    ASSERT_TRUE(hgss_gfx_emote(b, len, &e));
    ASSERT_EQ(e.px[1][5], 2);
    ASSERT_EQ_HEX(e.pal[15], 15);
}

TEST(frame_ncgr_nclr_decode) {
    static uint8_t ncgr[0x30 + 0x240], nclr[0x28 + 32];
    memset(ncgr, 0, sizeof(ncgr));
    memset(nclr, 0, sizeof(nclr));
    memcpy(ncgr, "RGCN", 4);
    memcpy(nclr, "RLCN", 4);
    ncgr[0x30 + 17 * 32 + 31] = 0x5A;   /* tile 17, last byte: left pixel 0xA, right 0x5 */
    w16(nclr + 0x28 + 2 * 15, 0x7FFF);
    static HgssFrameGfx f;
    ASSERT_TRUE(hgss_gfx_frame(ncgr, sizeof(ncgr), nclr, sizeof(nclr), &f));
    ASSERT_EQ(f.tile[17][62], 0xA);
    ASSERT_EQ(f.tile[17][63], 0x5);
    ASSERT_EQ_HEX(f.pal[15], 0x7FFF);
    ASSERT_TRUE(!hgss_gfx_frame(ncgr, 0x100, nclr, sizeof(nclr), &f));
}

TEST(font_pixel_order) {
    static uint8_t font[0x10 + 2 * 64 + 2];
    static uint8_t nclr[0x28 + 32];
    static HgssFont f;
    memset(font, 0, sizeof(font));
    memset(nclr, 0, sizeof(nclr));
    memcpy(nclr, "RLCN", 4);
    w32(font + 0, 0x10);           /* header size */
    w32(font + 4, 0x10 + 2 * 64);  /* width table start */
    w16(font + 8, 2);              /* glyph count */
    font[0x0A] = 16; font[0x0B] = 16; font[0x0C] = 2; font[0x0D] = 2;
    /* glyph 1, top-left sub-tile, row 0: byte0 = pixels 4..7, byte1 = pixels 0..3. */
    font[0x10 + 64 + 0] = 0xC0;    /* pixel 4 = 3 */
    font[0x10 + 64 + 1] = 0x40;    /* pixel 0 = 1 */
    font[0x10 + 2 * 64 + 1] = 7;   /* width of glyph 1 */
    ASSERT_TRUE(hgss_gfx_font(font, sizeof(font), nclr, sizeof(nclr), &f));
    ASSERT_EQ(f.count, 2);
    ASSERT_EQ(f.width[1], 7);
    ASSERT_EQ(hgss_font_pixel(&f, 1, 0, 0), 1);
    ASSERT_EQ(hgss_font_pixel(&f, 1, 4, 0), 3);
    ASSERT_EQ(hgss_font_pixel(&f, 1, 1, 0), 0);
    ASSERT_EQ(hgss_font_pixel(&f, 9, 0, 0), 0);    /* out of range glyph */
}

void run_hgss_gfx_tests(void) {
    RUN_TEST(follower_btx_decodes_frames_in_offset_order);
    RUN_TEST(follower_btx_rejects_bad_input);
    RUN_TEST(emote_btx_decodes_two_frames);
    RUN_TEST(frame_ncgr_nclr_decode);
    RUN_TEST(font_pixel_order);
}
