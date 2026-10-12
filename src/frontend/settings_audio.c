#include "frontend/settings.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <stdio.h>

#define TOGGLE_H 24.0f
#define VALUE_W  60.0f

void settings_audio_scale(int16_t* dst, const int16_t* src, uint32_t count, int32_t volume) {
    for (uint32_t i = 0; i < count; i++) dst[i] = (int16_t)((int32_t)src[i] * volume / 100);
}

static const char* const k_hq_status[] = {"", "Active", "Waiting for music", "Not available for this game"};

void settings_audio_draw(UiCanvas* c, UiInput* in, SettingsAudioPage* p, UiRect r) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, r.x, r.y, r.w - 40.0f, UI_ALIGN_LEFT, UI_TEXT, "Audio");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 26.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "Sound for every game. Changes apply right away.");

    float y = r.y + 58.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "VOLUME");
    y += 20.0f;
    UiRect row = {r.x, y, r.w - VALUE_W, 24.0f};
    if (ui_slider(c, in, SETTINGS_ID_VOLUME, row, &p->volume, 0, 100, 5)) p->changed = true;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)p->volume);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + r.w - VALUE_W,
            y + row.h / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY), VALUE_W, UI_ALIGN_RIGHT, UI_TEXT, buf);
    y += row.h + 18.0f;

    ui_hline(c, r.x, y, r.w, UI_DIVIDER);
    y += 16.0f;
    UiRect trow = {r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_START_MUTED, trow, &p->start_muted, true)) p->changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY),
            r.w - 46.0f, UI_ALIGN_LEFT, UI_TEXT, "Start muted");
    y += TOGGLE_H + 8.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Open games with sound off.");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y + 18.0f, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "M still turns sound on and off while you play.");

    y += 18.0f + 30.0f;
    ui_hline(c, r.x, y, r.w, UI_DIVIDER);
    y += 16.0f;
    UiRect hrow = {r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_HQ_MUSIC, hrow, &p->hq_music, true)) p->changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY),
            r.w - 46.0f, UI_ALIGN_LEFT, UI_TEXT, "HQ music");
    y += TOGGLE_H + 8.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "Plays game music in high quality. Press H to switch while you play.");
    if (p->hq_music && p->hq_status < sizeof(k_hq_status) / sizeof(k_hq_status[0]))
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y + 18.0f, r.w - 46.0f, UI_ALIGN_LEFT, UI_DIM,
                k_hq_status[p->hq_status]);
}
