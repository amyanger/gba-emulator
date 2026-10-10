#include "ui/ui_input.h"

#include <string.h>

uint32_t ui_id(const char* name, uint32_t index) {
    uint32_t h = 2166136261u;   /* FNV-1a */
    for (const char* p = name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    h = (h ^ index) * 16777619u;
    return h ? h : 1;
}

void ui_input_key(UiInput* in, UiKey key) {
    if (in->key_count < UI_INPUT_KEYS) in->keys[in->key_count++] = key;
}

void ui_input_text(UiInput* in, const char* utf8) {
    size_t n = strlen(utf8);
    if (in->text_len + n >= sizeof(in->text)) n = sizeof(in->text) - 1 - in->text_len;
    memcpy(in->text + in->text_len, utf8, n);
    in->text_len += (uint32_t)n;
    in->text[in->text_len] = '\0';
}

bool ui_input_has_key(const UiInput* in, UiKey key) {
    for (uint32_t i = 0; i < in->key_count; i++)
        if (in->keys[i] == key) return true;
    return false;
}

bool ui_input_pending(const UiInput* in) {
    return in->pressed || in->released || in->wheel != 0.0f || in->key_count > 0 || in->text_len > 0;
}

void ui_input_frame_begin(UiInput* in) {
    in->order_count = 0;
    in->hot = 0;
    in->blocked = in->popup_open;
}

static int32_t focus_index(const UiInput* in) {
    for (uint32_t i = 0; i < in->order_count; i++)
        if (in->order[i] == in->focus) return (int32_t)i;
    return -1;
}

void ui_input_frame_end(UiInput* in) {
    if (in->order_count > 0) {
        int32_t i = focus_index(in);
        int32_t n = (int32_t)in->order_count;
        if (ui_input_has_key(in, UI_KEY_TAB)) {
            in->focus = in->order[i < 0 ? 0 : (i + 1) % n];
            in->caret_ms = in->now_ms;
        } else if (ui_input_has_key(in, UI_KEY_SHIFT_TAB)) {
            in->focus = in->order[i < 0 ? n - 1 : (i - 1 + n) % n];
            in->caret_ms = in->now_ms;
        }
    }
    if (!in->down) in->active = 0;
    in->pressed = in->released = false;
    in->wheel = 0.0f;
    in->key_count = 0;
    in->text_len = 0;
    in->text[0] = '\0';
}
