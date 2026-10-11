#include "frontend/settings.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#define CHOICE_W 104.0f
#define CHOICE_H 30.0f
#define CHOICE_GAP 8.0f
#define TOGGLE_H 24.0f

static const char* const k_filter_help[VIDEO_FILTER_COUNT] = {
    "The original pixels, as the GBA drew them.",
    "Rounds jagged edges into clean curves (xBR).",
    "A softer, painted look (Super xBR).",
};

void settings_video_draw(UiCanvas* c, UiInput* in, SettingsVideoPage* p, UiRect r) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, r.x, r.y, r.w - 40.0f, UI_ALIGN_LEFT, UI_TEXT, "Video");
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, r.y + 26.0f, r.w, UI_ALIGN_LEFT, UI_MUTED,
            "How the game image is drawn. Changes show right away.");

    float y = r.y + 58.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, r.x, y, r.w, UI_ALIGN_LEFT, UI_DIM, "SCREEN FILTER");
    y += 20.0f;
    for (int i = 0; i < VIDEO_FILTER_COUNT; i++) {
        UiRect b = {r.x + (CHOICE_W + CHOICE_GAP) * (float)i, y, CHOICE_W, CHOICE_H};
        bool selected = p->filter == (VideoFilter)i;
        if (ui_button(c, in, settings_id_filter((uint32_t)i), b, video_filter_label((VideoFilter)i), true, selected) &&
            !selected) {
            p->filter = (VideoFilter)i;
            p->changed = true;
        }
    }
    y += CHOICE_H + 10.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x, y, r.w, UI_ALIGN_LEFT, UI_MUTED,
            k_filter_help[p->filter < VIDEO_FILTER_COUNT ? p->filter : 0]);
    y += 30.0f;

    ui_hline(c, r.x, y, r.w, UI_DIVIDER);
    y += 16.0f;
    UiRect row = {r.x, y, r.w, TOGGLE_H};
    if (ui_toggle(c, in, SETTINGS_ID_COLOR, row, &p->color_correct, true)) p->changed = true;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 46.0f, y + TOGGLE_H / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_BODY),
            r.w - 46.0f, UI_ALIGN_LEFT, UI_TEXT, "Color correction");
    y += TOGGLE_H + 8.0f;
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + 46.0f, y, r.w - 46.0f, UI_ALIGN_LEFT, UI_MUTED,
            "The darker, softer colors of a real GBA screen.");
}
