#include "frontend/trainer_card_draw.h"
#include "frontend/game_panel.h"
#include "frontend/badge_draw.h"
#include "screenshot/screenshot.h"
#include "common.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <SDL2/SDL_keyboard.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EMPTY_TILE 0xFF14171Fu
#define ACE_GLOW   0xFF1C2A4Au
#define LEFT_W     690.0f

/* GamePartyMon.ivs is HP, Atk, Def, Spe, SpA, SpD; the card shows HP, Atk, Def, SpA, SpD, Spe. */
static const uint8_t k_stat_order[6] = {0, 1, 2, 4, 5, 3};
static const char* const k_stat_label[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

bool card_style_from_name(const char* s, CardStyle* out) {
    if (!s) return false;
    if (strcmp(s, "grid") == 0) { *out = CARD_STYLE_GRID; return true; }
    if (strcmp(s, "ace") == 0) { *out = CARD_STYLE_ACE; return true; }
    return false;
}

void trainer_card_path(const char* rom_path, time_t now, char* out, size_t out_size) {
    if (out_size == 0) return;
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
    snprintf(out, out_size, "%s.%s.card.png", rom_path, stamp);
}

const char* trainer_card_display_name(const GamePartyMon* mon) {
    if (mon->egg) return "Egg";
    for (const char* p = mon->nickname; *p; p++)
        if (*p != '?' && *p != ' ') return mon->nickname;
    return mon->species;
}

static bool slot_ok(const TrainerCardData* d, int i) {
    return i >= 0 && i < 6 && i < d->count && d->party[i].present && !d->party[i].bad;
}

static void blit_sprite(UiCanvas* c, float x, float y, float size, const uint32_t* px) {
    int ox = ui_px(c, x), oy = ui_px(c, y), n = ui_px(c, size);
    for (int dy = 0; dy < n; dy++) {
        int py = oy + dy;
        if (py < c->cy0 || py >= c->cy1) continue;
        const uint32_t* row = px + (dy * MON_PIC_W / n) * MON_PIC_W;
        for (int dx = 0; dx < n; dx++) {
            int pxx = ox + dx;
            if (pxx < c->cx0 || pxx >= c->cx1) continue;
            uint32_t s = row[dx * MON_PIC_W / n];
            if ((s >> 24) == 0) continue;
            c->px[py * c->w + pxx] = s | 0xFF000000u;
        }
    }
}

static void placeholder(UiCanvas* c, float x, float y, float size) {
    float s = size * 0.7f;
    ui_fill_round_rect(c, (UiRect){x + (size - s) / 2, y + (size - s) / 2, s, s}, s / 2, UI_DIVIDER);
}

static void draw_pic(UiCanvas* c, float x, float y, float size, const uint32_t pics[6][MON_PIC_PIXELS],
                     const bool pic_ok[6], int i) {
    if (pic_ok[i]) blit_sprite(c, x, y, size, pics[i]);
    else placeholder(c, x, y, size);
}

/* Badges centred on a row at cy: the ROM art when there is some (size 16 or 32 keeps the 1x card's
 * pixels whole), else coloured dots. */
static void badge_row(UiCanvas* c, float x, float cy, const TrainerCardData* d, float size,
                      const BadgeArt* art) {
    float y = cy - size / 2;
    for (int i = 0; i < 8; i++) {
        float bx = x + (float)i * (size + 6.0f);
        if (art && art->ok)
            badge_draw(c, bx, y, size, art->px[i], (GameRegion)d->region, i, (d->badges & (1u << i)) != 0);
        else if (d->badges & (1u << i))
            ui_fill_round_rect(c, (UiRect){bx, y, size, size}, size / 2, badge_color(d->region, i));
        else
            ui_ring(c, bx + size / 2, y + size / 2, size / 2 - 1.0f, 2.0f, UI_DIVIDER);
    }
}

static float badge_row_width(float size) { return 8.0f * size + 7.0f * 6.0f; }

/* Type pills then SHINY. Returns the width drawn. */
static float type_pills(UiCanvas* c, float x, float y, const GamePartyMon* m) {
    if (m->egg) return 0;
    float cx = x;
    for (int t = 0; t < 2; t++) {
        uint8_t id = m->type_ids[t];
        if (id == 0xFF || (t == 1 && id == m->type_ids[0])) continue;
        const char* name = game_panel_type_name(id);
        if (!name) continue;
        cx += ui_pill(c, cx, y, name, ui_type_color(id)) + 4.0f;
    }
    if (m->shiny) cx += ui_pill(c, cx, y, "SHINY", UI_MID) + 4.0f;
    return cx - x;
}

static void footer(UiCanvas* c, float right) {
    ui_text(c, UI_FONT_REGULAR, 12.0f, right, (float)CARD_H - 32.0f, 0, UI_ALIGN_RIGHT, UI_FAINT,
            "GBA Emulator \xC2\xB7 Pok\xC3\xA9mon Companion");
}

static void play_time(const TrainerCardData* d, char* out, size_t n) {
    snprintf(out, n, "%u:%02u", (unsigned)(d->play_frames / 216000u), (unsigned)((d->play_frames / 3600u) % 60u));
}

static void join2(char* out, size_t n, const char* a, const char* b) {
    out[0] = 0;
    if (a[0] && b[0]) snprintf(out, n, "%s \xC2\xB7 %s", a, b);
    else if (a[0]) snprintf(out, n, "%s", a);
    else snprintf(out, n, "%s", b);
}

static void stat_block(UiCanvas* c, float right, float* x_out, const char* label, const char* value) {
    float w = ui_text_width(c, UI_FONT_SEMIBOLD, 22.0f, value);
    float lw = ui_text_width(c, UI_FONT_REGULAR, 11.0f, label);
    if (lw > w) w = lw;
    float x = right - w;
    ui_text(c, UI_FONT_REGULAR, 11.0f, right, 56.0f, 0, UI_ALIGN_RIGHT, UI_DIM, label);
    ui_text(c, UI_FONT_SEMIBOLD, 22.0f, right, 72.0f, 0, UI_ALIGN_RIGHT, UI_TEXT, value);
    *x_out = x;
}

/* ---- Team grid ---- */

static void draw_grid(UiCanvas* c, const TrainerCardData* d, const uint32_t pics[6][MON_PIC_PIXELS],
                      const bool pic_ok[6], const BadgeArt* art) {
    ui_canvas_clear(c, UI_BG);
    char buf[96];

    ui_text(c, UI_FONT_REGULAR, 11.0f, 40.0f, 34.0f, 0, UI_ALIGN_LEFT, UI_DIM, "TRAINER");
    ui_text(c, UI_FONT_SEMIBOLD, 34.0f, 40.0f, 48.0f, 460.0f, UI_ALIGN_LEFT, UI_TEXT, d->name);
    snprintf(buf, sizeof buf, "ID No. %05u \xC2\xB7 %s", (unsigned)d->public_id,
             d->game_name ? d->game_name : "Pok\xC3\xA9mon Emerald");
    ui_text(c, UI_FONT_REGULAR, 13.0f, 40.0f, 94.0f, 0, UI_ALIGN_LEFT, UI_MUTED, buf);

    float right = (float)CARD_W - 40.0f, x;
    ui_text(c, UI_FONT_REGULAR, 11.0f, right, 56.0f, 0, UI_ALIGN_RIGHT, UI_DIM, "BADGES");
    float bs = art && art->ok ? 32.0f : 18.0f;
    badge_row(c, right - badge_row_width(bs), 91.0f, d, bs, art);
    right -= badge_row_width(bs) + 34.0f;
    snprintf(buf, sizeof buf, "%u", (unsigned)d->dex_owned);
    stat_block(c, right, &x, "POKEDEX", buf);
    right = x - 34.0f;
    char pt[16];
    play_time(d, pt, sizeof pt);
    stat_block(c, right, &x, "PLAY TIME", pt);

    ui_hline(c, 40.0f, 122.0f, (float)CARD_W - 80.0f, UI_DIVIDER);

    const float gap = 12.0f, top = 146.0f, th = 190.0f, spr = 128.0f;
    float tw = ((float)CARD_W - 80.0f - 2 * gap) / 3.0f;
    for (int i = 0; i < 6; i++) {
        float tx = 40.0f + (float)(i % 3) * (tw + gap), ty = top + (float)(i / 3) * (th + gap);
        if (!slot_ok(d, i)) {
            ui_fill_round_rect(c, (UiRect){tx, ty, tw, th}, 10.0f, EMPTY_TILE);
            continue;
        }
        const GamePartyMon* m = &d->party[i];
        ui_fill_round_rect(c, (UiRect){tx, ty, tw, th}, 10.0f, UI_SURFACE);
        draw_pic(c, tx + 14.0f, ty + (th - spr) / 2, spr, pics, pic_ok, i);
        float x0 = tx + 14.0f + spr + 12.0f, w = tw - (x0 - tx) - 12.0f;
        const char* name = trainer_card_display_name(m);
        if (m->egg) {
            ui_text(c, UI_FONT_SEMIBOLD, 16.0f, x0, ty + th / 2 - 11.0f, w, UI_ALIGN_LEFT, UI_TEXT, name);
            continue;
        }
        float y0 = ty + (th - 90.0f) / 2;
        snprintf(buf, sizeof buf, "Lv %u", (unsigned)m->level);
        float lvw = ui_text_width(c, UI_FONT_REGULAR, 13.0f, buf);
        float nw = ui_text(c, UI_FONT_SEMIBOLD, 16.0f, x0, y0, w - lvw - 6.0f, UI_ALIGN_LEFT, UI_TEXT, name);
        ui_text(c, UI_FONT_REGULAR, 13.0f, x0 + nw + 6.0f, y0 + 3.0f, 0, UI_ALIGN_LEFT, UI_MUTED, buf);
        type_pills(c, x0, y0 + 28.0f, m);
        join2(buf, sizeof buf, m->nature, m->item);
        ui_text(c, UI_FONT_REGULAR, 12.0f, x0, y0 + 53.0f, w, UI_ALIGN_LEFT, UI_MUTED, buf);
        snprintf(buf, sizeof buf, "IV %u\xC2\xB7%u\xC2\xB7%u\xC2\xB7%u\xC2\xB7%u\xC2\xB7%u",
                 m->ivs[k_stat_order[0]], m->ivs[k_stat_order[1]], m->ivs[k_stat_order[2]],
                 m->ivs[k_stat_order[3]], m->ivs[k_stat_order[4]], m->ivs[k_stat_order[5]]);
        ui_text(c, UI_FONT_REGULAR, 11.0f, x0, y0 + 74.0f, w, UI_ALIGN_LEFT, UI_GOOD, buf);
    }
    footer(c, (float)CARD_W - 40.0f);
}

/* ---- Ace showcase ---- */

static uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << sh;
    }
    return out;
}

static void glow(UiCanvas* c, float cx, float cy, float radius) {
    int x1 = ui_px(c, LEFT_W) < c->w ? ui_px(c, LEFT_W) : c->w;
    for (int y = 0; y < c->h; y++)
        for (int x = 0; x < x1; x++) {
            float dx = (float)x - cx * c->scale, dy = (float)y - cy * c->scale;
            float t = sqrtf(dx * dx + dy * dy) / (radius * c->scale);
            if (t >= 1.0f) continue;
            c->px[y * c->w + x] = lerp_color(ACE_GLOW, UI_BG, t);
        }
}

static void draw_ace(UiCanvas* c, const TrainerCardData* d, const uint32_t pics[6][MON_PIC_PIXELS],
                     const bool pic_ok[6], int ace, const BadgeArt* art) {
    ui_canvas_clear(c, UI_BG);
    const GamePartyMon* a = &d->party[ace];
    char buf[96];
    const float y0 = 124.0f;
    glow(c, 210.0f, y0 + 160.0f, 420.0f);

    snprintf(buf, sizeof buf, "%s'S ACE", d->name);
    ui_text(c, UI_FONT_REGULAR, 11.0f, 40.0f, y0, 600.0f, UI_ALIGN_LEFT, UI_DIM, buf);
    float sy = y0 + 22.0f;
    draw_pic(c, 40.0f, sy, 256.0f, pics, pic_ok, ace);

    float tx = 40.0f + 256.0f + 24.0f, tw = LEFT_W - 40.0f - tx;
    ui_text(c, UI_FONT_SEMIBOLD, 40.0f, tx, sy + 4.0f, tw, UI_ALIGN_LEFT, UI_TEXT, trainer_card_display_name(a));
    char sub[64];
    join2(sub, sizeof sub, a->nature, a->ability);
    snprintf(buf, sizeof buf, "Lv %u%s%s", (unsigned)a->level, sub[0] ? " \xC2\xB7 " : "", sub);
    ui_text(c, UI_FONT_REGULAR, 15.0f, tx, sy + 56.0f, tw, UI_ALIGN_LEFT, UI_MUTED, buf);
    type_pills(c, tx, sy + 86.0f, a);
    for (int r = 0; r < 6; r++) {
        uint8_t iv = a->ivs[k_stat_order[r]];
        float ry = sy + 124.0f + (float)r * 22.0f;
        ui_text(c, UI_FONT_REGULAR, 12.0f, tx, ry, 0, UI_ALIGN_LEFT, UI_MUTED, k_stat_label[r]);
        ui_bar(c, (UiRect){tx + 40.0f, ry + 5.0f, 180.0f, 6.0f}, (float)iv / 31.0f, UI_GOOD);
        snprintf(buf, sizeof buf, "%u", (unsigned)iv);
        ui_text(c, UI_FONT_REGULAR, 12.0f, tx + 232.0f, ry, 0, UI_ALIGN_LEFT, UI_TEXT, buf);
    }

    float by = sy + 256.0f + 30.0f;
    float x = 40.0f;
    ui_text(c, UI_FONT_REGULAR, 11.0f, x, by, 0, UI_ALIGN_LEFT, UI_DIM, "TRAINER");
    float nw = ui_text(c, UI_FONT_SEMIBOLD, 18.0f, x, by + 16.0f, 220.0f, UI_ALIGN_LEFT, UI_TEXT, d->name);
    snprintf(buf, sizeof buf, "#%05u", (unsigned)d->public_id);
    float iw = ui_text(c, UI_FONT_REGULAR, 13.0f, x + nw + 6.0f, by + 20.0f, 0, UI_ALIGN_LEFT, UI_FAINT, buf);
    x += nw + 6.0f + iw + 30.0f;
    play_time(d, buf, sizeof buf);
    ui_text(c, UI_FONT_REGULAR, 11.0f, x, by, 0, UI_ALIGN_LEFT, UI_DIM, "PLAY TIME");
    float pw = ui_text(c, UI_FONT_SEMIBOLD, 18.0f, x, by + 16.0f, 0, UI_ALIGN_LEFT, UI_TEXT, buf);
    x += (pw > 70.0f ? pw : 70.0f) + 30.0f;
    ui_text(c, UI_FONT_REGULAR, 11.0f, x, by, 0, UI_ALIGN_LEFT, UI_DIM, "BADGES");
    if (art && art->ok) {
        float bs = x + badge_row_width(32.0f) <= LEFT_W - 34.0f ? 32.0f : 16.0f;
        badge_row(c, x, by + 32.0f, d, bs, art);
    } else {
        badge_row(c, x, by + 26.5f, d, 15.0f, art);
    }

    /* Right column */
    ui_fill_rect(c, (UiRect){LEFT_W, 0.0f, 1.0f, (float)CARD_H}, UI_DIVIDER);
    float rx = LEFT_W + 34.0f, rw = (float)CARD_W - 34.0f - rx;
    ui_text(c, UI_FONT_REGULAR, 11.0f, rx, 34.0f, 0, UI_ALIGN_LEFT, UI_DIM, "THE TEAM");
    float ry = 64.0f;
    for (int i = 0; i < 6; i++) {
        if (i == ace || !slot_ok(d, i)) continue;
        const GamePartyMon* m = &d->party[i];
        draw_pic(c, rx, ry, 56.0f, pics, pic_ok, i);
        float nx = rx + 66.0f, right = rx + rw;
        float nmw = rw - 66.0f - 90.0f;
        if (m->egg) {
            ui_text(c, UI_FONT_SEMIBOLD, 14.0f, nx, ry + 20.0f, nmw, UI_ALIGN_LEFT, UI_TEXT, "Egg");
        } else {
            ui_text(c, UI_FONT_SEMIBOLD, 14.0f, nx, ry + 8.0f, nmw, UI_ALIGN_LEFT, UI_TEXT,
                    trainer_card_display_name(m));
            type_pills(c, nx, ry + 31.0f, m);
            snprintf(buf, sizeof buf, "Lv %u", (unsigned)m->level);
            ui_text(c, UI_FONT_REGULAR, 12.0f, right, ry + 10.0f, 0, UI_ALIGN_RIGHT, UI_MUTED, buf);
            ui_text(c, UI_FONT_REGULAR, 12.0f, right, ry + 28.0f, 90.0f, UI_ALIGN_RIGHT, UI_MUTED, m->nature);
        }
        ui_hline(c, rx, ry + 66.0f, rw, UI_SELECTED);
        ry += 76.0f;
    }
    footer(c, (float)CARD_W - 34.0f);
}

void trainer_card_draw(UiCanvas* c, const TrainerCardData* d, const uint32_t pics[6][MON_PIC_PIXELS],
                       const bool pic_ok[6], CardStyle style, int ace, const BadgeArt* art) {
    if (style == CARD_STYLE_ACE && slot_ok(d, ace) && !d->party[ace].egg) draw_ace(c, d, pics, pic_ok, ace, art);
    else draw_grid(c, d, pics, pic_ok, art);
}

bool trainer_card_save(const GameState* gs, const GameMem* m, const TrainerCardData* d,
                       CardStyle style, int ace, const char* path) {
    uint32_t* px = (uint32_t*)calloc((size_t)CARD_W * CARD_H, sizeof(uint32_t));
    uint32_t(*pics)[MON_PIC_PIXELS] = (uint32_t(*)[MON_PIC_PIXELS])calloc(6, sizeof(*pics));
    if (!px || !pics) { free(px); free(pics); return false; }
    bool ok[6] = {false, false, false, false, false, false};
    for (int i = 0; i < 6; i++) {
        if (!slot_ok(d, i)) continue;
        const GamePartyMon* mon = &d->party[i];
        ok[i] = mon_pic_decode(m, gs->profile, mon->species_id, mon->egg, mon->shiny, pics[i]);
    }
    UiCanvas c;
    ui_canvas_init(&c, px, CARD_W, CARD_H, 1.0f);
    trainer_card_draw(&c, d, (const uint32_t(*)[MON_PIC_PIXELS])pics, ok, style, ace, &gs->badge_art);
    bool r = screenshot_save_argb(px, CARD_W, CARD_H, path);
    free(px);
    free(pics);
    return r;
}

uint8_t trainer_card_key_request(int32_t scancode, uint16_t mod, bool repeat) {
    if (scancode != SDL_SCANCODE_C || repeat || (mod & (KMOD_CTRL | KMOD_GUI | KMOD_ALT))) return 0;
    return (uint8_t)(1 + ((mod & KMOD_SHIFT) ? CARD_STYLE_ACE : CARD_STYLE_GRID));
}
