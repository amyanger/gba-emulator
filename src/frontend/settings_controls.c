#include "frontend/settings.h"
#include "input/input.h"
#include "input/keymap.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_keyboard.h>

#define ROW_H     22.0f
#define ROW_STEP  25.0f   /* 22 pt rows, 3 pt gap: 12 rows plus Reset fit in the 440 pt window */
#define KEY_X     120.0f
#define PAD_X     290.0f
#define CHIP_W    150.0f
#define FULL_W    450.0f
#define RESET_W   140.0f
#define RESET_H   26.0f

static const char* const k_row_label[CONTROLS_ROWS] = {"A",  "B",    "L",    "R",     "Start",   "Select",
                                                       "Up", "Down", "Left", "Right", "Turbo A", "Turbo B"};
static const uint16_t k_row_action[CONTROLS_ROWS] = {KEY_A,  KEY_B,    KEY_L,    KEY_R,     KEY_START,      KEY_SELECT,
                                                     KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEYMAP_TURBO_A, KEYMAP_TURBO_B};

/* SDL_CONTROLLER_BUTTON_A .. DPAD_RIGHT; later buttons show as "Button %d". */
static const char* const k_pad_label[] = {"A",           "B",          "X",     "Y",  "Back", "Guide",      "Start",
                                          "Left Stick",  "Right Stick", "LB",   "RB", "D-Pad Up", "D-Pad Down",
                                          "D-Pad Left", "D-Pad Right"};

static const char k_unbound[] = "Unbound";

static void set_status(SettingsControlsPage* p, const char* msg, bool warn) {
    snprintf(p->status, sizeof(p->status), "%s", msg);
    p->status_warn = warn;
}

static void stop_listening(SettingsControlsPage* p) {
    p->listen_row = -1;
    set_status(p, "", false);
}

bool settings_controls_listening(const SettingsControlsPage* p) {
    return p->listen_row >= 0 && p->listen_row < CONTROLS_ROWS;
}

const char* settings_controls_reserved(int32_t sc) {
    if ((sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)) return "F-keys run emulator tools";
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return "Number keys pick save slots";
    switch (sc) {
    case SDL_SCANCODE_TAB:       return "Tab is fast-forward";
    case SDL_SCANCODE_GRAVE:     return "` toggles fast-forward";
    case SDL_SCANCODE_BACKSLASH: return "\\ is frame advance";
    case SDL_SCANCODE_SPACE:     return "Space is pause";
    case SDL_SCANCODE_M:         return "M is mute";
    case SDL_SCANCODE_H:         return "H switches HQ music";
    default:                     return NULL;
    }
}

void settings_controls_capture_key(SettingsControlsPage* p, int32_t sc) {
    if (!settings_controls_listening(p)) return;
    uint16_t action = k_row_action[p->listen_row];
    if (sc == SDL_SCANCODE_ESCAPE) {
        stop_listening(p);
        return;
    }
    if (sc == SDL_SCANCODE_BACKSPACE) {
        keymap_unbind(action, p->listen_dev == CONTROLS_DEV_PAD);
        p->changed = true;
        stop_listening(p);
        return;
    }
    if (p->listen_dev == CONTROLS_DEV_PAD) return;   /* waiting for a controller button */
    const char* why = settings_controls_reserved(sc);
    if (why) {
        char msg[sizeof(p->status)];
        snprintf(msg, sizeof(msg), "%s. Pick another key.", why);
        set_status(p, msg, true);
        return;
    }
    if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_NUM_SCANCODES || !keymap_key_bindable((SDL_Scancode)sc)) {
        set_status(p, "That key can't be used. Pick another key.", true);
        return;
    }
    keymap_bind_key(action, (SDL_Scancode)sc);
    p->changed = true;
    stop_listening(p);
}

void settings_controls_capture_pad(SettingsControlsPage* p, int32_t pad) {
    if (!settings_controls_listening(p) || p->listen_dev != CONTROLS_DEV_PAD) return;
    if (pad < 0 || pad >= KEYMAP_PAD_COUNT) return;
    keymap_bind_pad(k_row_action[p->listen_row], pad);
    p->changed = true;
    stop_listening(p);
}

static const char* pad_label(int32_t pad, char* buf, size_t cap) {
    if (pad == KEYMAP_PAD_LT) return "LT";
    if (pad == KEYMAP_PAD_RT) return "RT";
    if (pad >= 0 && pad < (int32_t)(sizeof(k_pad_label) / sizeof(k_pad_label[0]))) return k_pad_label[pad];
    snprintf(buf, cap, "Button %d", (int)pad);
    return buf;
}

static bool inside(UiRect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void settings_controls_draw(UiCanvas* c, UiInput* in, SettingsControlsPage* p, UiRect r) {
    float k = r.w < FULL_W ? r.w / FULL_W : 1.0f;   /* shrink the columns on a narrow window */
    float key_x = r.x + KEY_X * k, pad_x = r.x + PAD_X * k, chip_w = CHIP_W * k;
    float rows_y = r.y + 80.0f;

    /* A mouse press anywhere but the listening chip cancels listening. */
    if (settings_controls_listening(p) && in->pressed) {
        UiRect lr = {p->listen_dev == CONTROLS_DEV_KEY ? key_x : pad_x, rows_y + ROW_STEP * (float)p->listen_row,
                     chip_w, ROW_H};
        if (!inside(lr, in->mx, in->my)) stop_listening(p);
    }

    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, r.x, r.y, r.w - 40.0f, UI_ALIGN_LEFT, UI_TEXT, "Controls");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 26.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "Click a box, then press the key or button to use.");
    if (!p->pad_connected)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 42.0f, r.w, UI_ALIGN_LEFT, UI_DIM,
                "No controller connected.");
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, key_x, r.y + 62.0f, chip_w, UI_ALIGN_LEFT, UI_DIM, "KEYBOARD");
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, pad_x, r.y + 62.0f, chip_w, UI_ALIGN_LEFT, UI_DIM, "CONTROLLER");

    float label_dy = ROW_H / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY);
    for (int32_t row = 0; row < CONTROLS_ROWS; row++) {
        float y = rows_y + ROW_STEP * (float)row;
        uint16_t action = k_row_action[row];
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x, y + label_dy, key_x - r.x - 8.0f, UI_ALIGN_LEFT, UI_TEXT,
                k_row_label[row]);
        for (int32_t d = 0; d < 2; d++) {
            ControlsDevice dev = (ControlsDevice)d;
            bool listening = p->listen_row == row && p->listen_dev == dev;
            char buf[24];
            const char* label;
            if (listening) {
                label = dev == CONTROLS_DEV_KEY ? "Press a key" : "Press a button";
            } else if (dev == CONTROLS_DEV_KEY) {
                SDL_Scancode sc = keymap_key_for(action);
                label = sc != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(sc) : k_unbound;
            } else {
                int32_t pad = keymap_pad_for(action);
                label = pad != KEYMAP_PAD_NONE ? pad_label(pad, buf, sizeof(buf)) : k_unbound;
            }
            UiRect chip = {dev == CONTROLS_DEV_KEY ? key_x : pad_x, y, chip_w, ROW_H};
            bool unbound = label == k_unbound;
            bool clicked = ui_button(c, in, settings_id_bind((uint32_t)row, dev), chip, unbound ? "" : label, true,
                                     listening);
            if (unbound)   /* dim, so missing bindings stand apart from real ones */
                ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, chip.x + chip.w / 2,
                        chip.y + chip.h / 2 - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL), chip.w - 8.0f,
                        UI_ALIGN_CENTER, UI_DIM, label);
            if (clicked) {
                if (listening) {
                    stop_listening(p);
                } else {
                    p->listen_row = row;
                    p->listen_dev = dev;
                    set_status(p,
                               dev == CONTROLS_DEV_KEY ? "Press a key. Backspace clears it, Esc cancels."
                                                       : "Press a button. Backspace clears it, Esc cancels.",
                               false);
                }
            }
        }
    }

    float by = rows_y + ROW_STEP * (float)CONTROLS_ROWS + 5.0f;
    UiRect reset = {r.x, by, RESET_W, RESET_H};
    if (ui_button(c, in, SETTINGS_ID_CONTROLS_RESET, reset, "Reset to defaults", true, false)) {
        keymap_reset_defaults();
        p->changed = true;
        p->listen_row = -1;
        set_status(p, "Controls reset.", false);
    }
    if (p->status[0])
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + RESET_W + 12.0f,
                by + RESET_H / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_SMALL), r.w - RESET_W - 12.0f,
                UI_ALIGN_LEFT, p->status_warn ? UI_WARN : UI_MUTED, p->status);
}
