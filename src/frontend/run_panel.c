#include "frontend/run_panel.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <stdio.h>
#include <string.h>

/* Layout in points (panel 600 x 480), from the approved mockups (dashboard option A,
 * Details option 1). Colours and sizes match the rest of the panel (style A). */
#define PANEL_W      600.0f
#define LEFT         16.0f
#define RIGHT        584.0f
#define LIST_END     448.0f   /* lists stop above the footer */
#define FOOT_Y       456.0f
#define ROW_H        19.0f
#define KV_SIZE      12.5f

/* Dashboard */
#define BADGE_Y      76.0f
#define BADGE_H      50.0f
#define BADGE_GAP    6.0f
#define COLS_Y       136.0f
#define FEED_Y       (COLS_Y + 18.0f)
#define FEED_W       306.0f              /* left column: LEFT .. 322 */
#define FEED_TIME_W  40.0f
#define FEED_TEXT_X  (LEFT + 58.0f)
#define FEED_WHERE_W 96.0f
#define HI_X         336.0f              /* right column: 336 .. RIGHT */
#define HI_W         (RIGHT - HI_X)
#define TILE_H       40.0f
#define CARD_GAP     8.0f

/* Details */
#define TEAM_HEAD_Y  94.0f
#define TEAM_Y       (TEAM_HEAD_Y + 18.0f)
#define COUNTER_ROW  17.0f
#define COUNTER_Y    (FOOT_Y - 10.0f - 18.0f - 4 * COUNTER_ROW)
#define TEAM_END     (COUNTER_Y - 8.0f)

#define RUN_PURPLE   0xFFB18CFFu         /* evolutions, from the mockup */

#define NDASH "\xE2\x80\x93"  /* – */
#define MDOT  " \xC2\xB7 "    /* " · " */

/* Rows that fit in a list from y0 to y1, and how many show once a "Showing" line takes one. */
static uint8_t rows_fit(float y0, float y1) { return (uint8_t)((y1 - y0) / ROW_H); }

static uint8_t list_shown(uint8_t count, uint8_t fit, bool extra_line) {
    if (count > fit || extra_line) return (uint8_t)(fit - 1);
    return fit;
}

static uint8_t max_start(uint8_t count, uint8_t shown) { return count > shown ? (uint8_t)(count - shown) : 0; }

uint8_t run_panel_max_scroll(const RunView* v, bool details) {
    if (!v || v->state != RUN_VIEW_RUN) return 0;
    if (details) {
        uint8_t count = v->team_count;
        return max_start(count, list_shown(count, rows_fit(TEAM_Y, TEAM_END), v->team_total > count));
    }
    uint8_t count = v->feed_count;
    return max_start(count, list_shown(count, rows_fit(FEED_Y, LIST_END), false));
}

static void section(UiCanvas* c, float x, float y, float w, const char* upper) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x, y, w, UI_ALIGN_LEFT, UI_DIM, upper);
}

/* 18402 -> "18,402". */
static void num(uint32_t n, char* out, size_t size) {
    char raw[16], buf[24];
    snprintf(raw, sizeof(raw), "%u", (unsigned)n);
    size_t len = strlen(raw), o = 0;
    for (size_t i = 0; i < len; i++) {
        if (i > 0 && (len - i) % 3 == 0) buf[o++] = ',';
        buf[o++] = raw[i];
    }
    buf[o] = '\0';
    snprintf(out, size, "%s", buf);
}

/* Big title plus a muted line on its baseline. */
static void title_line(UiCanvas* c, float y, const char* title, const char* meta) {
    float tw = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, LEFT, y, RIGHT - LEFT, UI_ALIGN_LEFT, UI_TEXT,
                       title);
    float x = LEFT + tw + 10.0f;
    if (x >= RIGHT - 20.0f) return;
    float ty = y + ui_text_ascent(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE) -
               ui_text_ascent(c, UI_FONT_REGULAR, UI_SIZE_BODY);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, ty, RIGHT - x, UI_ALIGN_LEFT, UI_MUTED, meta);
}

static void head_meta(const RunView* v, char* out, size_t size) {
    snprintf(out, size, "%s played" MDOT "%u badge%s", v->play_time, (unsigned)v->badge_count,
             v->badge_count == 1 ? "" : "s");
}

/* Footer left: a warning when the run file has a problem, else the tracking note. */
static void footer_left(UiCanvas* c, const RunView* v, float w) {
    const char* text = v->tracking_since;
    uint32_t color = UI_FAINT;
    if (v->write_failed) {
        text = "Can't write the run file: this run is only kept until you quit.";
        color = UI_WARN;
    } else if (v->log_full) {
        text = "Run log full: new events are no longer recorded.";
        color = UI_WARN;
    }
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, FOOT_Y, w, UI_ALIGN_LEFT, color, text);
}

static void showing_line(UiCanvas* c, float x, float y, float w, unsigned first, unsigned last, unsigned of,
                         unsigned hidden) {
    char buf[80];
    if (hidden)
        snprintf(buf, sizeof(buf), "Showing %u" NDASH "%u of %u" MDOT "%u more not listed", first, last, of,
                 hidden);
    else
        snprintf(buf, sizeof(buf), "Showing %u" NDASH "%u of %u", first, last, of);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x, y + 3.0f, w, UI_ALIGN_LEFT, UI_FAINT, buf);
}

/* ---- Dashboard ---- */

static void badge_cell(UiCanvas* c, float x, float w, const RunView* v, int b) {
    bool have = v->badges[b].have;
    ui_fill_round_rect(c, (UiRect){x, BADGE_Y, w, BADGE_H}, UI_RADIUS, UI_SURFACE);
    float cx = x + w / 2, cy = BADGE_Y + 11.0f;
    if (have) ui_fill_round_rect(c, (UiRect){cx - 6.0f, cy - 6.0f, 12.0f, 12.0f}, 6.0f, UI_MID);
    else ui_ring(c, cx, cy, 5.5f, 1.0f, UI_FAINT);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, cx, BADGE_Y + 19.0f, w - 6.0f, UI_ALIGN_CENTER,
            have ? UI_MUTED : UI_FAINT, v->badges[b].name);
    if (have && v->badges[b].split[0])
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, cx, BADGE_Y + 32.0f, w - 6.0f, UI_ALIGN_CENTER,
                v->badges[b].untimed ? UI_FAINT : UI_TEXT, v->badges[b].split);
}

static uint32_t icon_color(uint8_t icon) {
    switch (icon) {
    case RUN_ICON_BADGE: return UI_MID;
    case RUN_ICON_STORY: return UI_ACCENT;
    case RUN_ICON_CATCH: return UI_GOOD;
    case RUN_ICON_GIFT: return UI_GOOD;
    case RUN_ICON_FAINT: return UI_BAD;
    case RUN_ICON_WHITEOUT: return UI_BAD;
    case RUN_ICON_EVOLVE: return RUN_PURPLE;
    case RUN_ICON_ACE: return UI_WARN;
    default: return UI_FAINT;
    }
}

static void feed_row(UiCanvas* c, float y, const RunViewFeed* f) {
    float ty = y + ui_text_ascent(c, UI_FONT_REGULAR, UI_SIZE_SMALL) -
               ui_text_ascent(c, UI_FONT_REGULAR, UI_SIZE_LABEL);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, ty, FEED_TIME_W, UI_ALIGN_LEFT, UI_FAINT, f->time);
    float cx = LEFT + 49.0f, cy = y + ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_SMALL);
    uint32_t color = icon_color(f->icon);
    if (f->icon == RUN_ICON_WHITEOUT || f->icon == RUN_ICON_GIFT) ui_ring(c, cx, cy, 4.0f, 1.5f, color);
    else ui_fill_round_rect(c, (UiRect){cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 4.0f, color);
    /* The event text wins; the place gets what is left, between 48 and 96 points. */
    float right = LEFT + FEED_W;
    float tw = ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_SMALL, f->text);
    float room = right - FEED_TEXT_X - tw - 8.0f;
    float wmax = room < 48.0f ? 48.0f : room > FEED_WHERE_W ? FEED_WHERE_W : room;
    float ww = f->where[0] ? ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, right, ty, wmax, UI_ALIGN_RIGHT,
                                     UI_FAINT, f->where) : 0.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, FEED_TEXT_X, y, right - FEED_TEXT_X - (ww > 0 ? ww + 8.0f : 0.0f),
            UI_ALIGN_LEFT, UI_TEXT, f->text);
}

static void journey(UiCanvas* c, const RunView* v, uint8_t scroll) {
    section(c, LEFT, COLS_Y, FEED_W, "JOURNEY");
    if (v->feed_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT, FEED_Y, FEED_W, UI_ALIGN_LEFT, UI_MUTED,
                "Nothing yet. Badges, catches, faints");
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT, FEED_Y + ROW_H, FEED_W, UI_ALIGN_LEFT, UI_MUTED,
                "and evolutions show up here as you play.");
        return;
    }
    uint8_t count = v->feed_count;
    uint8_t shown = list_shown(count, rows_fit(FEED_Y, LIST_END), false);
    uint8_t start = scroll < max_start(count, shown) ? scroll : max_start(count, shown);
    float y = FEED_Y;
    for (uint8_t i = start; i < count && i < start + shown; i++, y += ROW_H) feed_row(c, y, &v->feed[i]);
    if (count > shown)
        showing_line(c, LEFT, y, FEED_W, start + 1u, (unsigned)(start + shown), count, 0);
}

static void stat_tile(UiCanvas* c, float x, float y, float w, uint32_t value, const char* label,
                      uint32_t color) {
    char buf[20];
    num(value, buf, sizeof(buf));
    ui_fill_round_rect(c, (UiRect){x, y, w, TILE_H}, UI_RADIUS, UI_SURFACE);
    ui_text(c, UI_FONT_SEMIBOLD, 16.0f, x + 10.0f, y + 4.0f, w - 16.0f, UI_ALIGN_LEFT, color, buf);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x + 10.0f, y + 24.0f, w - 16.0f, UI_ALIGN_LEFT, UI_MUTED,
            label);
}

/* A card with a small label and up to two lines of text, wrapped at a space. Returns its height. */
static float card(UiCanvas* c, float y, const char* label, const char* text) {
    const float pad = 10.0f, w = HI_W - 2 * pad, line = 15.0f;
    char one[64], two[64];
    snprintf(one, sizeof(one), "%s", text);
    two[0] = '\0';
    if (ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_SMALL, one) > w) {
        /* Longest prefix ending before a space that fits; the rest goes on line two. */
        size_t best = 0;
        for (size_t i = 1; one[i]; i++) {
            if (one[i] != ' ') continue;
            one[i] = '\0';
            bool fits = ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_SMALL, one) <= w;
            one[i] = ' ';
            if (!fits) break;
            best = i;
        }
        if (best) {
            snprintf(two, sizeof(two), "%s", one + best + 1);
            one[best] = '\0';
        }
    }
    float h = 21.0f + line + (two[0] ? line : 0.0f) + 3.0f;
    ui_fill_round_rect(c, (UiRect){HI_X, y, HI_W, h}, UI_RADIUS, UI_SURFACE);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, HI_X + pad, y + 6.0f, w, UI_ALIGN_LEFT, UI_DIM, label);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, HI_X + pad, y + 21.0f, w, UI_ALIGN_LEFT, UI_TEXT, one);
    if (two[0])
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, HI_X + pad, y + 21.0f + line, w, UI_ALIGN_LEFT, UI_TEXT,
                two);
    return h;
}

static void highlights(UiCanvas* c, const RunView* v) {
    section(c, HI_X, COLS_Y, HI_W, "HIGHLIGHTS");
    float gap = 8.0f, tw = (HI_W - gap) / 2, y = FEED_Y;
    stat_tile(c, HI_X, y, tw, v->caught, "caught", UI_TEXT);
    stat_tile(c, HI_X + tw + gap, y, tw, v->faints, v->faints_since_tracking ? "faints since tracking" : "faints",
              v->faints ? UI_BAD : UI_TEXT);
    y += TILE_H + gap;
    stat_tile(c, HI_X, y, tw, v->steps, "steps", UI_TEXT);
    stat_tile(c, HI_X + tw + gap, y, tw, v->battles, "battles", UI_TEXT);
    y += TILE_H + 10.0f;
    if (v->mvp[0]) y += card(c, y, "MVP", v->mvp) + CARD_GAP;
    if (v->closest[0]) y += card(c, y, "Closest call", v->closest) + CARD_GAP;
    if (v->fun[0]) card(c, y, "Fun fact", v->fun);
}

static void dashboard(UiCanvas* c, const RunView* v, float top, uint8_t scroll) {
    char title[GAME_NAME_LEN + 8], meta[96];
    snprintf(title, sizeof(title), "%s's run", v->player);
    char dex[48];
    head_meta(v, meta, sizeof(meta));
    snprintf(dex, sizeof(dex), MDOT "Dex %u owned, %u seen", (unsigned)v->dex_owned, (unsigned)v->dex_seen);
    strncat(meta, dex, sizeof(meta) - strlen(meta) - 1);
    title_line(c, top + 4.0f, title, meta);

    float bw = (RIGHT - LEFT - 7 * BADGE_GAP) / 8;
    for (int b = 0; b < RUN_BADGES; b++) badge_cell(c, LEFT + b * (bw + BADGE_GAP), bw, v, b);

    journey(c, v, scroll);
    highlights(c, v);

    footer_left(c, v, RIGHT - LEFT);
}

/* ---- Details ---- */

/* Column x positions of the team table: Pokémon, Joined, Where, Lv, KOs, Faints, Evolved. */
static const float k_col[8] = {LEFT, 134.0f, 182.0f, 298.0f, 354.0f, 398.0f, 446.0f, RIGHT};

static void col_text(UiCanvas* c, int col, float y, UiFont f, uint32_t color, const char* s) {
    ui_text(c, f, UI_SIZE_SMALL, k_col[col], y, k_col[col + 1] - k_col[col] - 6.0f, UI_ALIGN_LEFT, color, s);
}

static void team_row(UiCanvas* c, float y, const RunViewMon* m) {
    char buf[24];
    uint32_t text = m->away ? UI_MUTED : UI_TEXT;
    ui_hline(c, LEFT, y - 3.0f, RIGHT - LEFT, UI_SURFACE);
    float name_w = k_col[1] - k_col[0] - 6.0f;
    float mvp_w = m->mvp ? ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, "MVP") + 4.0f : 0.0f;
    float nw = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, LEFT, y, name_w - mvp_w, UI_ALIGN_LEFT, text, m->name);
    if (m->mvp) {
        float my = y + ui_text_ascent(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL) -
                   ui_text_ascent(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL);
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, LEFT + nw + 4.0f, my, mvp_w, UI_ALIGN_LEFT, UI_MID, "MVP");
    }
    col_text(c, 1, y, UI_FONT_REGULAR, m->joined[0] && strcmp(m->joined, "\xC2\xB7") != 0 ? text : UI_FAINT,
             m->joined);
    col_text(c, 2, y, UI_FONT_REGULAR, text, m->where);
    if (m->lv_from && m->lv_from != m->lv_to)
        snprintf(buf, sizeof(buf), "%u" NDASH "%u", (unsigned)m->lv_from, (unsigned)m->lv_to);
    else
        snprintf(buf, sizeof(buf), "%u", (unsigned)m->lv_to);
    col_text(c, 3, y, UI_FONT_REGULAR, text, buf);
    num(m->kos, buf, sizeof(buf));
    col_text(c, 4, y, UI_FONT_REGULAR, m->mvp ? UI_GOOD : text, buf);
    num(m->faints, buf, sizeof(buf));
    col_text(c, 5, y, UI_FONT_REGULAR, text, buf);
    if (m->evolved[0]) col_text(c, 6, y, UI_FONT_REGULAR, RUN_PURPLE, m->evolved);
    else col_text(c, 6, y, UI_FONT_REGULAR, UI_FAINT, "\xC2\xB7");
}

static void team_table(UiCanvas* c, const RunView* v, uint8_t scroll) {
    static const char* const heads[7] = {"POKEMON", "JOINED", "WHERE", "LV", "KOS", "FAINTS", "EVOLVED"};
    section(c, LEFT, TEAM_HEAD_Y - 18.0f, RIGHT - LEFT, "TEAM STORY");
    if (v->team_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT, TEAM_HEAD_Y, RIGHT - LEFT, UI_ALIGN_LEFT, UI_MUTED,
                "No Pok\xC3\xA9mon yet.");
        return;
    }
    for (int i = 0; i < 7; i++)
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, k_col[i], TEAM_HEAD_Y, k_col[i + 1] - k_col[i] - 6.0f,
                UI_ALIGN_LEFT, UI_DIM, heads[i]);
    uint8_t count = v->team_count;
    unsigned hidden = v->team_total > count ? (unsigned)(v->team_total - count) : 0;
    uint8_t shown = list_shown(count, rows_fit(TEAM_Y, TEAM_END), hidden > 0);
    uint8_t start = scroll < max_start(count, shown) ? scroll : max_start(count, shown);
    float y = TEAM_Y;
    for (uint8_t i = start; i < count && i < start + shown; i++, y += ROW_H) team_row(c, y, &v->team[i]);
    if (count > shown || hidden)
        showing_line(c, LEFT, y, RIGHT - LEFT, start + 1u, (unsigned)(start + (count < shown ? count : shown)),
                     count, hidden);
}

/* text, when set, is shown instead of value. */
typedef struct { const char* label; uint32_t value; uint32_t color; const char* text; } Counter;

static void counter_col(UiCanvas* c, float x, float w, const char* title, const Counter rows[4]) {
    section(c, x, COUNTER_Y, w, title);
    for (int i = 0; i < 4; i++) {
        char buf[32];
        float y = COUNTER_Y + 18.0f + i * COUNTER_ROW;
        if (rows[i].text) snprintf(buf, sizeof(buf), "%s", rows[i].text);
        else num(rows[i].value, buf, sizeof(buf));
        float vw = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, x + w, y, w / 2, UI_ALIGN_RIGHT, rows[i].color, buf);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, w - vw - 6.0f, UI_ALIGN_LEFT, UI_MUTED, rows[i].label);
    }
}

static void counters(UiCanvas* c, const RunView* v) {
    const float gap = 16.0f, w = (RIGHT - LEFT - 3 * gap) / 4;
    char shiny[32];
    snprintf(shiny, sizeof(shiny), "%u of %u", (unsigned)v->shiny_caught, (unsigned)v->shiny_seen);
    const Counter battles[4] = {{"Wild", v->wild, UI_TEXT, NULL},
                                {"Trainer battles", v->trainer_battles, UI_TEXT, NULL},
                                {"Whiteouts", v->whiteouts, v->whiteouts ? UI_BAD : UI_TEXT, NULL},
                                {"Upsets", v->upsets, UI_TEXT, NULL}};
    const Counter mons[4] = {{"Caught", v->caught, UI_TEXT, NULL},
                             {"Dex seen", v->dex_seen, UI_TEXT, NULL},
                             {"Eggs hatched", v->eggs, UI_TEXT, NULL},
                             {"Shinies caught", 0, v->shiny_seen ? UI_MID : UI_TEXT, shiny}};
    const Counter explore[4] = {{"Steps", v->steps, UI_TEXT, NULL},
                                {"Center heals", v->centers, UI_TEXT, NULL},
                                {"Shops", v->shops, UI_TEXT, NULL},
                                {"Fishing", v->fishing, UI_TEXT, NULL}};
    const Counter fun[4] = {{"Ledges", v->ledges, UI_TEXT, NULL},
                            {"Rained on", v->rained, UI_TEXT, NULL},
                            v->no_clock
                                ? (Counter){"Pok\xC3\xA9" "dex checks", v->dex_checks, UI_TEXT,
                                            NULL}
                                : (Counter){"Clock checks", v->clocks, UI_TEXT, NULL},
                            {"Splash used", v->splash, UI_TEXT, NULL}};
    counter_col(c, LEFT, w, "BATTLES", battles);
    counter_col(c, LEFT + (w + gap), w, "POKEMON", mons);
    counter_col(c, LEFT + 2 * (w + gap), w, "EXPLORING", explore);
    counter_col(c, LEFT + 3 * (w + gap), w, "JUST FOR FUN", fun);
}

static void details(UiCanvas* c, const RunView* v, float top, uint8_t scroll) {
    char meta[96];
    head_meta(v, meta, sizeof(meta));
    char full[GAME_NAME_LEN + 100];
    snprintf(full, sizeof(full), "%s" MDOT "%s", v->player, meta);
    title_line(c, top + 4.0f, "Details", full);
    team_table(c, v, scroll);
    counters(c, v);

    float half = (RIGHT - LEFT) / 2 - 8.0f;
    char buf[80];
    if (v->most_enc[0]) {
        snprintf(buf, sizeof(buf), "Most encountered: %s", v->most_enc);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, FOOT_Y, half, UI_ALIGN_LEFT, UI_FAINT, buf);
    }
    if (v->caught_in[0]) {
        snprintf(buf, sizeof(buf), "Caught in: %s", v->caught_in);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, RIGHT, FOOT_Y, half, UI_ALIGN_RIGHT, UI_FAINT, buf);
    }
}

/* ---- No run yet ---- */

static void no_run(UiCanvas* c, const RunView* v) {
    float x = PANEL_W / 2, w = RIGHT - LEFT, y = 140.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, w, UI_ALIGN_CENTER, UI_TEXT, "Run");
    y += 34.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, UI_MUTED, v->reason);
    if (v->state != RUN_VIEW_WAITING) return;
    y += 36.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, UI_FAINT,
            "Badges, catches, faints and the game's own counters");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y + 20.0f, w, UI_ALIGN_CENTER, UI_FAINT,
            "show up here once your save is loaded.");
}

void run_panel_draw(UiCanvas* c, const RunView* v, float top, uint8_t scroll, bool show_details) {
    if (!v || v->state != RUN_VIEW_RUN) {
        static const RunView off = { .state = RUN_VIEW_WAITING, .reason = "Waiting for save data" };
        no_run(c, v ? v : &off);
        return;
    }
    if (show_details) details(c, v, top, scroll);
    else dashboard(c, v, top, scroll);
}
