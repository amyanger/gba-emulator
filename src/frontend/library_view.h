#ifndef LIBRARY_VIEW_H
#define LIBRARY_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "frontend/library.h"
#include "ui/ui_canvas.h"

/* The library screen (layout B): "Continue playing" banner plus a table of
 * every game. Drawing and input handling only; no SDL. Coordinates are points. */

typedef enum {
    LIB_ACT_NONE, LIB_ACT_REDRAW, LIB_ACT_PLAY, LIB_ACT_OPEN_FILE, LIB_ACT_CHOOSE_FOLDER, LIB_ACT_QUIT
} LibAction;

typedef enum {
    LIB_KEY_UP, LIB_KEY_DOWN, LIB_KEY_PAGE_UP, LIB_KEY_PAGE_DOWN, LIB_KEY_HOME, LIB_KEY_END,
    LIB_KEY_ENTER, LIB_KEY_ESCAPE, LIB_KEY_OPEN
} LibKey;

/* What the mouse is over. */
typedef enum { LIB_HIT_NONE, LIB_HIT_BANNER, LIB_HIT_PLAY, LIB_HIT_ROW, LIB_HIT_FOLDER, LIB_HIT_OPEN } LibHit;

#define LIB_SEL_BANNER (-1)

typedef struct {
    float w, h;               /* area in points; set before handling input or drawing */
    int32_t sel;              /* LIB_SEL_BANNER or a games[] index */
    int32_t scroll;           /* first table row shown */
    uint8_t hover;            /* LibHit */
    int32_t hover_row;
    uint32_t last_click_ms;
    int32_t last_click_row;
    char error[160];          /* red line under the top bar; "" = none */
} LibView;

typedef struct {
    UiRect folder_btn, open_btn;
    float error_y;
    bool banner;
    UiRect banner_rect, play_btn;
    float section_y;
    float table_y;            /* first row */
    float row_h;
    int32_t rows;             /* rows that fit */
    float foot_y;
} LibLayout;

void library_view_reset(LibView* v, const Library* lib, float w, float h);
void library_view_layout(const LibView* v, const Library* lib, LibLayout* l);
LibAction library_view_key(LibView* v, const Library* lib, LibKey key);
/* Type to find: jumps to the next game whose name starts with c. */
LibAction library_view_char(LibView* v, const Library* lib, char c);
LibAction library_view_mouse_move(LibView* v, const Library* lib, float x, float y);
LibAction library_view_click(LibView* v, const Library* lib, float x, float y, uint32_t now_ms);
LibAction library_view_wheel(LibView* v, const Library* lib, float notches);
/* The game Enter or Play would start, NULL when nothing is selected. */
const char* library_view_selected_path(const LibView* v, const Library* lib);
void library_view_draw(UiCanvas* c, const LibView* v, const Library* lib, int64_t now);

void library_format_played(int64_t played, int64_t now, char* out, size_t out_size);
void library_format_size(uint32_t bytes, char* out, size_t out_size);

#endif // LIBRARY_VIEW_H
