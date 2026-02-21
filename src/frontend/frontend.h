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

    int scale;
    bool running;
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

#endif // FRONTEND_H
