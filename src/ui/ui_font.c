#include "ui/ui_font.h"
#include "ui/ui_stb.h"
#include "ui/font_inter.h"
#include <math.h>
#include <string.h>

#define GLYPH_SLOTS 2048
#define GLYPH_POOL_BYTES (2 * 1024 * 1024)
#define GLYPH_BUCKETS 256
#define SUBPIXEL_STEPS 4  /* horizontal glyph phases: 0, .25, .5, .75 px */

typedef struct {
    uint32_t codepoint;
    uint16_t size_q;      /* pixel size * 4 */
    uint8_t font, sub;    /* sub: subpixel phase, 0..SUBPIXEL_STEPS-1 */
    uint16_t next;        /* 1-based index of the next glyph in the bucket, 0 ends */
    int16_t w, h, xoff, yoff;
    uint32_t offset;      /* into s_pool */
} Glyph;

static stbtt_fontinfo s_fonts[UI_FONT_COUNT];
static bool s_ready;
static Glyph s_glyphs[GLYPH_SLOTS];
static uint16_t s_bucket[GLYPH_BUCKETS];   /* 1-based head index per hash bucket, 0 empty */
static uint32_t s_glyph_count;
static uint8_t s_pool[GLYPH_POOL_BYTES];
static uint32_t s_pool_used;

bool ui_font_init(void) {
    if (s_ready) return true;
    s_ready = stbtt_InitFont(&s_fonts[UI_FONT_REGULAR], k_inter_regular_ttf, 0) &&
              stbtt_InitFont(&s_fonts[UI_FONT_SEMIBOLD], k_inter_semibold_ttf, 0);
    return s_ready;
}

void ui_font_cache_clear(void) {
    memset(s_glyphs, 0, sizeof(s_glyphs));
    memset(s_bucket, 0, sizeof(s_bucket));
    s_glyph_count = 0;
    s_pool_used = 0;
}

uint32_t ui_font_cache_count(void) { return s_glyph_count; }

/* Decode one UTF-8 sequence; invalid bytes yield U+FFFD and advance by one. */
static uint32_t utf8_next(const char** s) {
    const unsigned char* p = (const unsigned char*)*s;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); n = 2; }
    else if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); n = 3;
    } else { cp = 0xFFFD; n = 1; }
    *s += n;
    return cp;
}

static float px_size(const UiCanvas* c, float size_pt) {
    float px = size_pt * c->scale;
    return px < 1.0f ? 1.0f : px;
}

/* Theme sizes come from CSS mockups, where font-size is the em size. */
static float font_scale(UiFont f, float px) {
    return stbtt_ScaleForMappingEmToPixels(&s_fonts[f], px);
}

static uint32_t glyph_hash(UiFont f, uint32_t cp, uint16_t size_q, uint8_t sub) {
    return (cp * 31u + size_q * 7u + sub * 3u + (uint32_t)f) % GLYPH_BUCKETS;
}

static const Glyph* glyph_get(UiFont f, uint32_t cp, float px, uint8_t sub) {
    uint16_t size_q = (uint16_t)(px * 4.0f + 0.5f);
    uint32_t h = glyph_hash(f, cp, size_q, sub);
    for (uint16_t i = s_bucket[h]; i; i = s_glyphs[i - 1].next) {
        const Glyph* g = &s_glyphs[i - 1];
        if (g->codepoint == cp && g->size_q == size_q && g->font == f && g->sub == sub) return g;
    }
    stbtt_fontinfo* fi = &s_fonts[f];
    float scale = font_scale(f, px), shift = (float)sub / SUBPIXEL_STEPS;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBoxSubpixel(fi, (int)cp, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
    uint32_t bytes = (uint32_t)((x1 - x0) * (y1 - y0));
    if (s_glyph_count >= GLYPH_SLOTS || bytes > GLYPH_POOL_BYTES - s_pool_used) ui_font_cache_clear();
    if (bytes > GLYPH_POOL_BYTES) return NULL;
    Glyph* g = &s_glyphs[s_glyph_count++];
    g->codepoint = cp; g->size_q = size_q; g->font = (uint8_t)f; g->sub = sub;
    g->w = (int16_t)(x1 - x0); g->h = (int16_t)(y1 - y0); g->xoff = (int16_t)x0; g->yoff = (int16_t)y0;
    g->offset = s_pool_used;
    g->next = s_bucket[h];
    s_bucket[h] = (uint16_t)s_glyph_count;
    if (bytes) {
        /* The v2 rasterizer has one unchecked allocation (the scanline buffer
         * for glyphs wider than 64 px). It is only safe because the arena is
         * always empty when a glyph starts, so reset before and after. */
        ui_stb_arena_reset();
        stbtt_MakeCodepointBitmapSubpixel(fi, s_pool + s_pool_used, g->w, g->h, g->w, scale, scale,
                                          shift, 0, (int)cp);
        ui_stb_arena_reset();
        s_pool_used += bytes;
    }
    return g;
}

float ui_text_ascent(const UiCanvas* c, UiFont f, float size_pt) {
    if (!ui_font_init()) return 0;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&s_fonts[f], &asc, &desc, &gap);
    float px = px_size(c, size_pt);
    float asc_px = floorf(asc * font_scale(f, px) + 0.5f);
    return asc_px / c->scale;
}

float ui_text_cap_middle(const UiCanvas* c, UiFont f, float size_pt) {
    if (!ui_font_init()) return 0;
    int x0, y0, x1, y1;
    if (!stbtt_GetCodepointBox(&s_fonts[f], 'H', &x0, &y0, &x1, &y1)) y1 = 0;
    return ui_text_ascent(c, f, size_pt) - y1 * font_scale(f, size_pt) * 0.5f;
}

/* Width in pixels of the first n codepoints (n < 0: all). */
static float width_px(UiFont f, float px, const char* s, int n) {
    stbtt_fontinfo* fi = &s_fonts[f];
    float scale = font_scale(f, px), w = 0;
    uint32_t prev = 0;
    while (*s && n != 0) {
        uint32_t cp = utf8_next(&s);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(fi, (int)cp, &adv, &lsb);
        if (prev) w += stbtt_GetCodepointKernAdvance(fi, (int)prev, (int)cp) * scale;
        w += adv * scale;
        prev = cp;
        if (n > 0) n--;
    }
    return w;
}

float ui_text_width(const UiCanvas* c, UiFont f, float size_pt, const char* utf8) {
    if (!ui_font_init()) return 0;
    return width_px(f, px_size(c, size_pt), utf8, -1) / c->scale;
}

static void draw_box(UiCanvas* c, int x, int y, int w, int h, uint32_t argb) {
    for (int i = 0; i < w; i++) { ui_blend_coverage(c, x + i, y, argb, 160); ui_blend_coverage(c, x + i, y + h - 1, argb, 160); }
    for (int j = 0; j < h; j++) { ui_blend_coverage(c, x, y + j, argb, 160); ui_blend_coverage(c, x + w - 1, y + j, argb, 160); }
}

static float draw_run(UiCanvas* c, UiFont f, float px, float pen_x, int baseline, uint32_t argb,
                      const char* s, int n) {
    stbtt_fontinfo* fi = &s_fonts[f];
    float scale = font_scale(f, px);
    uint32_t prev = 0;
    while (*s && n != 0) {
        uint32_t cp = utf8_next(&s);
        int adv, lsb;
        stbtt_GetCodepointHMetrics(fi, (int)cp, &adv, &lsb);
        if (prev) pen_x += stbtt_GetCodepointKernAdvance(fi, (int)prev, (int)cp) * scale;
        if (stbtt_FindGlyphIndex(fi, (int)cp) == 0 && cp != ' ') {
            int bw = (int)(px * 0.5f), bh = (int)(px * 0.7f);
            draw_box(c, (int)(pen_x + 0.5f), baseline - bh, bw, bh, argb);
            pen_x += px * 0.6f;
        } else {
            /* Whole pixels plus a quarter-pixel phase rasterized into the glyph,
             * so spacing stays even; the baseline stays on a whole pixel. */
            float fx = floorf(pen_x);
            int ix = (int)fx, sub = (int)((pen_x - fx) * SUBPIXEL_STEPS + 0.5f);
            if (sub == SUBPIXEL_STEPS) { ix++; sub = 0; }
            const Glyph* g = glyph_get(f, cp, px, (uint8_t)sub);
            if (g && g->w > 0) {
                int gx = ix + g->xoff, gy = baseline + g->yoff;
                const uint8_t* bmp = s_pool + g->offset;
                for (int j = 0; j < g->h; j++)
                    for (int i = 0; i < g->w; i++)
                        ui_blend_coverage(c, gx + i, gy + j, argb, bmp[j * g->w + i]);
            }
            pen_x += adv * scale;
        }
        prev = cp;
        if (n > 0) n--;
    }
    return pen_x;
}

float ui_text(UiCanvas* c, UiFont f, float size_pt, float x, float y, float max_w,
              UiAlign align, uint32_t argb, const char* utf8) {
    if (!ui_font_init() || !utf8) return 0;
    float px = px_size(c, size_pt);
    float full = width_px(f, px, utf8, -1);
    float limit = max_w > 0 ? max_w * c->scale : full;
    int count = -1;
    float width = full;
    const char* ell = "\xE2\x80\xA6";
    if (full > limit) {
        float ell_w = width_px(f, px, ell, -1);
        int total = 0;
        for (const char* s = utf8; *s; total++) utf8_next(&s);
        count = total;
        while (count > 0 && width_px(f, px, utf8, count) + ell_w > limit) count--;
        width = width_px(f, px, utf8, count) + ell_w;
    }
    float x_px = x * c->scale;
    if (align == UI_ALIGN_RIGHT) x_px -= width;
    else if (align == UI_ALIGN_CENTER) x_px -= width * 0.5f;
    int baseline = (int)floorf(y * c->scale + 0.5f) + (int)(ui_text_ascent(c, f, size_pt) * c->scale + 0.5f);
    float end = draw_run(c, f, px, x_px, baseline, argb, utf8, count);
    if (count >= 0) draw_run(c, f, px, end, baseline, argb, ell, -1);
    return width / c->scale;
}
