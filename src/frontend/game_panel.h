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
/* Switching to Auto when a battle starts. Zero-initialise; clear `restore` when the
 * user changes the page by hand so their choice survives the end of the battle. */
typedef struct {
    bool in_battle;     /* last non-NONE context seen was a battle */
    bool restore;       /* put saved_page back when the battle ends */
    uint8_t saved_page; /* GamePage the user had before the battle */
} PanelBattleFollow;
/* Returns the page to show after the context becomes ctx. GAME_CTX_NONE is ignored. */
GamePage game_panel_follow_battle(GamePage page, GameContext ctx, PanelBattleFollow* f);
/* Draws the panel into a canvas sized 600*density by 480*density pixels. */
void game_panel_draw(UiCanvas* c, const GameSnapshot* snap, GamePage page, uint8_t party_sel);
/* Moves the party selection by delta; wraps, and returns 0 when the party is empty. */
uint8_t game_panel_select(uint8_t sel, int delta, uint8_t party_count);
/* "WATER GUN" -> "Water Gun". The whole words POKE, HP and PP become "Poké", "HP" and
 * "PP", so the ROM's "POKe BALL" (decoded as "POKE BALL") reads "Poké Ball"; longer
 * words such as "POKEMON" are only title-cased. */
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
