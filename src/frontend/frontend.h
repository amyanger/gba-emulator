#ifndef FRONTEND_H
#define FRONTEND_H

#include "common.h"
#include <SDL2/SDL.h>

// Forward declaration
typedef struct GBA GBA;

typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_AudioDeviceID audio_device;
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
    bool muted;              // M key / --mute flag — drop audio, keep emulation running
    uint16_t controller_keys;  // bitmask of keys held by gamepad (avoids clobbering keyboard)
} Frontend;

// Forward declaration
typedef struct APU APU;

bool frontend_init(Frontend* fe, int scale);
void frontend_destroy(Frontend* fe);
void frontend_present_frame(Frontend* fe, uint16_t* framebuffer);
void frontend_poll_input(Frontend* fe, GBA* gba);
void frontend_audio_init(Frontend* fe);
void frontend_push_audio(Frontend* fe, APU* apu);
void frontend_frame_sync(Frontend* fe);
void frontend_set_ff_indicator(Frontend* fe, bool active);
void frontend_set_pause_indicator(Frontend* fe, bool active);
void frontend_set_mute_indicator(Frontend* fe, bool active);

#endif // FRONTEND_H
