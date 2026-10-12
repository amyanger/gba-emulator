#ifndef GAME_PANEL_H
#define GAME_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/game.h"
#include "ui/ui_canvas.h"
#include "frontend/nuzlocke_session.h"

typedef enum { GAME_PAGE_AUTO, GAME_PAGE_PARTY, GAME_PAGE_ENCOUNTERS, GAME_PAGE_NUZLOCKE, GAME_PAGE_COUNT } GamePage;
/* Views inside the Nuzlocke page, switched with [ and ]. */
typedef enum { NUZ_SUB_ROUTES, NUZ_SUB_TEAM, NUZ_SUB_GRAVEYARD, NUZ_SUB_COUNT } NuzSubView;

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
/* Draws the panel into a canvas sized 600*density by 480*density pixels. nuz may be
 * NULL (no tracking); nuz_sub is a NuzSubView. */
void game_panel_draw(UiCanvas* c, const GameSnapshot* snap, GamePage page, uint8_t party_sel,
                     const NuzView* nuz, uint8_t nuz_sub);
uint8_t game_panel_nuz_sub(uint8_t sub, int delta);
/* First route row shown when only `fit` rows fit: keeps the current location in view. */
uint8_t game_panel_nuz_route_start(const NuzView* v, uint8_t fit);
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

typedef enum {
    MIRAGE_KEY_IGNORE,     /* not in the overworld or no Mirage data */
    MIRAGE_KEY_NOT_SET,    /* game clock not set yet */
    MIRAGE_KEY_ALREADY_UP, /* a party Pokemon matches today */
    MIRAGE_KEY_NO_MATCH,   /* nothing to jump to */
    MIRAGE_KEY_ARM,        /* first press: confirm with a second one */
    MIRAGE_KEY_JUMP        /* second press within 3 s: jump forecast[0].days */
} MirageKey;

typedef struct { uint32_t armed_ms; uint16_t days; } MirageArm;

/* J on the Route page. Arms on the first press; a second one within 3 s for the same
 * target day returns MIRAGE_KEY_JUMP and disarms. */
MirageKey game_panel_mirage_key(const GameSnapshot* s, uint32_t now_ms, MirageArm* arm);

#endif // GAME_PANEL_H
