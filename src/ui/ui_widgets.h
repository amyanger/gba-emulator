#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdbool.h>
#include <stdint.h>
#include "ui/ui_canvas.h"

float ui_pill(UiCanvas* c, float x, float y, const char* text, uint32_t bg);   /* returns width */
void  ui_bar(UiCanvas* c, UiRect r, float frac, uint32_t fill);                /* track + fill, rounded */
void  ui_gender(UiCanvas* c, float x, float y, float size, bool female);       /* drawn male/female mark */
void  ui_check(UiCanvas* c, float x, float y, float size, uint32_t argb);
/* Tab bar across the top: labels, the active index, an optional badge and a
 * right-aligned hint. Returns the y (points) just below the bar. */
float ui_tabs(UiCanvas* c, float x, float y, float w, const char* const* labels, int count,
              int active, const char* badge, const char* hint);
void  ui_section_label(UiCanvas* c, float x, float y, const char* upper_text);

#endif // UI_WIDGETS_H
