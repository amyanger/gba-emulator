#include "frontend/settings.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <stdio.h>

#define TOGGLE_H 24.0f

void settings_randomizer_draw(UiCanvas* c, UiInput* in, SettingsRandomizerPage* p, UiRect r) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, r.x, r.y, r.w - 40.0f, UI_ALIGN_LEFT, UI_TEXT, "Randomizer");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 26.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "Random Pokemon for this save only.");
    float body_mid = ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY);

    float y = r.y + 54.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "RANDOMIZER  (THIS SAVE ONLY)");
    y += 20.0f;
    UiRect row = {r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_RANDO, row, &p->on, p->available)) p->changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - body_mid, r.w - 46.0f, UI_ALIGN_LEFT,
            p->available ? UI_TEXT : UI_MUTED, "Randomize Pokemon");
    y += TOGGLE_H + 4.0f;
    if (!p->available) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
                "Pokemon Emerald (US) only.");
        return;
    }
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Starters, wild Pokemon and every trainer.");
    y += 18.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Legendaries are never touched.");
    y += 26.0f;
    char line[32];
    if (p->on && p->seed)
        snprintf(line, sizeof(line), "Seed: %04X-%04X", (unsigned)(p->seed >> 16), (unsigned)(p->seed & 0xFFFF));
    else
        snprintf(line, sizeof(line), "Seed: none");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + 15.0f - body_mid, 150.0f, UI_ALIGN_LEFT,
            p->on ? UI_TEXT : UI_MUTED, line);
    UiRect reseed = {r.x + 200.0f, y, 110.0f, 30.0f};
    if (ui_button(c, in, SETTINGS_ID_RANDO_SEED, reseed, "New seed", p->on, false)) p->reseed = true;
    y += 38.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Takes effect the next time you open the game. Savestates still load.");
}
