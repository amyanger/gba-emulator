#ifndef SEARCH_VIEW_H
#define SEARCH_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include "game/species_search.h"

/* View model of the Search page in the Emerald Companion panel: query text,
 * focus, list selection and scroll. Pure state, no SDL, no drawing. The owner
 * runs the query and reports the hit count with search_view_set_results. */

typedef enum {
    SV_KEY_UP, SV_KEY_DOWN, SV_KEY_HOME, SV_KEY_END, SV_KEY_PAGE_UP, SV_KEY_PAGE_DOWN,
    SV_KEY_BACKSPACE, SV_KEY_CLEAR, SV_KEY_ENTER, SV_KEY_ESCAPE, SV_KEY_OTHER
} SvKey;

/* SV_CHANGED: redraw (and re-run the query if the text changed).
 * SV_UNFOCUS: the owner should release text input. */
typedef enum { SV_NONE, SV_CHANGED, SV_UNFOCUS } SvAction;

typedef struct {
    char query[SEARCH_QUERY_MAX];   /* NUL-terminated, at most 15 chars */
    bool focused;
    uint16_t sel, list_top, detail_top;
    uint16_t result_count;          /* set by the owner after each query */
    uint16_t detail_lines, detail_visible;  /* set by the owner; detail_top <= lines - visible */
    uint32_t version;               /* bumped on every visible change */
} SearchView;

/* Panel-point geometry. The drawer must use search_view_layout() so clicks and
 * wheel events hit what is drawn. */
typedef struct {
    float box_x, box_y, box_w, box_h;
    float list_x, list_y, list_w, row_h;
    float detail_x, detail_y, detail_w, detail_h;
    uint16_t list_rows;             /* whole rows that fit */
} SearchLayout;

void search_view_init(SearchView* sv);
/* Appends the characters in [A-Za-z0-9 .'-], dropping the rest and anything past 15. */
SvAction search_view_text(SearchView* sv, const char* utf8);
SvAction search_view_key(SearchView* sv, SvKey key);
/* Clamps sel and list_top to the new count; detail_top resets only if sel changes. */
void search_view_set_results(SearchView* sv, uint16_t count);
/* Total detail lines and how many fit; clamps detail_top to max(0, lines - visible). */
void search_view_set_detail_lines(SearchView* sv, uint16_t lines, uint16_t visible);
void search_view_set_focus(SearchView* sv, bool focused);
/* x,y in panel points. The box focuses, a row selects (focus unchanged), anywhere else unfocuses. */
SvAction search_view_click(SearchView* sv, const SearchLayout* l, float x, float y);
/* dy in notches, positive scrolls down. Over the list it moves list_top (sel stays),
 * over the detail column it moves detail_top (clamped to the detail range). */
SvAction search_view_wheel(SearchView* sv, const SearchLayout* l, float x, float y, int dy);
/* Adds SDL wheel travel (positive = up) to *acc; returns whole notches, positive = down,
 * and keeps the remainder so trackpad scrolling is not lost or amplified. */
int search_wheel_notches(float* acc, float sdl_y);
/* Which wheel region (0 none, 1 list, 2 detail) a point is over. */
int search_view_wheel_region(const SearchLayout* l, float x, float y);
void search_view_layout(SearchLayout* out);

/* Leaving focus by key: that scancode's keydowns are ignored until its keyup, so a
 * held Enter does not press Start and a held Esc does nothing. Zero-initialise;
 * swallow_text drops the text of the key that focused the box. */
typedef struct { int32_t held_scancode; bool swallow_text; } SearchFocusGuard;
void search_guard_leave(SearchFocusGuard* g, int32_t scancode);
bool search_guard_blocks_keydown(SearchFocusGuard* g, int32_t scancode);  /* true while held */
void search_guard_keyup(SearchFocusGuard* g, int32_t scancode);

/* What a keydown does while the box has focus. Arguments are SDL scancodes and
 * key modifiers, as ints so this header stays free of SDL. */
typedef enum {
    SV_ROUTE_SWALLOW,       /* eaten: never reaches the game or a hotkey */
    SV_ROUTE_PASS,          /* normal handling, focus kept (F1-F4, F11, F12) */
    SV_ROUTE_UNFOCUS_PASS,  /* drop focus, then normal handling (F6, F7, F9, F10) */
    SV_ROUTE_KEY,           /* *key goes to search_view_key */
    SV_ROUTE_PASTE          /* clipboard text goes to search_view_text */
} SvRoute;
SvRoute search_route_keydown(int32_t scancode, uint16_t mod, bool repeat, SvKey* key);
/* "/" (by keycode, so any layout) focuses the box on the shown Search page unless
 * it is a game key. */
bool search_focus_key(int32_t keycode, bool repeat, bool page_shown, bool game_key);

#endif // SEARCH_VIEW_H
