#ifndef FRONTEND_H
#define FRONTEND_H

#include "common.h"
#include <SDL2/SDL.h>
#include "frontend/slot_picker.h"
#include "frontend/game_panel.h"
#include "frontend/panel_layout.h"
#include "frontend/toast.h"

// Forward declaration
typedef struct GBA GBA;

typedef struct Frontend {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
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
    LayoutPlan plan;
    PxRect game_rect, panel_rect; /* drawable pixels */

    /* What the panel buffer currently shows, so unchanged frames skip the redraw. */
    GameSnapshot last_snap;
    uint8_t last_page, last_sel;
    bool panel_dirty;

    /* Toasts and status badges, drawn over the top of the game at full resolution. */
    ToastQueue toasts;
    SDL_Texture* hud_texture;     /* hud_px_w x hud_px_h, ARGB8888 straight alpha */
    uint32_t* hud_buffer;
    int hud_px_w, hud_px_h;
    bool hud_dirty;               /* size changed: redraw even if the queue did not */
    bool hud_shown;               /* draw the texture this frame */
    SDL_AudioDeviceID audio_device;
    uint32_t audio_target_bytes;  /* frame-sync queue target: 3x the device pull size */
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
    uint16_t controller_keys;  // bitmask of keys held by gamepad (avoids clobbering keyboard)

    SlotPicker slot_picker;  // F6/F7 modal slot picker and label editor
} Frontend;

// Forward declaration
typedef struct APU APU;

void frontend_set_hints(void);
bool frontend_init(Frontend* fe, int scale);
void frontend_destroy(Frontend* fe);
void frontend_apply_layout(Frontend* fe);
void frontend_present_frame(Frontend* fe, uint16_t* framebuffer);
void frontend_poll_input(Frontend* fe, GBA* gba);
void frontend_audio_init(Frontend* fe);
void frontend_push_audio(Frontend* fe, APU* apu);
void frontend_frame_sync(Frontend* fe);
void frontend_toast(Frontend* fe, ToastKind kind, const char* title, const char* detail);
void frontend_toast_keyed(Frontend* fe, const char* key, ToastKind kind, const char* title,
                          const char* detail);
/* Updates badges from frontend state, expires toasts, redraws the HUD if needed. */
void frontend_update_hud(Frontend* fe, uint32_t now_ms);

#endif // FRONTEND_H
