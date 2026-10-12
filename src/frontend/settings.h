#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "cheat/cheat.h"
#include "frontend/toast.h"
#include "frontend/video_filter.h"
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

/* The frontend copies its values in before drawing and applies them when changed is set. */
typedef struct {
    VideoFilter filter;
    bool color_correct;
    bool changed;
} SettingsVideoPage;

typedef enum { SETTINGS_HQ_OFF, SETTINGS_HQ_ACTIVE, SETTINGS_HQ_WAITING, SETTINGS_HQ_UNAVAILABLE } SettingsHqStatus;

typedef struct {
    int32_t volume;   /* 0-100 */
    bool start_muted;
    bool hq_music;
    uint8_t hq_status;   /* SettingsHqStatus, shown under the switch */
    bool changed;
} SettingsAudioPage;

#define SETTINGS_PATH_LEN 1024
typedef struct {
    char bios_path[SETTINGS_PATH_LEN];   /* "" = built-in BIOS */
    uint8_t ff_speed;                    /* 2, 3, 4; 0 = Max (uncapped) */
    bool pause_on_blur;
    bool choose_bios, clear_bios;        /* buttons ask the frontend */
    char status[96];
    bool changed;
    /* Following Pokemon: the frontend copies these in and handles the requests. */
    char hgss_path[SETTINGS_PATH_LEN];   /* "" = not set */
    bool hgss_ready;                     /* the ROM opened: the toggle is usable */
    bool follower;
    bool choose_hgss, follower_changed;
} SettingsGeneralPage;

typedef enum { CONTROLS_DEV_KEY, CONTROLS_DEV_PAD } ControlsDevice;
#define CONTROLS_ROWS 12
typedef struct {
    int32_t listen_row;          /* -1 = not listening */
    ControlsDevice listen_dev;
    bool pad_connected;          /* copied in by the frontend */
    char status[96];
    bool status_warn;            /* status is a refusal */
    bool changed;                /* bindings changed: frontend saves keymap.ini and releases keys */
} SettingsControlsPage;

typedef struct {
    bool open;
    bool close_requested;           /* the frontend closes it */
    SettingsPage page;
    SettingsCheatsPage cheats;
    SettingsVideoPage video;
    SettingsAudioPage audio;
    SettingsGeneralPage general;
    SettingsControlsPage controls;
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
/* The Video page: screen filter and color correction. */
void settings_video_draw(UiCanvas* c, UiInput* in, SettingsVideoPage* p, UiRect content);
static inline uint32_t settings_id_filter(uint32_t i) { return ui_id("video-filter", i); }
#define SETTINGS_ID_COLOR ui_id("video-color", 0)

/* The General page: BIOS file, fast-forward speed, pause on focus loss, Following Pokemon. */
void settings_general_draw(UiCanvas* c, UiInput* in, SettingsGeneralPage* p, UiRect content);
static inline uint32_t settings_id_ff(uint32_t i) { return ui_id("general-ff", i); }   /* 0=2x 1=3x 2=4x 3=Max */
#define SETTINGS_ID_BLUR ui_id("general-blur", 0)
#define SETTINGS_ID_BIOS_CHOOSE ui_id("general-bios", 0)
#define SETTINGS_ID_BIOS_CLEAR  ui_id("general-bios", 1)
#define SETTINGS_ID_HGSS_CHOOSE ui_id("general-hgss", 0)
#define SETTINGS_ID_FOLLOWER    ui_id("general-follower", 0)

/* The Audio page: volume, start muted and HQ music. */
void settings_audio_draw(UiCanvas* c, UiInput* in, SettingsAudioPage* p, UiRect content);
#define SETTINGS_ID_VOLUME      ui_id("audio-volume", 0)
#define SETTINGS_ID_START_MUTED ui_id("audio-start-muted", 0)
#define SETTINGS_ID_HQ_MUSIC    ui_id("audio-hq-music", 0)
/* dst[i] = src[i] * volume / 100 for count int16 samples. */
void settings_audio_scale(int16_t* dst, const int16_t* src, uint32_t count, int32_t volume);

/* The Controls page: keyboard and controller bindings for the GBA buttons and turbo. */
static inline uint32_t settings_id_bind(uint32_t row, ControlsDevice dev) {
    return ui_id("controls-bind", row * 2 + (uint32_t)dev);
}
#define SETTINGS_ID_CONTROLS_RESET ui_id("controls-reset", 0)
void settings_controls_draw(UiCanvas* c, UiInput* in, SettingsControlsPage* p, UiRect content);
bool settings_controls_listening(const SettingsControlsPage* p);
/* Next key while listening. Escape cancels, Backspace unbinds, reserved keys are refused. */
void settings_controls_capture_key(SettingsControlsPage* p, int32_t scancode);
void settings_controls_capture_pad(SettingsControlsPage* p, int32_t pad);
/* Why a key can't be bound ("Tab is fast-forward"), or NULL. */
const char* settings_controls_reserved(int32_t scancode);

#endif // SETTINGS_H
