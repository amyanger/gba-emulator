#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "ui/ui_canvas.h"

/* Input for immediate-mode widgets. Pure C: the frontend fills it from SDL
 * events, widgets read it while drawing. Mouse is in canvas points. */

typedef enum {
    UI_KEY_NONE,
    UI_KEY_TAB, UI_KEY_SHIFT_TAB,
    UI_KEY_UP, UI_KEY_DOWN, UI_KEY_LEFT, UI_KEY_RIGHT, UI_KEY_HOME, UI_KEY_END,
    UI_KEY_ENTER, UI_KEY_ESCAPE, UI_KEY_BACKSPACE, UI_KEY_DELETE, UI_KEY_SPACE,
    UI_KEY_SELECT_ALL
} UiKey;

#define UI_INPUT_KEYS  16
#define UI_INPUT_TEXT  4096
#define UI_FOCUS_MAX   96
#define UI_CARET_MS    530u

typedef struct {
    /* Mouse: position persists; pressed/released/wheel are this frame only. */
    float mx, my;
    bool down, pressed, released;
    float wheel;                     /* notches, + = up */
    UiKey keys[UI_INPUT_KEYS];
    uint32_t key_count;
    char text[UI_INPUT_TEXT];        /* typed or pasted UTF-8 this frame */
    uint32_t text_len;

    uint32_t hot, active, focus;     /* widget ids, 0 = none */
    uint32_t order[UI_FOCUS_MAX];    /* focusable ids in call order, this frame */
    uint32_t order_count;

    bool popup_open;                 /* a pop-up owns the mouse */
    UiRect popup_rect;
    bool blocked;                    /* ordinary widgets ignore the mouse this frame */

    uint32_t now_ms, caret_ms;       /* caret is visible for UI_CARET_MS after caret_ms, then blinks */
} UiInput;

uint32_t ui_id(const char* name, uint32_t index);

void ui_input_key(UiInput* in, UiKey key);
void ui_input_text(UiInput* in, const char* utf8);
bool ui_input_has_key(const UiInput* in, UiKey key);
/* True if anything this frame needs a redraw (mouse, keys, text). */
bool ui_input_pending(const UiInput* in);

void ui_input_frame_begin(UiInput* in);
/* Applies Tab/Shift+Tab focus moves and clears this frame's events. */
void ui_input_frame_end(UiInput* in);

#endif // UI_INPUT_H
