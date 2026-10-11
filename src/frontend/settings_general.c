#include "frontend/settings.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

#define CHOICE_W 72.0f
#define CHOICE_H 30.0f
#define CHOICE_GAP 8.0f
#define TOGGLE_H 24.0f

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
    float body_mid = ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY);

    float y = r.y + 54.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "BIOS FILE");
    y += 20.0f;
    UiRect choose = {r.x, y, 110.0f, 30.0f};
    UiRect clear = {r.x + 118.0f, y, 120.0f, 30.0f};
    if (ui_button(c, in, SETTINGS_ID_BIOS_CHOOSE, choose, "Choose...", true, false)) p->choose_bios = true;
    if (ui_button(c, in, SETTINGS_ID_BIOS_CLEAR, clear, "Use built-in", p->bios_path[0] != '\0', false))
        p->clear_bios = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 252.0f, y + 15.0f - body_mid, r.w - 252.0f, UI_ALIGN_LEFT,
            UI_TEXT, p->bios_path[0] ? base_name(p->bios_path) : "Built-in");
    y += 38.0f;
    if (p->status[0])
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y, r.w, UI_ALIGN_LEFT, UI_WARN, p->status);
    else
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y, r.w, UI_ALIGN_LEFT, UI_MUTED,
                "Used the next time you open a game. Without one, the built-in BIOS runs.");
    y += 28.0f;

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
    y += CHOICE_H + 8.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "How fast Tab and ` run the game. Max goes as fast as your computer can.");
    y += 26.0f;

    UiRect row = {r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_BLUR, row, &p->pause_on_blur, true)) p->changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - body_mid, r.w - 46.0f, UI_ALIGN_LEFT,
            UI_TEXT, "Pause when the window loses focus");
    y += TOGGLE_H + 4.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Picks up again when you click back into the game.");
    y += 24.0f;

    ui_hline(c, r.x, y, r.w, UI_DIVIDER);
    y += 14.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "FOLLOWING POKEMON");
    y += 20.0f;
    row = (UiRect){r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_FOLLOWER, row, &p->follower, p->hgss_ready)) p->follower_changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - body_mid, r.w - 46.0f, UI_ALIGN_LEFT,
            p->hgss_ready ? UI_TEXT : UI_MUTED, "Your lead Pokemon walks behind you (F1)");
    y += TOGGLE_H + 10.0f;
    UiRect pick = {r.x, y, 110.0f, 30.0f};
    if (ui_button(c, in, SETTINGS_ID_HGSS_CHOOSE, pick, "Choose...", true, false)) p->choose_hgss = true;
    char line[SETTINGS_PATH_LEN + 32];
    snprintf(line, sizeof(line), "HeartGold/SoulSilver ROM: %s", p->hgss_path[0] ? base_name(p->hgss_path) : "Not set");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 124.0f, y + 15.0f - body_mid, r.w - 124.0f, UI_ALIGN_LEFT,
            UI_TEXT, line);
    y += 38.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "Pokemon Emerald (US) only. Sprites and lines are read from your own ROM.");
}
