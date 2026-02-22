#include "frontend.h"
#include "gba.h"
#include "apu/apu.h"
#include "input/input.h"

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
#endif

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

#ifdef ENABLE_XRAY
        case SDL_WINDOWEVENT:
            /* Handle X-Ray window close button */
            if (event.window.event == SDL_WINDOWEVENT_CLOSE &&
                g_xray && g_xray->window_id == event.window.windowID) {
                xray_toggle(g_xray);
            }
            break;
#endif

        case SDL_KEYDOWN: {
            uint16_t key = sdl_to_gba_key(event.key.keysym.scancode);
            if (key) input_press(&gba->input, key);

            // Escape to quit
            if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                fe->running = false;
                gba->running = false;
            }

#ifdef ENABLE_XRAY
            // F2 toggles X-Ray mode
            if (event.key.keysym.scancode == SDL_SCANCODE_F2 && g_xray) {
                xray_toggle(g_xray);
            }
#endif
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
    want.callback = NULL; /* Push mode via SDL_QueueAudio */

    fe->audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (fe->audio_device == 0) {
        LOG_WARN("SDL audio failed: %s", SDL_GetError());
        return;
    }

    SDL_PauseAudioDevice(fe->audio_device, 0);
    LOG_INFO("Audio initialized: %d Hz, %d channels", have.freq, have.channels);
}

void frontend_push_audio(Frontend* fe, APU* apu) {
    if (fe->audio_device == 0) return;

    /* Calculate how many samples are available in the ring buffer */
    uint32_t write_pos = apu->write_pos;
    uint32_t read_pos = apu->read_pos;

    uint32_t available;
    if (write_pos >= read_pos) {
        available = write_pos - read_pos;
    } else {
        available = SAMPLE_BUFFER_SIZE - read_pos + write_pos;
    }

    if (available == 0) return;

    /* Push samples from ring buffer to SDL */
    if (write_pos > read_pos) {
        /* Contiguous region */
        SDL_QueueAudio(fe->audio_device,
                       &apu->sample_buffer[read_pos * 2],
                       available * 2 * sizeof(int16_t));
    } else {
        /* Wraps around: push in two parts */
        uint32_t first_part = SAMPLE_BUFFER_SIZE - read_pos;
        SDL_QueueAudio(fe->audio_device,
                       &apu->sample_buffer[read_pos * 2],
                       first_part * 2 * sizeof(int16_t));
        if (write_pos > 0) {
            SDL_QueueAudio(fe->audio_device,
                           &apu->sample_buffer[0],
                           write_pos * 2 * sizeof(int16_t));
        }
    }

    apu->read_pos = write_pos;
}

void frontend_frame_sync(Frontend* fe) {
    if (fe->audio_device != 0) {
        /* Audio-driven sync: block until SDL's audio queue drains.
         * This ties emulation speed to the audio playback rate (~60fps).
         * Target: ~2 frames of audio buffered (low latency, no underrun).
         * Cap iterations to avoid hanging if the audio device stalls. */
        uint32_t target = 1100 * 2 * sizeof(int16_t);
        int max_wait = 100;
        while (SDL_GetQueuedAudioSize(fe->audio_device) > target && max_wait > 0) {
            SDL_Delay(1);
            max_wait--;
        }
    } else {
        /* No audio device: timer-based fallback (~60fps). */
        SDL_Delay(16);
    }
}
