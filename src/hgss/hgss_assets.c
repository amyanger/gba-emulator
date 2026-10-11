#include "hgss/hgss_assets.h"

#include <stdio.h>
#include <string.h>

#include "hgss/hgss_model_lut.h"

#define MMODEL_BASE      297
#define MSG_MEMBER       265
#define FONT_MEMBER      1
#define FONT_PAL_MEMBER  8
#define FRAME_NCGR       2
#define FRAME_NCLR       26
#define EMOTE_BASE       2
#define LOCAL_RULES      30
#define GLOBAL_RULES     70
#define GLYPH_FALLBACK   427

/* Scratch for member reads. s_pal holds the second file of a pair (NCLR). */
static uint8_t s_buf[0x10000];
static uint8_t s_pal[0x4000];

static void set_error(HgssAssets* a, const char* msg) {
    snprintf(a->error, sizeof(a->error), "%s", msg);
}

bool hgss_assets_open(HgssAssets* a, const char* path) {
    memset(a, 0, sizeof(*a));
    FILE* probe = fopen(path, "rb");
    if (!probe) {
        set_error(a, "File not found");
        return false;
    }
    fclose(probe);
    if (!nds_open(&a->rom, path)) {
        set_error(a, "Not a HeartGold ROM");
        return false;
    }
    bool hg = strcmp(a->rom.code, "IPKE") == 0;
    if (!hg && strcmp(a->rom.code, "IPGE") != 0) {
        set_error(a, "Not a HeartGold ROM");
        hgss_assets_close(a);
        return false;
    }

    const struct { const char* path; NdsSpan* span; } files[] = {
        {"a/0/8/1", &a->mmodel},  {"a/1/4/1", &a->tp_param}, {"a/0/2/7", &a->msg},
        {"a/2/2/0", &a->rules},   {"a/2/2/1", &a->scripts},  {"a/2/2/2", &a->anims},
        {"a/2/3/1", &a->species_class}, {"a/0/3/8", &a->frames}, {"a/0/1/6", &a->fonts},
        {"a/1/0/3", &a->emotes},
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        if (!nds_find(&a->rom, files[i].path, files[i].span)) {
            /* SoulSilver is only accepted when it has the same layout. */
            set_error(a, hg ? "ROM data unreadable" : "Not a HeartGold ROM");
            hgss_assets_close(a);
            return false;
        }
    }

    uint32_t len = 0, pal_len = 0;
    bool ok = narc_read(&a->rom, a->rules, 0, s_buf, sizeof(s_buf), &len) && len >= sizeof(a->global_rules);
    if (ok) memcpy(a->global_rules, s_buf, sizeof(a->global_rules));
    ok = ok && narc_read(&a->rom, a->species_class, 0, s_buf, sizeof(s_buf), &len) &&
         len >= sizeof(a->species_class_tbl);
    if (ok) memcpy(a->species_class_tbl, s_buf, sizeof(a->species_class_tbl));
    ok = ok && narc_read(&a->rom, a->frames, FRAME_NCGR, s_buf, sizeof(s_buf), &len) &&
         narc_read(&a->rom, a->frames, FRAME_NCLR, s_pal, sizeof(s_pal), &pal_len) &&
         hgss_gfx_frame(s_buf, len, s_pal, pal_len, &a->frame);
    ok = ok && narc_read(&a->rom, a->fonts, FONT_MEMBER, s_buf, sizeof(s_buf), &len) &&
         narc_read(&a->rom, a->fonts, FONT_PAL_MEMBER, s_pal, sizeof(s_pal), &pal_len) &&
         hgss_gfx_font(s_buf, len, s_pal, pal_len, &a->font);
    for (uint32_t e = 0; ok && e < HGSS_EMOTES; e++)
        ok = narc_read(&a->rom, a->emotes, EMOTE_BASE + e, s_buf, sizeof(s_buf), &len) &&
             hgss_gfx_emote(s_buf, len, &a->emote[e]);
    ok = ok && narc_read(&a->rom, a->msg, MSG_MEMBER, a->msg_bank, sizeof(a->msg_bank), &a->msg_len);
    if (!ok) {
        set_error(a, "ROM data unreadable");
        hgss_assets_close(a);
        return false;
    }
    a->ok = true;
    return true;
}

void hgss_assets_close(HgssAssets* a) {
    nds_close(&a->rom);
    a->ok = false;
}

bool hgss_assets_follower(HgssAssets* a, uint16_t national, bool female, uint8_t form, HgssFollowerGfx* out,
                          bool* too_tall) {
    if (!a->ok || national == 0 || national > 386) return false;
    uint32_t offset = k_hgss_model_offset[national];
    uint32_t member = MMODEL_BASE + offset;
    if (female && k_hgss_female_model[national])
        member += 1;
    else if (national == 201 && form <= 27)   /* Unown letter */
        member += form;

    uint32_t len = 0;
    if (!narc_read(&a->rom, a->tp_param, offset, s_buf, sizeof(s_buf), &len) || len < 2) return false;
    *too_tall = s_buf[1] != 0;
    return narc_read(&a->rom, a->mmodel, member, s_buf, sizeof(s_buf), &len) &&
           hgss_gfx_follower(s_buf, len, out);
}

void hgss_assets_rules(HgssAssets* a, uint16_t mapsec, uint8_t rules[HGSS_RULES][HGSS_RULE_SIZE]) {
    memset(rules, 0, (size_t)LOCAL_RULES * HGSS_RULE_SIZE);
    uint32_t len = 0;
    if (a->ok && mapsec != 0 && narc_read(&a->rom, a->rules, (uint32_t)mapsec + 1, s_buf, sizeof(s_buf), &len)) {
        uint32_t take = len < LOCAL_RULES * HGSS_RULE_SIZE ? len : LOCAL_RULES * HGSS_RULE_SIZE;
        memcpy(rules, s_buf, take);
    }
    memcpy(rules[LOCAL_RULES], a->global_rules, sizeof(a->global_rules));
}

bool hgss_assets_script(HgssAssets* a, uint16_t id, HgssTalkScript* out) {
    uint32_t len = 0;
    return a->ok && id != 0 && narc_read(&a->rom, a->scripts, (uint32_t)id - 1, s_buf, sizeof(s_buf), &len) &&
           hgss_script_parse(s_buf, len, out);
}

uint8_t hgss_assets_anim(HgssAssets* a, uint16_t id, HgssAnimFrame out[HGSS_ANIM_FRAMES]) {
    uint32_t len = 0;
    if (!a->ok || id == 0 || !narc_read(&a->rom, a->anims, (uint32_t)id - 1, s_buf, sizeof(s_buf), &len))
        return 0;
    return hgss_anim_parse(s_buf, len, out);
}

bool hgss_assets_line(const HgssAssets* a, uint16_t msg1, uint16_t* out, uint32_t cap, uint32_t* n) {
    return a->ok && msg1 != 0 && hgss_msg_decode(a->msg_bank, a->msg_len, (uint32_t)msg1 - 1, out, cap, n);
}

/* ---- Text layout ---- */

static uint16_t glyph_of(const HgssFont* f, uint16_t code) {
    if (code == 0 || code > f->count) {
        uint16_t last = f->count ? (uint16_t)(f->count - 1) : 0;
        return GLYPH_FALLBACK < last ? GLYPH_FALLBACK : last;
    }
    return (uint16_t)(code - 1);
}

static uint32_t char_w(const HgssFont* f, uint16_t code) {
    return f->width[glyph_of(f, code)];
}

static bool is_break(uint16_t c) {
    return c == HGSS_CH_SPACE || c == HGSS_CH_NEWLINE || c == HGSS_CH_SCROLL || c == HGSS_CH_NEWBOX ||
           c == HGSS_CH_END || c == HGSS_CH_CTRL;
}

typedef struct {
    HgssTextPage* pages;
    uint8_t max, page, line;
    uint32_t x;
    bool full;   /* out of pages: drop the rest */
} Layout;

static bool page_empty(const Layout* l) {
    return l->pages[l->page].len[0] == 0 && l->pages[l->page].len[1] == 0;
}

static void next_page(Layout* l) {
    if (l->full || page_empty(l)) {
        l->line = 0;
        l->x = 0;
        return;
    }
    if (l->page + 1 >= l->max) {
        l->full = true;
        return;
    }
    l->page++;
    memset(&l->pages[l->page], 0, sizeof(HgssTextPage));
    l->line = 0;
    l->x = 0;
}

static void next_line(Layout* l) {
    l->x = 0;
    if (++l->line >= HGSS_TEXT_LINES) next_page(l);
}

static void put(Layout* l, const HgssFont* f, uint16_t c) {
    if (l->full) return;   /* line may be past the last one */
    uint32_t w = char_w(f, c);
    HgssTextPage* p = &l->pages[l->page];
    if (l->x + w > HGSS_TEXT_W || p->len[l->line] >= sizeof(p->line[0]) / sizeof(p->line[0][0])) {
        next_line(l);
        p = &l->pages[l->page];
    }
    if (l->full) return;
    p->line[l->line][p->len[l->line]++] = c;
    l->x += w;
}

uint8_t hgss_text_layout(const HgssFont* f, const uint16_t* text, uint32_t n, HgssTextPage* pages, uint8_t max) {
    if (max == 0) return 0;
    Layout l = {pages, max, 0, 0, 0, false};
    memset(&pages[0], 0, sizeof(HgssTextPage));
    bool space = false;   /* a space waits until the next word decides the line */
    uint32_t cap = sizeof(pages[0].line[0]) / sizeof(pages[0].line[0][0]);

    for (uint32_t i = 0; i < n && !l.full;) {
        uint16_t c = text[i];
        if (c == HGSS_CH_END) break;
        if (c == HGSS_CH_CTRL) {   /* cmd, argc, args: not expected after hgss_msg_expand */
            i += 3 + (i + 2 < n ? text[i + 2] : 0);
            continue;
        }
        if (c == HGSS_CH_SPACE) {
            if (space) put(&l, f, HGSS_CH_SPACE);   /* runs of spaces: keep all but the last */
            space = true;
            i++;
            continue;
        }
        if (c == HGSS_CH_NEWLINE || c == HGSS_CH_SCROLL || c == HGSS_CH_NEWBOX) {
            space = false;
            if (c == HGSS_CH_NEWLINE) next_line(&l);
            else next_page(&l);
            i++;
            continue;
        }
        uint32_t j = i, w = 0;
        while (j < n && !is_break(text[j])) w += char_w(f, text[j++]);
        uint32_t sw = space ? char_w(f, HGSS_CH_SPACE) : 0;
        uint32_t used = pages[l.page].len[l.line];
        if (l.x > 0 && (l.x + sw + w > HGSS_TEXT_W || used + (space ? 1 : 0) + (j - i) > cap)) {
            next_line(&l);   /* the pending space is dropped at the break */
        } else if (space) {
            put(&l, f, HGSS_CH_SPACE);
        }
        space = false;
        for (; i < j; i++) put(&l, f, text[i]);   /* breaks inside a word only if it alone is too wide */
    }
    return page_empty(&l) ? l.page : (uint8_t)(l.page + 1);
}

/* ---- Text box drawing ---- */

#define FB_W     240
#define WIN_X    2
#define WIN_Y    15
#define WIN_W    25
#define WIN_H    4
#define TEXT_X   16
#define TEXT_Y   120
#define LINE_H   16
#define ARROW_X  222
#define ARROW_Y  146

static void draw_tile(uint16_t* fb, const HgssFrameGfx* fr, uint32_t t, uint32_t col, uint32_t row) {
    for (uint32_t y = 0; y < 8; y++)
        for (uint32_t x = 0; x < 8; x++) {
            uint8_t v = fr->tile[t][y * 8 + x];
            if (v) fb[(row * 8 + y) * FB_W + col * 8 + x] = fr->pal[v];
        }
}

static void fill_tile(uint16_t* fb, uint16_t color, uint32_t col, uint32_t row) {
    for (uint32_t y = 0; y < 8; y++)
        for (uint32_t x = 0; x < 8; x++) fb[(row * 8 + y) * FB_W + col * 8 + x] = color;
}

void hgss_text_draw(uint16_t* fb, const HgssFrameGfx* fr, const HgssFont* f, const HgssTextPage* p,
                    uint32_t chars, bool arrow) {
    /* 9-slice frame around the text window; tile 8 is unused, interior is colour 15. */
    const uint32_t left = WIN_X - 2, right = WIN_X + WIN_W;
    for (uint32_t row = WIN_Y - 1; row <= WIN_Y + WIN_H; row++) {
        bool top = row == WIN_Y - 1, bottom = row == WIN_Y + WIN_H;
        uint32_t base = top ? 0 : bottom ? 12 : 6;
        draw_tile(fb, fr, base, left, row);
        draw_tile(fb, fr, base + 1, left + 1, row);
        for (uint32_t col = WIN_X; col < right; col++) {
            if (top || bottom)
                draw_tile(fb, fr, base + 2, col, row);
            else
                fill_tile(fb, fr->pal[15], col, row);
        }
        uint32_t rb = top ? 3 : bottom ? 15 : 9;
        for (uint32_t k = 0; k < 3; k++) draw_tile(fb, fr, rb + k, right + k, row);
    }

    const uint32_t text_right = right * 8;
    for (uint32_t li = 0; li < HGSS_TEXT_LINES && chars > 0; li++) {
        uint32_t x0 = TEXT_X, y0 = TEXT_Y + li * LINE_H;
        for (uint32_t k = 0; k < p->len[li] && chars > 0; k++, chars--) {
            uint16_t g = glyph_of(f, p->line[li][k]);
            for (uint32_t y = 0; y < 16; y++)
                for (uint32_t x = 0; x < 16 && x0 + x < text_right; x++) {
                    uint8_t v = hgss_font_pixel(f, g, x, y);
                    if (v == 1 || v == 2) fb[(y0 + y) * FB_W + x0 + x] = f->pal[v];
                }
            x0 += f->width[g];
            if (x0 >= text_right) break;
        }
    }

    if (arrow)
        for (uint32_t y = 0; y < 4; y++)
            for (uint32_t x = y; x < 7 - y; x++) fb[(ARROW_Y + y) * FB_W + ARROW_X + x] = f->pal[1];
}
