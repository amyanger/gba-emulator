#include "hgss/hgss_gfx.h"

#include <string.h>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* True when [off, off + n) lies inside a buffer of `len` bytes, without overflowing. */
static bool in_range(uint32_t off, uint32_t n, uint32_t len) {
    return off <= len && n <= len - off;
}

typedef struct { uint32_t off; uint16_t w, h; uint8_t fmt; } BtxTex;

/* Dictionary at TEX0-relative `o`: entry count and pointer to entries. */
static bool btx_dict(const uint8_t* t, uint32_t tlen, uint32_t o, uint32_t unit, uint8_t* count,
                     const uint8_t** entries) {
    if (!in_range(o, 8, tlen)) return false;
    *count = t[o + 1];
    uint32_t data = o + rd16(t + o + 6);
    if (!in_range(data, 4, tlen) || rd16(t + data) != unit) return false;
    if (!in_range(data + 4, (uint32_t)*count * unit, tlen)) return false;
    *entries = t + data + 4;
    return true;
}

/* Parses TEX0 texture and palette dictionaries. Textures are sorted by data offset. */
static bool btx_parse(const uint8_t* b, uint32_t len, BtxTex* tex, uint8_t max, uint8_t* ntex,
                      uint32_t* pal_off, uint8_t pal_max, uint8_t* npal, const uint8_t** t_out, uint32_t* tlen_out) {
    if (len < 0x18 || memcmp(b, "BTX0", 4) != 0) return false;
    uint32_t start = rd32(b + 0x10);
    if (!in_range(start, 0x3C, len)) return false;
    const uint8_t* t = b + start;
    uint32_t tlen = len - start;
    uint32_t tex_data = rd32(t + 0x14), pal_data = rd32(t + 0x38);
    const uint8_t* e;
    uint8_t n;
    if (!btx_dict(t, tlen, rd16(t + 0x0E), 8, &n, &e) || n > max) return false;
    if (tex_data > tlen) return false;
    for (uint8_t i = 0; i < n; i++) {
        uint16_t p = rd16(e + 8 * i + 2);
        tex[i].off = tex_data + ((uint32_t)rd16(e + 8 * i) << 3);
        tex[i].w = (uint16_t)(8u << ((p >> 4) & 7));
        tex[i].h = (uint16_t)(8u << ((p >> 7) & 7));
        tex[i].fmt = (uint8_t)((p >> 10) & 7);
        if (tex[i].fmt != 3 || tex[i].off > tlen || !in_range(tex[i].off, (uint32_t)tex[i].w * tex[i].h / 2, tlen))
            return false;
    }
    for (uint8_t i = 1; i < n; i++)          /* insertion sort by offset */
        for (uint8_t j = i; j > 0 && tex[j - 1].off > tex[j].off; j--) {
            BtxTex x = tex[j];
            tex[j] = tex[j - 1];
            tex[j - 1] = x;
        }
    *ntex = n;
    if (!btx_dict(t, tlen, rd32(t + 0x34), 4, &n, &e) || n > pal_max) return false;
    if (pal_data > tlen) return false;
    for (uint8_t i = 0; i < n; i++) {
        pal_off[i] = pal_data + ((uint32_t)rd16(e + 4 * i) << 3);
        if (!in_range(pal_off[i], 32, tlen)) return false;
    }
    *npal = n;
    *t_out = t;
    *tlen_out = tlen;
    return true;
}

/* 4bpp to one palette index per byte. Low nibble is the first pixel. */
static void unpack4(const uint8_t* src, uint32_t pixels, uint8_t* dst) {
    for (uint32_t i = 0; i < pixels; i += 2) {
        dst[i] = src[i / 2] & 0x0F;
        dst[i + 1] = src[i / 2] >> 4;
    }
}

static void read_pal(const uint8_t* src, uint16_t* dst) {
    for (int c = 0; c < 16; c++) dst[c] = (uint16_t)(rd16(src + 2 * c) & 0x7FFF);
}

bool hgss_gfx_follower(const uint8_t* btx, uint32_t len, HgssFollowerGfx* out) {
    BtxTex tex[HGSS_FOLLOW_FRAMES];
    uint32_t pal[2];
    uint8_t nt, np;
    const uint8_t* t;
    uint32_t tlen;
    if (!btx_parse(btx, len, tex, HGSS_FOLLOW_FRAMES, &nt, pal, 2, &np, &t, &tlen)) return false;
    if (nt != HGSS_FOLLOW_FRAMES || np < 1) return false;
    uint16_t s = tex[0].w;
    if ((s != 32 && s != 64) || tex[0].h != s) return false;
    for (int k = 0; k < HGSS_FOLLOW_FRAMES; k++)
        if (tex[k].w != s || tex[k].h != s) return false;
    memset(out, 0, sizeof(*out));
    out->size = (uint8_t)s;
    for (int k = 0; k < HGSS_FOLLOW_FRAMES; k++)
        unpack4(t + tex[k].off, (uint32_t)s * s, out->px[k]);
    read_pal(t + pal[0], out->pal[0]);
    read_pal(t + pal[np > 1 ? 1 : 0], out->pal[1]);
    return true;
}

bool hgss_gfx_emote(const uint8_t* btx, uint32_t len, HgssEmoteGfx* out) {
    BtxTex tex[2];
    uint32_t pal[1];
    uint8_t nt, np;
    const uint8_t* t;
    uint32_t tlen;
    if (!btx_parse(btx, len, tex, 2, &nt, pal, 1, &np, &t, &tlen) || nt < 1 || np != 1) return false;
    for (uint8_t k = 0; k < nt; k++)
        if (tex[k].w != 32 || tex[k].h != 32) return false;
    unpack4(t + tex[0].off, 32 * 32, out->px[0]);
    unpack4(t + tex[nt - 1].off, 32 * 32, out->px[1]);   /* second frame, or the only one */
    read_pal(t + pal[0], out->pal);
    return true;
}

bool hgss_gfx_frame(const uint8_t* ncgr, uint32_t ncgr_len, const uint8_t* nclr, uint32_t nclr_len,
                    HgssFrameGfx* out) {
    if (ncgr_len < 0x30 + 18 * 32 || nclr_len < 0x28 + 32) return false;
    if (memcmp(ncgr, "RGCN", 4) != 0 || memcmp(nclr, "RLCN", 4) != 0) return false;
    for (int i = 0; i < 18; i++) unpack4(ncgr + 0x30 + 32 * i, 64, out->tile[i]);
    read_pal(nclr + 0x28, out->pal);
    return true;
}

bool hgss_gfx_font(const uint8_t* font, uint32_t len, const uint8_t* nclr, uint32_t nclr_len, HgssFont* out) {
    if (len < 0x10 || nclr_len < 0x28 + 32 || memcmp(nclr, "RLCN", 4) != 0) return false;
    uint32_t hdr = rd32(font), wstart = rd32(font + 4);
    uint16_t count = rd16(font + 8);
    if (count == 0 || count > HGSS_FONT_GLYPHS || font[0x0C] != 2 || font[0x0D] != 2) return false;
    if (!in_range(hdr, (uint32_t)count * 64, len) || !in_range(wstart, count, len)) return false;
    memset(out, 0, sizeof(*out));
    out->count = count;
    memcpy(out->glyph, font + hdr, (size_t)count * 64);
    memcpy(out->width, font + wstart, count);
    read_pal(nclr + 0x28, out->pal);
    return true;
}

uint8_t hgss_font_pixel(const HgssFont* f, uint16_t glyph, uint32_t x, uint32_t y) {
    if (glyph >= f->count || x > 15 || y > 15) return 0;
    const uint8_t* g = f->glyph[glyph] + 16 * ((y / 8) * 2 + x / 8);
    uint32_t row = y % 8, col = x % 8;
    uint8_t byte = g[2 * row + (col < 4 ? 1 : 0)];
    return (uint8_t)((byte >> (6 - 2 * (col % 4))) & 3);
}
