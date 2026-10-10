#include "frontend/game_panel.h"
#include "frontend/overlay_draw.h"
#include "common.h"

#define COL_BG     0xFF0D0D36
#define COL_HEADER 0xFF00FFFF
#define COL_LABEL  0xFF88AACC
#define COL_DIM    0xFF445566
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

void game_panel_render(uint32_t* canvas, const GameSnapshot* snap, GamePage page) {
    const int w = PANEL_CANVAS_W, h = PANEL_CANVAS_H;
    overlay_draw_rect(canvas, w, h, 0, 0, w, h, COL_BG);
    overlay_draw_textf(canvas, w, h, 0, ROW(0), COL_HEADER, "GAME INFO [%s]", page_label(page));
    overlay_draw_text(canvas, w, h, 0, ROW(1), "F9 hide  F10 page", COL_DIM);
    if (!snap->valid) {
        overlay_draw_text(canvas, w, h, 0, ROW(3), snap->reason, COL_LABEL);
        return;
    }
}
