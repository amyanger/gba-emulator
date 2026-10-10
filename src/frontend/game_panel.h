#ifndef GAME_PANEL_H
#define GAME_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/game.h"
#include "ui/ui_canvas.h"

typedef enum { GAME_PAGE_AUTO, GAME_PAGE_PARTY, GAME_PAGE_ENCOUNTERS, GAME_PAGE_COUNT } GamePage;

GamePage game_panel_next_page(GamePage page);
/* GAME_PAGE_COUNT means "show the battle page". */
GamePage game_panel_resolve_page(GamePage page, GameContext ctx);
/* Draws the panel into a canvas sized 600*density by 480*density pixels. */
void game_panel_draw(UiCanvas* c, const GameSnapshot* snap, GamePage page, uint8_t party_sel);
/* Moves the party selection by delta; wraps, and returns 0 when the party is empty. */
uint8_t game_panel_select(uint8_t sel, int delta, uint8_t party_count);
/* "POKé BALL" -> "Poké Ball". */
void game_panel_title_case(const char* in, char* out, size_t out_size);
/* How the Route page fits its encounter list into the space above the footer.
 * Exposed for tests. */
typedef struct {
    float section_adv, row_h, gap;   /* points */
    bool two_columns;                /* rod methods flow into a right-hand column */
    uint8_t shown[ENC_METHOD_COUNT]; /* whole rows drawn per method */
    uint8_t more[2];                 /* rows left out per column, drawn as "+N more" */
    float height[2], avail;          /* column heights including any "+N more" line */
} RouteLayout;
void game_panel_route_layout(const GameSnapshot* s, RouteLayout* out);

#endif // GAME_PANEL_H
