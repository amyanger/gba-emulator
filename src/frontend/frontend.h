#ifndef FRONTEND_H
#define FRONTEND_H

#include "common.h"
#include <SDL2/SDL.h>
#include "frontend/slot_picker.h"
#include "frontend/game_panel.h"
#include "frontend/panel_layout.h"
#include "frontend/toast.h"
#include "frontend/audio_out.h"
#include "frontend/settings.h"
#include "frontend/prefs.h"
#include "frontend/frame_pacer.h"
#include "frontend/video_filter.h"
#include "frontend/follower_draw.h"
#include "game/overworld.h"

// Forward declaration
typedef struct GBA GBA;

typedef struct Frontend {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    /* Screen filter and LCD color correction (Settings > Video); Sharp without correction
     * keeps uploading the BGR555 framebuffer to texture as is. */
    VideoFilter video_filter;
    bool color_correct;
    int32_t volume;       /* Settings > Audio, 0-100 */
    bool start_muted;
    SDL_Texture* video_texture;   /* filter output, ARGB8888, video_tex_w x video_tex_h */
    int video_tex_w, video_tex_h;
    SDL_Texture* overlay_texture;
    uint32_t* overlay_buffer;     /* SCREEN_WIDTH * SCREEN_HEIGHT pixels, ARGB8888 */
    bool overlay_dirty;  /* set by overlay producers each frame; cleared by frontend_overlay_clear */
    SDL_Texture* panel_texture;   /* panel_px_w x panel_px_h, ARGB8888, drawn 1:1 */
    uint32_t* panel_buffer;
    int panel_px_w, panel_px_h;
    float panel_density;          /* canvas pixels per point */
    bool panel_visible;           /* F9 */
    uint8_t panel_page;           /* GamePage, F10 */
    PanelBattleFollow panel_follow; /* switches to Auto while a battle runs */
    uint8_t panel_sel;            /* selected party slot, [ and ] */
    uint8_t nuz_sub;              /* NuzSubView on the Nuzlocke page, [ and ] */
    NuzSession* nuz;              /* set by main; N starts a run on the Nuzlocke page */
    LayoutPlan plan;
    PxRect game_rect, panel_rect; /* drawable pixels */

    /* What the panel buffer currently shows, so unchanged frames skip the redraw. */
    GameSnapshot last_snap;
    MirageArm mirage_arm;    /* J J clock jump on the Route page */
    uint8_t last_page, last_sel, last_nuz_sub;
    uint32_t last_nuz_version;
    bool panel_dirty;

    /* Toasts and status badges, drawn over the top of the game at full resolution. */
    ToastQueue toasts;
    SDL_Texture* hud_texture;     /* hud_px_w x hud_px_h, ARGB8888 straight alpha */
    uint32_t* hud_buffer;
    int hud_px_w, hud_px_h;
    bool hud_dirty;               /* size changed: redraw even if the queue did not */
    bool hud_shown;               /* draw the texture this frame */

    /* Overworld X-Ray (F4): trainer sight and hidden items over the game, full resolution. */
    bool overworld_xray;
    OverworldFeed* ow_feed;       /* set by main; NULL when the ROM is not supported */
    Prefs* prefs;                 /* set by main; NULL in headless runs */
    SDL_Texture* ow_texture;      /* game_rect size, ARGB8888 straight alpha */
    uint32_t* ow_buffer;
    int ow_px_w, ow_px_h;
    bool ow_dirty;                /* size changed: repaint even if the snapshot did not */
    bool ow_shown;
    OverworldSnapshot ow_last;    /* what ow_buffer shows */

    /* Following Pokemon (F1): the lead walks behind the player as an extra OBJ. */
    FollowerView follower;        /* follower.enabled is the F1 state */
    FollowerFeed* fol_feed;       /* set by main; NULL when the ROM is not supported */
    HgssAssets* hgss;             /* set by main; !hgss->ok when no HeartGold ROM is set */
    uint8_t fol_step;             /* talk step whose data was last handed to the player */
    uint16_t fol_prev_keys;       /* host GBA keys held last frame (pressed = 1) */
    uint32_t audio_rate;          /* output device rate, 0 without audio */
    SDL_AudioDeviceID audio_device;
    uint32_t audio_target_bytes;  /* frame-sync queue target: 3x the device pull size */
    AudioOut audio_out;           /* DC blocker + resampler from 32768 Hz to the device rate */
    bool audio_stopped;           /* fade-out queued; the next push fades back in */
    SDL_GameController* controller;

    int scale;
    bool running;

    // Save state
    int32_t savestate_slot;    // Current slot (0-9), default 0
    bool save_requested;
    bool load_requested;
    bool screenshot_requested; // F12 — captured at next frame boundary
    char rom_path[256];        // Copy of ROM path for building slot filenames

    // Fast-forward
    bool ff_hold;            // true while Tab is held
    bool ff_toggle;          // persistent toggle via backtick
    uint32_t ff_frame_skip;  // render every N frames during FF (default 4)
    uint32_t ff_frame_count; // rolling counter for frame skip

    // Rewind
#ifdef ENABLE_REWIND
    bool rewind_hold;        // true while Backspace is held
#endif

    bool fullscreen;
    bool paused;             // Spacebar — freeze emulation, mute audio, keep window responsive
    bool step_pending;       // true if frame advance just requested a single-step frame
    bool input_display_enabled; // F3 — mini-GBA HUD showing held buttons
    bool muted;              // M key / --mute flag — drop audio, keep emulation running
    /* Held masks per source, ORed into KEYINPUT by frontend_apply_input.
     * kb_keys/pad_keys carry turbo bits in 10-11 (KEYMAP_TURBO_A/B). */
    uint16_t kb_keys;
    uint16_t pad_keys;
    uint16_t stick_keys;
    uint32_t turbo_frame;    // advanced once per emulated frame; drives the turbo pulse
    bool lt_down, rt_down;   // trigger state, with hysteresis
    const char* keymap_path; // <prefdir>/keymap.ini, NULL when unavailable

    SlotPicker slot_picker;  // F6/F7 modal slot picker and label editor

    /* Settings window (⌘, / Ctrl+,): drawn over everything at window resolution. */
    Settings settings;
    SettingsContext settings_ctx;   /* set by main once cheats are loaded */
    UiInput ui_input;
    SDL_Texture* settings_texture;
    uint32_t* settings_buffer;
    int settings_px_w, settings_px_h;
    float settings_density;
    bool settings_dirty;
    bool settings_was_paused;
    uint32_t settings_blink;        /* caret blink phase last drawn */
    uint32_t settings_event;        /* SDL user event type that opens/closes Settings */
    uint8_t ff_speed;               /* fast-forward: 2, 3, 4; 0 = Max */
    bool pause_on_blur;
    bool blur_paused;               /* window lost focus and pause_on_blur is on */
    bool had_focus;                 /* a window has had keyboard focus at least once */
    bool link_active;               /* link cable peer attached: never blur-pause */
    FramePacer pacer;               /* deadline pacing when sync is not audio-driven */
} Frontend;

// Forward declaration
typedef struct APU APU;

void frontend_set_hints(void);
bool frontend_init(Frontend* fe, int scale);
void frontend_destroy(Frontend* fe);
void frontend_apply_layout(Frontend* fe);
void frontend_present_frame(Frontend* fe, uint16_t* framebuffer);
void frontend_poll_input(Frontend* fe, GBA* gba);
/* Writes the combined held masks into KEYINPUT. */
void frontend_apply_input(Frontend* fe, GBA* gba);
/* Call once before each emulated frame: advances turbo, then applies input. */
void frontend_input_frame(Frontend* fe, GBA* gba);
void frontend_audio_init(Frontend* fe);
void frontend_push_audio(Frontend* fe, APU* apu);
/* Drop pending APU samples and end playback with a short fade instead of a
 * hard cut. Safe to call every frame while audio is off. */
void frontend_audio_stop(Frontend* fe, APU* apu);
void frontend_frame_sync(Frontend* fe);
/* Sleeps until the next frame deadline at `speed` x real time. */
void frontend_pace(Frontend* fe, uint32_t speed);
void frontend_toast(Frontend* fe, ToastKind kind, const char* title, const char* detail);
void frontend_toast_keyed(Frontend* fe, const char* key, ToastKind kind, const char* title,
                          const char* detail);
/* Updates badges from frontend state, expires toasts, redraws the HUD if needed. */
void frontend_update_hud(Frontend* fe, uint32_t now_ms);
/* F4: turns the overlay on or off, (un)registers the line-160 observer, saves prefs. */
void frontend_toggle_overworld(Frontend* fe, GBA* gba);
/* Line-160 observer hub: feeds the Overworld X-Ray and Following Pokemon (ctx = Frontend). */
void frontend_vblank(const GBA* gba, void* ctx);
/* Installs frontend_vblank when either feature needs it, else no observer. */
void frontend_sync_observer(Frontend* fe, GBA* gba);
/* F1: turns Following Pokemon on or off (or toasts why it can't), saves prefs. */
void frontend_toggle_follower(Frontend* fe, GBA* gba);
/* Drops follower state that no longer matches the game: savestate load, rewind, toggle. */
void frontend_follower_reset(Frontend* fe, GBA* gba);
/* Sets the screen filter and color correction and saves them to prefs. */
void frontend_set_video(Frontend* fe, VideoFilter filter, bool color_correct);
/* Sets the volume (0-100) and start-muted default and saves them to prefs. */
void frontend_set_audio(Frontend* fe, int32_t volume, bool start_muted);
/* Sets the fast-forward speed (2, 3, 4, 0 = Max) and pause-on-blur, and saves them to prefs. */
void frontend_set_general(Frontend* fe, uint8_t ff_speed, bool pause_on_blur);
bool frontend_is_paused(const Frontend* fe);
/* Repaints the overlay when s changed; s = NULL or !s->visible hides it. */
void frontend_update_overworld(Frontend* fe, const OverworldSnapshot* s);
void frontend_settings_open(Frontend* fe, GBA* gba);
void frontend_settings_close(Frontend* fe, GBA* gba);
/* Redraws Settings when input arrived and closes it when it asked to; call before presenting. */
void frontend_update_settings(Frontend* fe, GBA* gba);

#endif // FRONTEND_H
