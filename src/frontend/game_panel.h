#ifndef GAME_PANEL_H
#define GAME_PANEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game/game.h"
#include "ui/ui_canvas.h"
#include "frontend/nuzlocke_session.h"
#include "frontend/search_view.h"
#include "frontend/run_session.h"

typedef enum {
    GAME_PAGE_AUTO, GAME_PAGE_PARTY, GAME_PAGE_ENCOUNTERS, GAME_PAGE_NUZLOCKE, GAME_PAGE_BERRIES,
    GAME_PAGE_SEARCH, GAME_PAGE_RUN, GAME_PAGE_COUNT
} GamePage;
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
    bool hold;          /* while true (typing a search), battles only update in_battle */
} PanelBattleFollow;
/* Returns the page to show after the context becomes ctx. GAME_CTX_NONE is ignored. */
GamePage game_panel_follow_battle(GamePage page, GameContext ctx, PanelBattleFollow* f);
/* What the Search page draws. The owner runs the query and the detail lookup. */
typedef struct {
    const SearchView* view;
    const SpeciesIndex* index;
    const uint16_t* results;            /* view->result_count species ids */
    const SpeciesDetail* detail;        /* NULL when nothing is selected */
    bool caret_on;                      /* blink phase; drawn only while focused */
} GamePanelSearch;
/* Draws the panel into a canvas sized 600*density by 480*density pixels. nuz may be
 * NULL (no tracking); nuz_sub is a NuzSubView. berry_sel picks the open place on the
 * Berries page (0 = the first, which is the current place when it has trees). search
 * may be NULL; the Search page then says it is unavailable. run may be NULL; run_scroll
 * scrolls its list and run_details shows the Details sheet. */
void game_panel_draw(UiCanvas* c, const GameSnapshot* snap, GamePage page, uint8_t party_sel,
                     const NuzView* nuz, uint8_t nuz_sub, uint8_t berry_sel, bool berry_alerts,
                     const GamePanelSearch* search, const RunView* run, uint8_t run_scroll,
                     bool run_details);
/* Lines in the Search page's scrolling detail body and how many the drawer shows at once
 * (one fewer than fit when it needs the "N more below" footer). Feed both to
 * search_view_set_detail_lines. */
uint16_t game_panel_search_detail_lines(const SpeciesDetail* d, uint16_t* visible);
/* Tab bar key hint for the shown page (GAME_PAGE_COUNT = battle). nuz and run may be NULL. */
const char* game_panel_hint(GamePage shown, const NuzView* nuz, bool search_focused, const RunView* run,
                            bool run_details);
/* Width in points of the tab labels, plus the Auto badge when auto_badge. */
float game_panel_tabs_width(const UiCanvas* c, GamePage shown, bool auto_badge);

/* Berries page helpers, exposed for tests and the frontend. */
/* "40m", "5h 10m", "1d 9h". */
void game_panel_berry_time(uint32_t minutes, char* out, size_t size);
/* Places with trees: the current place first, then most urgent, then by name. Returns the count. */
uint8_t game_panel_berry_places(const BerryView* v, uint8_t order[BERRY_PLACE_MAX]);
/* Tree indexes that need the player (falling, ripe, water; untouched trees left out), most
 * urgent first. Returns the count. */
uint8_t game_panel_berry_needs(const BerryView* v, uint8_t idx[BERRY_VIEW_MAX]);
typedef struct {
    uint8_t place;      /* open place, BERRY_PLACE_NONE when no place has trees */
    uint8_t places;     /* places with trees */
    uint8_t needs;      /* "Needs you" rows shown */
    uint8_t needs_more; /* the rest, as "+N more" */
    uint8_t rows;       /* tree rows shown for the open place */
    uint8_t more;       /* the rest, as "+N more" */
    float list_y;       /* first tree row */
    float list_end;     /* bottom of the open place's block */
    uint8_t others;      /* other places drawn as one-line summaries */
    uint8_t others_more; /* the rest, as "+N more places" */
    float others_end;    /* bottom of that list, at most the list end */
} BerryPageLayout;
void game_panel_berry_layout(const BerryView* v, uint8_t sel, BerryPageLayout* out);
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
