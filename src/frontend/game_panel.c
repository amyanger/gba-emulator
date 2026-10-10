#include "frontend/game_panel.h"
#include "frontend/overlay_draw.h"
#include "common.h"
#include <stdarg.h>
#include <stdio.h>

#define COL_BG     0xFF0D0D36
#define COL_HEADER 0xFF00FFFF
#define COL_LABEL  0xFF88AACC
#define COL_DIM    0xFF445566
#define COL_VALUE  0xFFE0E0E0
#define ROW(n)     (8 * (n))

void frontend_logical_size(bool panel_visible, int* w, int* h) {
    *w = SCREEN_WIDTH + (panel_visible ? PANEL_LOGICAL_W : 0);
    *h = SCREEN_HEIGHT;
}

GamePage game_panel_next_page(GamePage page) {
    return (GamePage)((page + 1) % GAME_PAGE_COUNT);
}

GamePage game_panel_resolve_page(GamePage page, GameContext ctx) {
    if (page != GAME_PAGE_AUTO) return page;
    if (ctx == GAME_CTX_BATTLE_WILD || ctx == GAME_CTX_BATTLE_TRAINER) return GAME_PAGE_COUNT;
    return GAME_PAGE_ENCOUNTERS;
}

static const char* page_label(GamePage page) {
    switch (page) {
    case GAME_PAGE_PARTY: return "Party";
    case GAME_PAGE_ENCOUNTERS: return "Encounters";
    default: return "Auto";
    }
}

static const char* const k_stat_short[5] = {"Atk", "Def", "Spe", "SpA", "SpD"};
static const char* const k_iv_label[6] = {"HP", "At", "Df", "Sp", "SA", "SD"};

static const char* gender_mark(MonGender g) {
    return g == MON_GENDER_MALE ? "M" : g == MON_GENDER_FEMALE ? "F" : "";
}

static const char* eff_text(const GameMoveEff* e) {
    if (e->status_move) return "--";
    switch (e->quarters) {
    case 0: return "x0";
    case 1: return "x1/4";
    case 2: return "x1/2";
    case 4: return "x1";
    case 8: return "x2";
    case 16: return "x4";
    default: return "?";
    }
}

static const char* status_text(uint32_t s) {
    if (s & 0x07) return "SLP";
    if (s & 0x80) return "TOX";
    if (s & 0x08) return "PSN";
    if (s & 0x10) return "BRN";
    if (s & 0x20) return "FRZ";
    if (s & 0x40) return "PAR";
    return "";
}

typedef struct { uint32_t* c; int row; } Pen;

static bool pen_ok(const Pen* p) { return p->row < PANEL_CANVAS_H / 8; }

static void line(Pen* p, uint32_t color, const char* fmt, ...) {
    char buf[64];
    va_list ap;
    if (!pen_ok(p)) return;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    overlay_draw_text(p->c, PANEL_CANVAS_W, PANEL_CANVAS_H, 0, ROW(p->row), buf, color);
    p->row++;
}

static void nature_line(Pen* p, const char* nature, int8_t up, int8_t down) {
    if (up < 0 || down < 0) line(p, COL_VALUE, "  Nature %s", nature);
    else line(p, COL_VALUE, "  Nature %s +%s -%s", nature, k_stat_short[up], k_stat_short[down]);
}

static void iv_line(Pen* p, const char* tag, const uint8_t v[6]) {
    line(p, COL_VALUE, "  %s %s%u %s%u %s%u %s%u %s%u %s%u", tag, k_iv_label[0], v[0],
         k_iv_label[1], v[1], k_iv_label[2], v[2], k_iv_label[3], v[3], k_iv_label[4], v[4],
         k_iv_label[5], v[5]);
}

static void draw_battle(Pen* p, const GameSnapshot* s) {
    for (uint8_t i = 0; i < s->enemy_count; i++) {
        const GameEnemy* e = &s->enemies[i];
        line(p, COL_HEADER, "%s %s Lv%u %s %s", s->context == GAME_CTX_BATTLE_WILD ? "Wild" : "Foe",
             e->species, e->level, gender_mark(e->gender), status_text(e->status1));
        line(p, COL_VALUE, "  HP %u/%u  %s%s%s", e->hp, e->max_hp, e->type1,
             e->type2[0] ? "/" : "", e->type2);
        nature_line(p, e->nature, e->nature_up, e->nature_down);
        iv_line(p, "IV", e->ivs);
        line(p, COL_VALUE, "  Ability %s", e->ability);
        line(p, COL_VALUE, "  Item %s", e->item);
        for (uint8_t m = 0; m < e->eff_count; m++)
            line(p, COL_LABEL, "  %-12s %s", e->eff[m].move, eff_text(&e->eff[m]));
        p->row++;
    }
    if (s->context != GAME_CTX_BATTLE_WILD) return;
    line(p, COL_HEADER, "Catch chance");
    if (s->catch_count == 0) line(p, COL_DIM, "  No balls in bag");
    for (uint8_t i = 0; i < s->catch_count; i++) {
        const GameCatchRow* r = &s->catch_rows[i];
        if (r->permille < 0) line(p, COL_LABEL, "  %-14s n/a", r->ball);
        else line(p, COL_LABEL, "  %-14s x%-3u %3d.%d%%", r->ball, r->quantity,
                  r->permille / 10, r->permille % 10);
    }
}

static void draw_party(Pen* p, const GameSnapshot* s) {
    if (s->party_count == 0) line(p, COL_DIM, "No party");
    for (uint8_t i = 0; i < s->party_count; i++) {
        const GamePartyMon* m = &s->party[i];
        if (m->bad) { line(p, COL_DIM, "%u Bad data", i + 1); continue; }
        if (m->egg) {
            line(p, COL_HEADER, "%u Egg", i + 1);
            nature_line(p, m->nature, m->nature_up, m->nature_down);
            iv_line(p, "IV", m->ivs);
            continue;
        }
        line(p, COL_HEADER, "%u %s (%s) Lv%u %s", i + 1, m->nickname, m->species, m->level,
             gender_mark(m->gender));
        nature_line(p, m->nature, m->nature_up, m->nature_down);
        iv_line(p, "IV", m->ivs);
        iv_line(p, "EV", m->evs);
        line(p, COL_VALUE, "  HP %s %u  %s  Item %s", m->hp_type, m->hp_power, m->ability, m->item);
        line(p, COL_LABEL, "  Friend %u  Next %u", m->friendship, (unsigned)m->exp_to_next);
    }
}

static void draw_encounters(Pen* p, const GameSnapshot* s) {
    line(p, COL_LABEL, "Map %u:%u", s->map_group, s->map_num);
    if (!s->has_encounters) { line(p, COL_DIM, "No wild Pokemon here"); return; }
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        if (s->enc_count[k] == 0) continue;
        line(p, COL_HEADER, "%s", game_method_name((EncMethod)k));
        for (uint8_t i = 0; i < s->enc_count[k]; i++) {
            const GameEncRow* r = &s->enc[k][i];
            line(p, COL_VALUE, " %s %-10s Lv%u-%u %3u%%", r->caught ? "*" : " ", r->species,
                 r->min_level, r->max_level, r->percent);
        }
    }
    line(p, COL_DIM, "* caught. Not shown: outbreaks,");
    line(p, COL_DIM, "Feebas tiles, roamers, Altering Cave.");
}

void game_panel_render(uint32_t* canvas, const GameSnapshot* snap, GamePage page) {
    const int w = PANEL_CANVAS_W, h = PANEL_CANVAS_H;
    overlay_draw_rect(canvas, w, h, 0, 0, w, h, COL_BG);
    overlay_draw_textf(canvas, w, h, 0, ROW(0), COL_HEADER, "GAME INFO [%s]", page_label(page));
    overlay_draw_text(canvas, w, h, 0, ROW(1), "F9 hide  F10 page", COL_DIM);
    if (!snap->valid) {
        overlay_draw_text(canvas, w, h, 0, ROW(3), snap->reason, COL_LABEL);
        return;
    }
    Pen pen = { canvas, 3 };
    switch (game_panel_resolve_page(page, snap->context)) {
    case GAME_PAGE_PARTY: draw_party(&pen, snap); break;
    case GAME_PAGE_ENCOUNTERS: draw_encounters(&pen, snap); break;
    default: draw_battle(&pen, snap); break;
    }
}
