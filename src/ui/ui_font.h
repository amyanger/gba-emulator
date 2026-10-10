#ifndef UI_FONT_H
#define UI_FONT_H

#include <stdbool.h>
#include <stdint.h>
#include "ui/ui_canvas.h"

typedef enum { UI_FONT_REGULAR, UI_FONT_SEMIBOLD, UI_FONT_COUNT } UiFont;
typedef enum { UI_ALIGN_LEFT, UI_ALIGN_CENTER, UI_ALIGN_RIGHT } UiAlign;

bool  ui_font_init(void);                       /* idempotent; false if the embedded data fails to parse */
float ui_text_width(const UiCanvas* c, UiFont f, float size_pt, const char* utf8);   /* points */
float ui_text_ascent(const UiCanvas* c, UiFont f, float size_pt);                    /* points */
/* Draws with the top of the line box at y (points). Truncates with "…" so the
 * text fits max_w points (max_w <= 0 means no limit). Returns the drawn width. */
float ui_text(UiCanvas* c, UiFont f, float size_pt, float x, float y, float max_w,
              UiAlign align, uint32_t argb, const char* utf8);
void  ui_font_cache_clear(void);
uint32_t ui_font_cache_count(void);             /* for tests */

#endif // UI_FONT_H
