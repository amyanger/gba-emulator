#include "frontend/settings.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <string.h>

#define CHOICE_W 72.0f
#define CHOICE_H 30.0f
#define CHOICE_GAP 8.0f
#define TOGGLE_H 24.0f
#define BIOS_ROW_H 70.0f

static const char* const k_ff_label[4] = {"2x", "3x", "4x", "Max"};
static const uint8_t k_ff_value[4] = {2, 3, 4, 0};

static const char* base_name(const char* path) {
    const char* b = path;
    for (const char* q = path; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

void settings_general_draw(UiCanvas* c, UiInput* in, SettingsGeneralPage* p, UiRect r) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, r.x, r.y, r.w - 40.0f, UI_ALIGN_LEFT, UI_TEXT, "General");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 26.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "How the emulator behaves while you play.");

    float y = r.y + 58.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "BIOS FILE");
    y += 20.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x, y, r.w, UI_ALIGN_LEFT, UI_TEXT,
            p->bios_path[0] ? base_name(p->bios_path) : "Built-in (no BIOS file)");
    y += 26.0f;
    UiRect choose = {r.x, y, 110.0f, 30.0f};
    UiRect clear = {r.x + 118.0f, y, 120.0f, 30.0f};
    if (ui_button(c, in, SETTINGS_ID_BIOS_CHOOSE, choose, "Choose...", true, false)) p->choose_bios = true;
    if (ui_button(c, in, SETTINGS_ID_BIOS_CLEAR, clear, "Use built-in", p->bios_path[0] != '\0', false))
        p->clear_bios = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y + 38.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "Used the next time you open a game.");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y + 56.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "Without one, the emulator uses its built-in BIOS.");
    if (p->status[0])
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y + 76.0f, r.w, UI_ALIGN_LEFT, UI_WARN, p->status);
    y += BIOS_ROW_H - 20.0f + 60.0f;

    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "FAST-FORWARD SPEED");
    y += 20.0f;
    for (int i = 0; i < 4; i++) {
        UiRect b = {r.x + (CHOICE_W + CHOICE_GAP) * (float)i, y, CHOICE_W, CHOICE_H};
        bool selected = p->ff_speed == k_ff_value[i];
        if (ui_button(c, in, settings_id_ff((uint32_t)i), b, k_ff_label[i], true, selected) && !selected) {
            p->ff_speed = k_ff_value[i];
            p->changed = true;
        }
    }
    y += CHOICE_H + 10.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "How fast Tab and ` run the game. Max goes as fast as your computer can.");
    y += 30.0f;

    ui_hline(c, r.x, y, r.w, UI_DIVIDER);
    y += 16.0f;
    UiRect row = {r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_BLUR, row, &p->pause_on_blur, true)) p->changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY),
            r.w - 46.0f, UI_ALIGN_LEFT, UI_TEXT, "Pause when the window loses focus");
    y += TOGGLE_H + 8.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Picks up again when you click back into the game.");
}
