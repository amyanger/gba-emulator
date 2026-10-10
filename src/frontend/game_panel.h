#ifndef GAME_PANEL_H
#define GAME_PANEL_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game.h"

/* Panel geometry: 200 logical pixels beside the 240x160 game, drawn on a
 * 2x canvas so the 8x8 font gives 50 columns by 40 rows. */
#define PANEL_LOGICAL_W 200
#define PANEL_CANVAS_W  400
#define PANEL_CANVAS_H  320

typedef enum { GAME_PAGE_AUTO, GAME_PAGE_PARTY, GAME_PAGE_ENCOUNTERS, GAME_PAGE_COUNT } GamePage;

void frontend_logical_size(bool panel_visible, int* w, int* h);
GamePage game_panel_next_page(GamePage page);
/* GAME_PAGE_COUNT means "show the battle page". */
GamePage game_panel_resolve_page(GamePage page, GameContext ctx);
void game_panel_render(uint32_t* canvas, const GameSnapshot* snap, GamePage page);

#endif // GAME_PANEL_H
