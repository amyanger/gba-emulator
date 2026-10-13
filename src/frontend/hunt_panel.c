#include "frontend/hunt_panel.h"
#include "frontend/game_panel.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Layout in points (panel 600 x 480), from the approved mockup (hunt-layout-v2). */
#define PANEL_W 600.0f
#define LEFT 16.0f
#define RIGHT 584.0f
#define COL_W 250.0f  /* left column: 16 .. 266 */
#define RCOL_X 278.0f /* right column: 278 .. 584 */
#define RCOL_W (RIGHT - RCOL_X)
#define PAD 12.0f
#define CARDS_Y 76.0f
#define CARD_GAP 8.0f
#define TARGET_H 78.0f
#define ENC_H 104.0f
#define LOOK_H 54.0f
#define LISTS_Y 280.0f
#define ROW_H 19.0f
#define ROWS 6
#define FOOT_Y 456.0f

#define NDASH "\xE2\x80\x93" /* – */
#define MDOT " \xC2\xB7 "    /* " · " */

typedef struct {
    char s[GAME_NAME_LEN];
} Name;

static Name nice(const char* in) {
    Name n;
    game_panel_title_case(in, n.s, sizeof(n.s));
    return n;
}

void hunt_panel_number(uint32_t n, char* out, size_t size) {
    char raw[16], buf[24];
    snprintf(raw, sizeof(raw), "%u", (unsigned)n);
    size_t len = strlen(raw), o = 0;
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0)
            buf[o++] = ',';
        buf[o++] = raw[i];
    }
    buf[o] = '\0';
    snprintf(out, size, "%s", buf);
}

static void card_bg(UiCanvas* c, float x, float y, float w, float h) {
    ui_fill_round_rect(c, (UiRect){x, y, w, h}, UI_RADIUS, UI_SURFACE);
}

static void label(UiCanvas* c, float x, float y, float w, const char* upper) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x, y, w, UI_ALIGN_LEFT, UI_DIM, upper);
}

/* A muted line that shares the baseline of a larger semibold run at the same y. */
static float aside(UiCanvas* c, float x, float y, float big_size, float max_w, float size,
                   const char* text) {
    float ty = y + ui_text_ascent(c, UI_FONT_SEMIBOLD, big_size) -
               ui_text_ascent(c, UI_FONT_REGULAR, size);
    return ui_text(c, UI_FONT_REGULAR, size, x, ty, max_w, UI_ALIGN_LEFT, UI_MUTED, text);
}

/* Outline star with a dot in the middle; the font subset has no star glyph. */
static void star(UiCanvas* c, float cx, float cy, float r, uint32_t color) {
    float px[5], py[5];
    for (int i = 0; i < 5; i++) {
        float a = -1.5707963f + (float)i * 1.2566371f;
        px[i] = cx + r * cosf(a);
        py[i] = cy + r * sinf(a);
    }
    for (int i = 0; i < 5; i++) {
        int j = (i + 2) % 5;
        ui_capsule(c, px[i], py[i], px[j], py[j], 1.3f, color);
    }
    ui_fill_round_rect(c, (UiRect){cx - 1.5f, cy - 1.5f, 3.0f, 3.0f}, 1.5f, color);
}

/* ---- Header ---- */

static void header(UiCanvas* c, const HuntView* v, float y) {
    float tw = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, LEFT, y, RIGHT - LEFT, UI_ALIGN_LEFT,
                       UI_TEXT, v->map);
    char sub[64], adv[16];
    hunt_panel_number(v->advance, adv, sizeof(adv));
    if (v->method[0])
        snprintf(sub, sizeof(sub), "%s" MDOT "advance %s", v->method, adv);
    else
        snprintf(sub, sizeof(sub), "advance %s", adv);
    float x = LEFT + tw + 10.0f;
    if (x < RIGHT - 20.0f)
        aside(c, x, y, UI_SIZE_TITLE, RIGHT - x, UI_SIZE_BODY, sub);
}

/* ---- Left column ---- */

static void target_card(UiCanvas* c, const HuntView* v) {
    float x = LEFT, y = CARDS_Y;
    card_bg(c, x, y, COL_W, TARGET_H);
    ui_fill_round_rect(c, (UiRect){x, y + 8.0f, 2.5f, TARGET_H - 16.0f}, 1.25f, UI_ACCENT);
    label(c, x + PAD, y + 8.0f, COL_W - 2 * PAD, "TARGET");
    float ny = y + 24.0f;
    char pct[48];
    pct[0] = '\0';
    if (!v->any_target) {
        if (v->target_percent) {
            Name m = nice(v->method[0] ? v->method : "land");
            for (char* p = m.s; *p; p++)
                *p = (char)tolower((unsigned char)*p);
            snprintf(pct, sizeof(pct), "%u%% of %s", (unsigned)v->target_percent, m.s);
        } else {
            snprintf(pct, sizeof(pct), "Not on this map");
        }
    }
    float pw = pct[0] ? ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_SMALL, pct) : 0.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x + PAD, ny, COL_W - 2 * PAD - pw - 8.0f,
            UI_ALIGN_LEFT, UI_TEXT, v->target);
    if (pct[0])
        aside(c, x + COL_W - PAD - pw, ny, UI_SIZE_TITLE, pw + 2.0f, UI_SIZE_SMALL, pct);
    float py = y + 51.0f;
    if (v->any_target) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x + PAD, py, COL_W - 2 * PAD, UI_ALIGN_LEFT,
                UI_MUTED, "Every species on this map");
        return;
    }
    float px = x + PAD;
    for (int i = 0; i < 2; i++) {
        uint8_t t = v->target_types[i];
        const char* name = game_panel_type_name(t);
        if (!name || (i == 1 && t == v->target_types[0]))
            continue;
        px += ui_pill(c, px, py, name, ui_type_color(t)) + 6.0f;
    }
}

static void encounters_card(UiCanvas* c, const HuntView* v) {
    float x = LEFT, y = CARDS_Y + TARGET_H + CARD_GAP, w = COL_W - 2 * PAD;
    card_bg(c, x, y, COL_W, ENC_H);
    label(c, x + PAD, y + 8.0f, w, "ENCOUNTERS");
    char buf[48];
    hunt_panel_number(v->total, buf, sizeof(buf));
    float ty = y + 22.0f;
    float bw = ui_text(c, UI_FONT_SEMIBOLD, 30.0f, x + PAD, ty, w, UI_ALIGN_LEFT, UI_TEXT, buf);
    snprintf(buf, sizeof(buf), "+%u this session", (unsigned)v->session);
    aside(c, x + PAD + bw + 8.0f, ty, 30.0f, w - bw - 8.0f, UI_SIZE_BODY, buf);

    ui_bar(c, (UiRect){x + PAD, y + 62.0f, w, 6.0f}, (float)v->chance, UI_MID);
    if (v->any_target) {
        snprintf(buf, sizeof(buf), "%.1f%% you'd have a shiny by now", v->chance * 100.0);
    } else {
        Name n = nice(v->target);
        snprintf(buf, sizeof(buf), "%.1f%% you'd have a shiny %s by now", v->chance * 100.0, n.s);
    }
    /* A target's name makes the line long: a size smaller rather than cut off. */
    float size =
        ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_SMALL, buf) > w ? UI_SIZE_LABEL : UI_SIZE_SMALL;
    ui_text(c, UI_FONT_REGULAR, size, x + PAD, y + 73.0f, w, UI_ALIGN_LEFT, UI_MUTED, buf);

    float ly = y + 87.0f;
    const char* hint = v->reset_armed ? "Press R again to reset" : "Press R twice to reset";
    float hw = ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x + COL_W - PAD, ly + 1.0f, w / 2,
                       UI_ALIGN_RIGHT, UI_FAINT, hint);
    if (!v->any_target) {
        Name n = nice(v->target);
        char cnt[16];
        hunt_panel_number(v->target_count, cnt, sizeof(cnt));
        snprintf(buf, sizeof(buf), "%s %s", cnt, n.s);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x + PAD, ly, w - hw - 8.0f, UI_ALIGN_LEFT,
                UI_MUTED, buf);
    }
}

/* ---- Right column ---- */

static float chip(UiCanvas* c, float x, float y, const char* text, bool with_star) {
    float tw = ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_LABEL + 0.5f, text);
    float sw = with_star ? 14.0f : 0.0f;
    float w = tw + sw + 14.0f, h = 20.0f;
    ui_fill_round_rect(c, (UiRect){x, y, w, h}, 5.0f, UI_SELECTED);
    if (with_star)
        star(c, x + 11.0f, y + h / 2, 4.2f, UI_MID);
    float ty = y + h / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_LABEL + 0.5f);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL + 0.5f, x + 7.0f + sw, ty, 0, UI_ALIGN_LEFT, UI_TEXT,
            text);
    return w + 5.0f;
}

static void looking_card(UiCanvas* c, const HuntView* v) {
    float x = RCOL_X, y = CARDS_Y;
    card_bg(c, x, y, RCOL_W, LOOK_H);
    label(c, x + PAD, y + 8.0f, RCOL_W - 2 * PAD, "LOOKING FOR");
    float cx = x + PAD, cy = y + 26.0f;
    char buf[48];
    cx += chip(c, cx, cy, "Shiny", true);
    if (v->any_target)
        snprintf(buf, sizeof(buf), "Any species");
    else {
        Name n = nice(v->target);
        snprintf(buf, sizeof(buf), "%s", n.s);
    }
    cx += chip(c, cx, cy, buf, false);
    if (strcmp(v->nature_filter, "Any") == 0)
        snprintf(buf, sizeof(buf), "Nature: any");
    else {
        Name n = nice(v->nature_filter);
        snprintf(buf, sizeof(buf), "Nature: %s", n.s);
    }
    chip(c, cx, cy, buf, false);
}

static void match_card(UiCanvas* c, const HuntView* v) {
    float x = RCOL_X, y = CARDS_Y + LOOK_H + CARD_GAP,
          h = CARDS_Y + TARGET_H + CARD_GAP + ENC_H - y;
    float w = RCOL_W - 2 * PAD;
    card_bg(c, x, y, RCOL_W, h);
    label(c, x + PAD, y + 8.0f, w, "NEXT MATCH");
    const char* msg = NULL;
    if (!v->predict)
        msg = v->why_not;
    else if (!v->match_found)
        msg = v->scanning ? "Searching..." : "None in the next 1,048,576 advances";
    if (msg) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x + PAD, y + 28.0f, w, UI_ALIGN_LEFT, UI_MUTED,
                msg);
        return;
    }
    char buf[64];
    if (v->match_seconds >= 60.0f) {
        unsigned secs = (unsigned)(v->match_seconds + 0.5f);
        snprintf(buf, sizeof(buf), "%u:%02u", secs / 60, secs % 60);
    } else {
        snprintf(buf, sizeof(buf), "%.1f s", v->match_seconds);
    }
    float ty = y + 24.0f;
    float bw = ui_text(c, UI_FONT_SEMIBOLD, 28.0f, x + PAD, ty, w, UI_ALIGN_LEFT, UI_TEXT, buf);
    char n[24];
    hunt_panel_number(v->match_in, n, sizeof(n));
    snprintf(buf, sizeof(buf), "%s advance%s", n, v->match_in == 1 ? "" : "s");
    aside(c, x + PAD + bw + 8.0f, ty, 28.0f, w - bw - 8.0f, UI_SIZE_BODY, buf);

    float ky = y + 66.0f;
    game_panel_nature_row(c, x + PAD, ky, 56.0f, w, v->match_nature, v->match_up, v->match_down);
    static const uint8_t order[6] = {0, 1, 2,
                                     4, 5, 3}; /* WildPreview IVs are HP Atk Def Spe SpA SpD */
    char iv[48];
    size_t o = 0;
    for (int i = 0; i < 6; i++)
        o += (size_t)snprintf(iv + o, sizeof(iv) - o, i ? " / %u" : "%u",
                              (unsigned)v->match_ivs[order[i]]);
    ui_text(c, UI_FONT_REGULAR, 12.5f, x + PAD, ky + 20.0f, 56.0f - 6.0f, UI_ALIGN_LEFT, UI_MUTED,
            "IVs");
    ui_text(c, UI_FONT_REGULAR, 12.5f, x + PAD + 56.0f, ky + 20.0f, w - 56.0f, UI_ALIGN_LEFT,
            UI_TEXT, iv);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x + PAD, ky + 40.0f, w, UI_ALIGN_LEFT, UI_FAINT,
            "HP / Atk / Def / SpA / SpD / Spe");
}

/* ---- Lists ---- */

static void seen_list(UiCanvas* c, const HuntView* v) {
    label(c, LEFT, LISTS_Y, COL_W, "SEEN THIS HUNT");
    float y = LISTS_Y + 18.0f;
    if (v->tally_n == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT, y, COL_W, UI_ALIGN_LEFT, UI_MUTED,
                "Nothing yet");
        return;
    }
    const float name_w = 84.0f, num_w = 40.0f;
    float bar_x = LEFT + name_w, bar_w = COL_W - name_w - num_w;
    uint32_t top = v->tally_count[0] ? v->tally_count[0] : 1;
    uint8_t shown = v->tally_n < ROWS ? v->tally_n : ROWS;
    for (uint8_t i = 0; i < shown; i++, y += ROW_H) {
        bool is_target = !v->any_target && strcmp(v->tally_name[i], v->target) == 0;
        Name n = nice(v->tally_name[i]);
        ui_text(c, is_target ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT, y,
                name_w - 6.0f, UI_ALIGN_LEFT, UI_TEXT, n.s);
        float by = y + ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_SMALL) - 3.0f;
        ui_bar(c, (UiRect){bar_x, by, bar_w, 6.0f}, (float)v->tally_count[i] / (float)top,
               is_target ? UI_GOOD : UI_ACCENT);
        char cnt[16];
        hunt_panel_number(v->tally_count[i], cnt, sizeof(cnt));
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT + COL_W, y, num_w - 6.0f, UI_ALIGN_RIGHT,
                UI_TEXT, cnt);
    }
    if (v->tally_n > ROWS) {
        char buf[24];
        snprintf(buf, sizeof(buf), "+%u more", (unsigned)(v->tally_n - ROWS));
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, y + 2.0f, COL_W, UI_ALIGN_LEFT, UI_FAINT,
                buf);
    }
}

static void next_list(UiCanvas* c, const HuntView* v) {
    label(c, RCOL_X, LISTS_Y, RCOL_W, "NEXT FRAMES");
    float y = LISTS_Y + 18.0f;
    if (!v->predict) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, RCOL_X, y, RCOL_W, UI_ALIGN_LEFT, UI_MUTED,
                "Not predicted");
        return;
    }
    const float tag_w = 40.0f, name_w = 96.0f;
    for (int i = 0; i < ROWS; i++, y += ROW_H) {
        const WildPreview* r = &v->rows[i];
        char tag[8];
        if (i == 0)
            snprintf(tag, sizeof(tag), "now");
        else
            snprintf(tag, sizeof(tag), "+%d", i);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, RCOL_X, y, tag_w, UI_ALIGN_LEFT,
                i == 0 ? UI_ACCENT : UI_MUTED, tag);
        float x = RCOL_X + tag_w;
        switch (r->kind) {
        case WG_BATTLE:
        case WG_ROAMER: {
            bool is_target = !v->any_target && strcmp(v->row_name[i], v->target) == 0;
            Name n = nice(v->row_name[i]);
            ui_text(c, is_target ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, UI_SIZE_SMALL, x, y,
                    name_w - 6.0f, UI_ALIGN_LEFT, UI_TEXT, n.s);
            if (r->kind == WG_ROAMER)
                break;
            Name nat = nice(v->row_nature[i]);
            ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x + name_w, y, 100.0f, UI_ALIGN_LEFT,
                    UI_MUTED, nat.s);
            float rx = RIGHT;
            if (!r->certain)
                ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, rx, y, 0, UI_ALIGN_RIGHT, UI_FAINT, "?");
            else if (r->shiny)
                star(c, rx - 5.0f, y + ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_SMALL), 4.6f,
                     UI_MID);
            break;
        }
        case WG_NO_BATTLE:
            ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, RIGHT - x, UI_ALIGN_LEFT, UI_FAINT,
                    "no encounter");
            break;
        default:
            ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, RIGHT - x, UI_ALIGN_LEFT, UI_FAINT,
                    NDASH);
            break;
        }
    }
}

/* ---- Page ---- */

static void unavailable(UiCanvas* c, const char* reason) {
    float x = PANEL_W / 2, w = RIGHT - LEFT, y = 140.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, w, UI_ALIGN_CENTER, UI_TEXT, "Hunt");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y + 34.0f, w, UI_ALIGN_CENTER, UI_MUTED, reason);
}

void hunt_panel_draw(UiCanvas* c, const HuntView* v, float top) {
    if (!v) {
        unavailable(c, "Hunt unavailable");
        return;
    }
    if (!v->on) {
        unavailable(c, v->reason[0] ? v->reason : "Hunt unavailable");
        return;
    }
    header(c, v, top + 2.0f);
    target_card(c, v);
    encounters_card(c, v);
    looking_card(c, v);
    match_card(c, v);
    seen_list(c, v);
    next_list(c, v);
    if (v->write_failed)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, FOOT_Y, RIGHT - LEFT, UI_ALIGN_LEFT,
                UI_WARN,
                v->file_foreign ? "Hunt file belongs to another game, not saving"
                                : "Couldn't save the hunt file, retrying");
    else
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, FOOT_Y, RIGHT - LEFT, UI_ALIGN_LEFT,
                UI_FAINT,
                "Frames assume a battle that starts on that frame. Counts keep going through "
                "savestates.");
}
