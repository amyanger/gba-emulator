#include "frontend/search_view.h"
#include <string.h>
/* Only the scancode and keycode constants: no SDL calls here. */
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_scancode.h>

/* Panel is 600x480 pt; the content area starts under ui_tabs(.., 12.0f, ..) at y = 42. */
#define PANEL_H       480.0f
#define PANEL_MARGIN  4.0f
#define CONTENT_TOP   42.0f
#define LEFT          16.0f
#define RIGHT         584.0f
#define COL2_X        308.0f
#define LEFT_COL_END  292.0f
#define BOX_H         28.0f
#define BOX_GAP       8.0f
#define ROW_H         22.0f

#define PAGE_STEP     5
#define WHEEL_STEP    3

void search_view_layout(SearchLayout* out) {
    out->box_x = LEFT;
    out->box_y = CONTENT_TOP;
    out->box_w = LEFT_COL_END - LEFT;
    out->box_h = BOX_H;
    out->list_x = LEFT;
    out->list_y = CONTENT_TOP + BOX_H + BOX_GAP;
    out->list_w = LEFT_COL_END - LEFT;
    out->row_h = ROW_H;
    out->list_rows = (uint16_t)((PANEL_H - PANEL_MARGIN - out->list_y) / ROW_H);
    out->detail_x = COL2_X;
    out->detail_y = CONTENT_TOP;
    out->detail_w = RIGHT - COL2_X;
    out->detail_h = PANEL_H - PANEL_MARGIN - CONTENT_TOP;
}

void search_view_init(SearchView* sv) {
    memset(sv, 0, sizeof(*sv));
}

static bool char_ok(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == ' ' || c == '.' || c == '\'' || c == '-';
}

static void reset_position(SearchView* sv) {
    sv->sel = 0;
    sv->list_top = 0;
    sv->detail_top = 0;
}

SvAction search_view_text(SearchView* sv, const char* utf8) {
    size_t len = strlen(sv->query);
    bool added = false;
    for (; *utf8 && len < SEARCH_QUERY_MAX - 1; utf8++) {
        if (!char_ok(*utf8)) continue;
        sv->query[len++] = *utf8;
        added = true;
    }
    sv->query[len] = '\0';
    if (!added) return SV_NONE;
    reset_position(sv);
    sv->version++;
    return SV_CHANGED;
}

static void keep_sel_visible(SearchView* sv, uint16_t rows) {
    if (sv->sel < sv->list_top) sv->list_top = sv->sel;
    else if (sv->sel >= sv->list_top + rows) sv->list_top = (uint16_t)(sv->sel - rows + 1);
}

static uint16_t max_detail_top(const SearchView* sv) {
    return sv->detail_lines > sv->detail_visible ? (uint16_t)(sv->detail_lines - sv->detail_visible) : 0;
}

static SvAction move_detail(SearchView* sv, int32_t delta) {
    int32_t top = (int32_t)sv->detail_top + delta;
    int32_t max = max_detail_top(sv);
    if (top < 0) top = 0;
    if (top > max) top = max;
    if (top == sv->detail_top) return SV_NONE;
    sv->detail_top = (uint16_t)top;
    sv->version++;
    return SV_CHANGED;
}

void search_view_set_detail_lines(SearchView* sv, uint16_t lines, uint16_t visible) {
    sv->detail_lines = lines;
    sv->detail_visible = visible;
    if (sv->detail_top > max_detail_top(sv)) {
        sv->detail_top = max_detail_top(sv);
        sv->version++;
    }
}

static SvAction select_row(SearchView* sv, uint16_t sel) {
    SearchLayout l;
    if (sel == sv->sel) return SV_NONE;
    search_view_layout(&l);
    sv->sel = sel;
    sv->detail_top = 0;
    keep_sel_visible(sv, l.list_rows);
    sv->version++;
    return SV_CHANGED;
}

static SvAction clear_query(SearchView* sv) {
    if (!sv->query[0]) return SV_NONE;
    sv->query[0] = '\0';
    reset_position(sv);
    sv->version++;
    return SV_CHANGED;
}

SvAction search_view_key(SearchView* sv, SvKey key) {
    uint16_t n = sv->result_count;
    switch (key) {
    case SV_KEY_UP:   return n && sv->sel > 0 ? select_row(sv, (uint16_t)(sv->sel - 1)) : SV_NONE;
    case SV_KEY_DOWN: return n && sv->sel + 1 < n ? select_row(sv, (uint16_t)(sv->sel + 1)) : SV_NONE;
    case SV_KEY_HOME: return n ? select_row(sv, 0) : SV_NONE;
    case SV_KEY_END:  return n ? select_row(sv, (uint16_t)(n - 1)) : SV_NONE;
    case SV_KEY_PAGE_UP:   return move_detail(sv, -PAGE_STEP);
    case SV_KEY_PAGE_DOWN: return move_detail(sv, PAGE_STEP);
    case SV_KEY_BACKSPACE: {
        size_t len = strlen(sv->query);
        if (!len) return SV_NONE;
        sv->query[len - 1] = '\0';
        reset_position(sv);
        sv->version++;
        return SV_CHANGED;
    }
    case SV_KEY_CLEAR:  return clear_query(sv);
    case SV_KEY_ENTER:  return SV_UNFOCUS;
    case SV_KEY_ESCAPE: return sv->query[0] ? clear_query(sv) : SV_UNFOCUS;
    case SV_KEY_OTHER:  break;
    }
    return SV_NONE;
}

static uint16_t max_list_top(uint16_t count, uint16_t rows) {
    return count > rows ? (uint16_t)(count - rows) : 0;
}

void search_view_set_results(SearchView* sv, uint16_t count) {
    SearchLayout l;
    SearchView old = *sv;
    search_view_layout(&l);
    sv->result_count = count;
    if (count == 0) {
        sv->sel = 0;
        sv->list_top = 0;
    } else {
        if (sv->sel >= count) sv->sel = (uint16_t)(count - 1);
        if (sv->list_top > max_list_top(count, l.list_rows)) sv->list_top = max_list_top(count, l.list_rows);
        keep_sel_visible(sv, l.list_rows);
    }
    if (sv->sel != old.sel) sv->detail_top = 0;
    if (sv->result_count != old.result_count || sv->sel != old.sel || sv->list_top != old.list_top)
        sv->version++;
}

void search_view_set_focus(SearchView* sv, bool focused) {
    if (sv->focused == focused) return;
    sv->focused = focused;
    sv->version++;
}

static bool in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

SvAction search_view_click(SearchView* sv, const SearchLayout* l, float x, float y) {
    if (in_rect(x, y, l->box_x, l->box_y, l->box_w, l->box_h)) {
        if (sv->focused) return SV_NONE;
        search_view_set_focus(sv, true);
        return SV_CHANGED;
    }
    if (in_rect(x, y, l->list_x, l->list_y, l->list_w, l->list_rows * l->row_h)) {
        uint32_t row = (uint32_t)sv->list_top + (uint32_t)((y - l->list_y) / l->row_h);
        if (row < sv->result_count) return select_row(sv, (uint16_t)row);
        return SV_NONE;
    }
    if (!sv->focused) return SV_NONE;
    search_view_set_focus(sv, false);
    return SV_UNFOCUS;
}

int search_view_wheel_region(const SearchLayout* l, float x, float y) {
    if (in_rect(x, y, l->list_x, l->list_y, l->list_w, l->list_rows * l->row_h)) return 1;
    if (in_rect(x, y, l->detail_x, l->detail_y, l->detail_w, l->detail_h)) return 2;
    return 0;
}

int search_wheel_notches(float* acc, float sdl_y) {
    *acc -= sdl_y;
    int n = (int)*acc;
    *acc -= (float)n;
    return n;
}

SvAction search_view_wheel(SearchView* sv, const SearchLayout* l, float x, float y, int dy) {
    if (in_rect(x, y, l->list_x, l->list_y, l->list_w, l->list_rows * l->row_h)) {
        int32_t top = (int32_t)sv->list_top + dy * WHEEL_STEP;
        int32_t max = max_list_top(sv->result_count, l->list_rows);
        if (top < 0) top = 0;
        if (top > max) top = max;
        if (top == sv->list_top) return SV_NONE;
        sv->list_top = (uint16_t)top;
        sv->version++;
        return SV_CHANGED;
    }
    if (in_rect(x, y, l->detail_x, l->detail_y, l->detail_w, l->detail_h)) {
        return move_detail(sv, dy * WHEEL_STEP);
    }
    return SV_NONE;
}

void search_guard_leave(SearchFocusGuard* g, int32_t scancode) {
    g->held_scancode = scancode;
}

bool search_guard_blocks_keydown(SearchFocusGuard* g, int32_t scancode) {
    return g->held_scancode > 0 && g->held_scancode == scancode;
}

void search_guard_keyup(SearchFocusGuard* g, int32_t scancode) {
    if (g->held_scancode == scancode) g->held_scancode = 0;
}

SvRoute search_route_keydown(int32_t scancode, uint16_t mod, bool repeat, SvKey* key) {
    /* Ctrl+Alt is AltGr on Windows, which types characters. */
    bool cmd = (mod & (KMOD_CTRL | KMOD_GUI)) != 0 && (mod & KMOD_ALT) == 0;
    switch (scancode) {
    case SDL_SCANCODE_F1: case SDL_SCANCODE_F2: case SDL_SCANCODE_F3: case SDL_SCANCODE_F4:
    case SDL_SCANCODE_F11: case SDL_SCANCODE_F12:
        return SV_ROUTE_PASS;
    case SDL_SCANCODE_F6: case SDL_SCANCODE_F7: case SDL_SCANCODE_F9: case SDL_SCANCODE_F10:
        return repeat ? SV_ROUTE_SWALLOW : SV_ROUTE_UNFOCUS_PASS;
    /* Held keys repeat; Enter, Esc and paste act once per press. */
    case SDL_SCANCODE_UP:        *key = SV_KEY_UP; return SV_ROUTE_KEY;
    case SDL_SCANCODE_DOWN:      *key = SV_KEY_DOWN; return SV_ROUTE_KEY;
    case SDL_SCANCODE_HOME:      *key = SV_KEY_HOME; return SV_ROUTE_KEY;
    case SDL_SCANCODE_END:       *key = SV_KEY_END; return SV_ROUTE_KEY;
    case SDL_SCANCODE_PAGEUP:    *key = SV_KEY_PAGE_UP; return SV_ROUTE_KEY;
    case SDL_SCANCODE_PAGEDOWN:  *key = SV_KEY_PAGE_DOWN; return SV_ROUTE_KEY;
    case SDL_SCANCODE_BACKSPACE: *key = cmd ? SV_KEY_CLEAR : SV_KEY_BACKSPACE; return SV_ROUTE_KEY;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
        if (repeat) return SV_ROUTE_SWALLOW;
        *key = SV_KEY_ENTER;
        return SV_ROUTE_KEY;
    case SDL_SCANCODE_ESCAPE:
        if (repeat) return SV_ROUTE_SWALLOW;
        *key = SV_KEY_ESCAPE;
        return SV_ROUTE_KEY;
    case SDL_SCANCODE_V:
        return cmd && !repeat ? SV_ROUTE_PASTE : SV_ROUTE_SWALLOW;
    default:
        return SV_ROUTE_SWALLOW;
    }
}

bool search_focus_key(int32_t keycode, bool repeat, bool page_shown, bool game_key) {
    return keycode == SDLK_SLASH && !repeat && page_shown && !game_key;
}
