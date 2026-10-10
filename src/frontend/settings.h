#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "cheat/cheat.h"
#include "frontend/toast.h"
#include "ui/ui_controls.h"

/* The in-app Settings window: a 640x440 pt panel over the dimmed game with a
 * sidebar of pages. Pure C; the frontend owns the texture and feeds UiInput. */

typedef struct Cartridge Cartridge;

typedef enum {
    SETTINGS_PAGE_CHEATS,
    SETTINGS_PAGE_CONTROLS,
    SETTINGS_PAGE_GENERAL,
    SETTINGS_PAGE_VIDEO,
    SETTINGS_PAGE_AUDIO,
    SETTINGS_PAGE_COUNT
} SettingsPage;

typedef enum { SETTINGS_STATUS_NONE, SETTINGS_STATUS_OK, SETTINGS_STATUS_UNSURE, SETTINGS_STATUS_ERROR } SettingsStatus;

#define SETTINGS_POPUP_NONE (-1)
#define SETTINGS_POPUP_ADD  (-2)   /* the device chip under the paste box */
#define SETTINGS_PASTE_LEN  2048

typedef struct {
    char paste_buf[SETTINGS_PASTE_LEN];
    UiTextArea paste;
    char name_buf[CHEAT_NAME_LEN];
    UiTextArea name;
    bool name_edited;               /* typed by the user: stop filling it from the paste */
    SettingsStatus status_kind;
    char status[160];
    CheatDevice detected, forced;   /* forced: CHEAT_DEV_AUTO when not picked */
    int32_t popup;                  /* SETTINGS_POPUP_NONE / _ADD / row index */
    UiRect popup_anchor;
    int32_t confirm_delete;         /* row showing "Delete?", -1 for none */
    float list_scroll;
    bool scroll_to_end;
} SettingsCheatsPage;

typedef struct {
    bool open;
    bool close_requested;           /* the frontend closes it */
    SettingsPage page;
    SettingsCheatsPage cheats;
} Settings;

typedef struct {
    CheatEngine* cheats;
    Cartridge* cart;
    const char* cht_path;           /* where cheats are saved; NULL = don't save */
    void (*toast)(void* user, ToastKind kind, const char* title, const char* detail);
    void* user;
} SettingsContext;

#define SETTINGS_W_PT 640.0f
#define SETTINGS_H_PT 440.0f
#define SETTINGS_MIN_DENSITY (10.0f / 13.0f)   /* 13 pt body text never below 10 px */

#define SETTINGS_ID_PASTE ui_id("cheat-paste", 0)
#define SETTINGS_ID_NAME  ui_id("cheat-name", 0)
#define SETTINGS_ID_ADD   ui_id("cheat-add", 0)
static inline uint32_t settings_id_toggle(uint32_t i) { return ui_id("cheat-toggle", i); }
static inline uint32_t settings_id_delete(uint32_t i) { return ui_id("cheat-delete", i); }
static inline uint32_t settings_id_device(uint32_t i) { return ui_id("cheat-device", i); }

void settings_init(Settings* s);
void settings_open(Settings* s);
void settings_close(Settings* s);

/* The window in points, centered in an area, shrunk to fit with an 8 pt margin. */
UiRect settings_window_rect(float area_w_pt, float area_h_pt);
/* Canvas density for a drawable of area_px: the panel density, lowered so the
 * window fits, never below SETTINGS_MIN_DENSITY. */
float settings_density(int area_px_w, int area_px_h, float base_density);
/* SDL window units to canvas points. */
void settings_point_from_window(float wx, float wy, float px_per_unit, float density, float* x, float* y);

/* Clears the canvas, dims it, draws the window and the current page. */
void settings_draw(UiCanvas* c, UiInput* in, Settings* s, SettingsContext* ctx);

/* The Cheats page inside content (points). */
void settings_cheats_draw(UiCanvas* c, UiInput* in, SettingsCheatsPage* p, SettingsContext* ctx, UiRect content);

#endif // SETTINGS_H
