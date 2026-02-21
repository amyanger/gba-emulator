#include "frontend.h"
#include "gba.h"
#include "input/input.h"

bool frontend_init(Frontend* fe, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        LOG_ERROR("SDL init failed: %s", SDL_GetError());
        return false;
    }

    fe->scale = scale;
    fe->window = SDL_CreateWindow("GBA Emulator", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH * scale,
                                  SCREEN_HEIGHT * scale, SDL_WINDOW_SHOWN);
    if (!fe->window) {
        LOG_ERROR("SDL window creation failed: %s", SDL_GetError());
        return false;
    }

    fe->renderer = SDL_CreateRenderer(fe->window, -1, SDL_RENDERER_ACCELERATED);
    if (!fe->renderer) {
        LOG_ERROR("SDL renderer creation failed: %s", SDL_GetError());
        return false;
    }

    // 15-bit color texture (ABGR1555)
    fe->texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ABGR1555,
                                    SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!fe->texture) {
        LOG_ERROR("SDL texture creation failed: %s", SDL_GetError());
        return false;
    }

    fe->running = true;

    LOG_INFO("Frontend initialized (%dx scale)", scale);
    return true;
}

void frontend_destroy(Frontend* fe) {
    if (fe->texture) SDL_DestroyTexture(fe->texture);
    if (fe->renderer) SDL_DestroyRenderer(fe->renderer);
    if (fe->window) SDL_DestroyWindow(fe->window);
    if (fe->audio_device) SDL_CloseAudioDevice(fe->audio_device);
    SDL_Quit();
}

void frontend_present_frame(Frontend* fe, uint16_t* framebuffer) {
    SDL_UpdateTexture(fe->texture, NULL, framebuffer, SCREEN_WIDTH * sizeof(uint16_t));
    SDL_RenderClear(fe->renderer);
    SDL_RenderCopy(fe->renderer, fe->texture, NULL, NULL);
    SDL_RenderPresent(fe->renderer);
}

// Map SDL scancodes to GBA buttons
static uint16_t sdl_to_gba_key(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_Z:      return KEY_A;
    case SDL_SCANCODE_X:      return KEY_B;
    case SDL_SCANCODE_RETURN: return KEY_START;
    case SDL_SCANCODE_RSHIFT: return KEY_SELECT;
    case SDL_SCANCODE_UP:     return KEY_UP;
    case SDL_SCANCODE_DOWN:   return KEY_DOWN;
    case SDL_SCANCODE_LEFT:   return KEY_LEFT;
    case SDL_SCANCODE_RIGHT:  return KEY_RIGHT;
    case SDL_SCANCODE_A:      return KEY_L;
    case SDL_SCANCODE_S:      return KEY_R;
    default:                  return 0;
    }
}

void frontend_poll_input(Frontend* fe, GBA* gba) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            fe->running = false;
            gba->running = false;
            break;

        case SDL_KEYDOWN: {
            uint16_t key = sdl_to_gba_key(event.key.keysym.scancode);
            if (key) input_press(&gba->input, key);

            // Escape to quit
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                fe->running = false;
                gba->running = false;
            }
            break;
        }

        case SDL_KEYUP: {
            uint16_t key = sdl_to_gba_key(event.key.keysym.scancode);
            if (key) input_release(&gba->input, key);
            break;
        }
        }
    }
}

void frontend_audio_init(Frontend* fe) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 32768;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; // TODO: Set audio callback

    fe->audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (fe->audio_device == 0) {
        LOG_WARN("SDL audio failed: %s", SDL_GetError());
        return;
    }

    SDL_PauseAudioDevice(fe->audio_device, 0); // Start playback
    LOG_INFO("Audio initialized: %d Hz, %d channels", have.freq, have.channels);
}
