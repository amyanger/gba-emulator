#include "frontend/settings.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <string.h>

#define MARGIN     8.0f
#define SIDEBAR_W  150.0f
#define PAD        20.0f
#define DIM_COLOR  0x9E08090Du   /* ~62% black over the game */
#define SIDEBAR_BG 0xFF0D0F15u

static const char* const k_pages[SETTINGS_PAGE_COUNT] = {"Cheats", "Controls", "General", "Video", "Audio", "Randomizer"};

void settings_init(Settings* s) {
    memset(s, 0, sizeof(*s));
    ui_text_area_init(&s->cheats.paste, s->cheats.paste_buf, sizeof(s->cheats.paste_buf));
    ui_text_area_init(&s->cheats.name, s->cheats.name_buf, sizeof(s->cheats.name_buf));
    s->cheats.forced = CHEAT_DEV_AUTO;
    s->cheats.popup = SETTINGS_POPUP_NONE;
    s->cheats.confirm_delete = -1;
    s->controls.listen_row = -1;
}

void settings_open(Settings* s) {
    s->open = true;
    s->close_requested = false;
    s->cheats.popup = SETTINGS_POPUP_NONE;
    s->cheats.confirm_delete = -1;
    s->controls.listen_row = -1;
    s->controls.status[0] = '\0';
}

void settings_close(Settings* s) {
    s->open = false;
    s->close_requested = false;
    s->cheats.popup = SETTINGS_POPUP_NONE;
    s->controls.listen_row = -1;
}

UiRect settings_window_rect(float area_w, float area_h) {
    float w = SETTINGS_W_PT, h = SETTINGS_H_PT;
    if (w > area_w - 2 * MARGIN) w = area_w - 2 * MARGIN;
    if (h > area_h - 2 * MARGIN) h = area_h - 2 * MARGIN;
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
    return (UiRect){(area_w - w) / 2, (area_h - h) / 2, w, h};
}

float settings_density(int area_px_w, int area_px_h, float base) {
    float need_w = SETTINGS_W_PT + 2 * MARGIN, need_h = SETTINGS_H_PT + 2 * MARGIN;
    float fit_w = (float)area_px_w / need_w, fit_h = (float)area_px_h / need_h;
    float fit = fit_w < fit_h ? fit_w : fit_h;
    float d = base < fit ? base : fit;
    return d < SETTINGS_MIN_DENSITY ? SETTINGS_MIN_DENSITY : d;
}

void settings_point_from_window(float wx, float wy, float px_per_unit, float density, float* x, float* y) {
    *x = wx * px_per_unit / density;
    *y = wy * px_per_unit / density;
}

static bool inside(UiRect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void settings_draw(UiCanvas* c, UiInput* in, Settings* s, SettingsContext* ctx) {
    float area_w = (float)c->w / c->scale, area_h = (float)c->h / c->scale;
    UiRect win = settings_window_rect(area_w, area_h);

    /* Outside clicks and Escape close, unless a pop-up is taking them. */
    if (!in->popup_open) {
        if (in->pressed && !inside(win, in->mx, in->my)) s->close_requested = true;
        if (ui_input_has_key(in, UI_KEY_ESCAPE)) s->close_requested = true;
    }

    c->keep_alpha = true;
    ui_canvas_unclip(c);
    ui_canvas_clear(c, DIM_COLOR);
    ui_fill_round_rect(c, (UiRect){win.x - 1, win.y - 1, win.w + 2, win.h + 2}, 11.0f, UI_DIVIDER);
    ui_fill_round_rect(c, win, 10.0f, UI_BG);
    ui_canvas_clip(c, win);

    /* Sidebar, clipped to the window so its corner stays rounded. */
    UiRect side = {win.x, win.y, SIDEBAR_W, win.h};
    ui_fill_round_rect(c, (UiRect){side.x, side.y, side.w + 12.0f, side.h}, 10.0f, SIDEBAR_BG);
    ui_fill_rect(c, (UiRect){side.x + side.w, side.y, 12.0f, side.h}, UI_BG);
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, side.x + 16.0f, side.y + 16.0f, side.w - 24.0f, UI_ALIGN_LEFT, UI_TEXT,
            "Settings");
    for (int i = 0; i < SETTINGS_PAGE_COUNT; i++) {
        UiRect r = {side.x + 8.0f, side.y + 54.0f + 34.0f * (float)i, side.w - 16.0f, 30.0f};
        if (ui_sidebar_item(c, in, ui_id("settings-page", (uint32_t)i), r, k_pages[i], s->page == (SettingsPage)i))
            s->page = (SettingsPage)i;
    }
    /* Leaving Controls drops a listening chip, or the frontend would keep capturing keys. */
    if (s->page != SETTINGS_PAGE_CONTROLS && s->controls.listen_row >= 0) {
        s->controls.listen_row = -1;
        s->controls.status[0] = '\0';
    }

    UiRect close = {win.x + win.w - 34.0f, win.y + 10.0f, 24.0f, 24.0f};
    if (ui_button(c, in, ui_id("settings-close", 0), close, "\xC3\x97", true, false)) s->close_requested = true;

    UiRect content = {side.x + side.w + PAD, win.y + 16.0f, win.w - side.w - 2 * PAD, win.h - 32.0f};
    if (s->page == SETTINGS_PAGE_CHEATS) {
        settings_cheats_draw(c, in, &s->cheats, ctx, content);
    } else if (s->page == SETTINGS_PAGE_VIDEO) {
        settings_video_draw(c, in, &s->video, content);
    } else if (s->page == SETTINGS_PAGE_GENERAL) {
        settings_general_draw(c, in, &s->general, content);
    } else if (s->page == SETTINGS_PAGE_AUDIO) {
        settings_audio_draw(c, in, &s->audio, content);
    } else if (s->page == SETTINGS_PAGE_RANDOMIZER) {
        settings_randomizer_draw(c, in, &s->rando, content);
    } else {
        settings_controls_draw(c, in, &s->controls, content);
    }
    ui_canvas_unclip(c);
}
