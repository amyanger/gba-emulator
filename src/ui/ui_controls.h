#ifndef UI_CONTROLS_H
#define UI_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>
#include "ui/ui_canvas.h"
#include "ui/ui_input.h"

/* Immediate-mode controls in the Companion style. Each call draws the control
 * and reports what the input did to it this frame. Focusable controls join
 * the Tab order in call order. */

bool ui_button(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, const char* label, bool enabled,
               bool primary);
/* Draws a switch at the left of r; the whole rect is clickable. True when flipped. */
bool ui_toggle(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, bool* on, bool enabled);
bool ui_sidebar_item(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, const char* label, bool selected);
/* A small rounded label that acts like a button (opens a pop-up). */
bool ui_chip(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, const char* label, uint32_t color);

typedef struct {
    char* buf;
    uint32_t cap, len, caret;
    bool all;          /* everything selected: the next edit replaces it */
    float scroll;      /* points, vertical */
} UiTextArea;

void ui_text_area_init(UiTextArea* t, char* buf, uint32_t cap);
void ui_text_area_set(UiTextArea* t, const char* text);
/* True when the text changed. Single-line areas turn newlines into spaces. */
bool ui_text_area(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, UiTextArea* t, const char* placeholder,
                  bool multiline);

/* Applies the wheel to *scroll when the mouse is over r, clamped to the content. */
void ui_scroll(UiInput* in, UiRect r, float content_h, float* scroll);

/* A list shown under anchor while in->popup_open. Returns the picked index,
 * UI_POPUP_DISMISSED on a click outside or Escape, or UI_POPUP_NONE. The caller
 * closes it (in->popup_open = false) on either result. */
#define UI_POPUP_NONE      (-1)
#define UI_POPUP_DISMISSED (-2)
int32_t ui_popup_list(UiCanvas* c, UiInput* in, UiRect anchor, const char* const* items, int32_t count,
                      int32_t selected);

#endif // UI_CONTROLS_H
