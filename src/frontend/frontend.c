#include "frontend.h"
#include "frame_advance.h"
#include "input_display.h"
#include "slot_picker.h"
#include "gba.h"
#include "apu/apu.h"
#include "input/input.h"
#include "input/keymap.h"

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
#endif

bool frontend_init(Frontend* fe, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
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

    fe->overlay_texture = SDL_CreateTexture(fe->renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!fe->overlay_texture) {
        LOG_ERROR("Failed to create overlay texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(fe->overlay_texture, SDL_BLENDMODE_BLEND);

    fe->overlay_buffer = (uint32_t*)calloc(SCREEN_WIDTH * SCREEN_HEIGHT,
                                           sizeof(uint32_t));
    if (!fe->overlay_buffer) {
        LOG_ERROR("Failed to allocate overlay buffer");
        return false;
    }
    fe->overlay_dirty = false;

    fe->running = true;
    fe->savestate_slot = 0;
    fe->save_requested = false;
    fe->load_requested = false;

    fe->paused = false;
    fe->step_pending = false;
    fe->input_display_enabled = false;
    fe->ff_hold = false;
    fe->ff_toggle = false;
    fe->ff_frame_skip = 4;
    fe->ff_frame_count = 0;

#ifdef ENABLE_REWIND
    fe->rewind_hold = false;
#endif

    fe->fullscreen = false;
    fe->muted = false;
    fe->controller_keys = 0;
    fe->slot_picker.mode = SLOT_PICKER_CLOSED;

    fe->controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            fe->controller = SDL_GameControllerOpen(i);
            if (fe->controller) {
                LOG_INFO("Controller connected: %s",
                         SDL_GameControllerName(fe->controller));
                break;
            }
        }
    }

    LOG_INFO("Frontend initialized (%dx scale)", scale);
    return true;
}

void frontend_set_ff_indicator(Frontend* fe, bool active) {
    if (active) {
        SDL_SetWindowTitle(fe->window, "GBA Emulator [FF]");
    } else {
        SDL_SetWindowTitle(fe->window, "GBA Emulator");
    }
}

void frontend_set_pause_indicator(Frontend* fe, bool active) {
    if (active) {
        SDL_SetWindowTitle(fe->window, "GBA Emulator [PAUSED]");
    } else {
        SDL_SetWindowTitle(fe->window, fe->muted ? "GBA Emulator [MUTE]" : "GBA Emulator");
    }
}

void frontend_set_mute_indicator(Frontend* fe, bool active) {
    if (active) {
        SDL_SetWindowTitle(fe->window, "GBA Emulator [MUTE]");
    } else {
        SDL_SetWindowTitle(fe->window, "GBA Emulator");
    }
}

void frontend_overlay_clear(Frontend* fe) {
    if (!fe || !fe->overlay_buffer) return;
    memset(fe->overlay_buffer, 0,
           SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    fe->overlay_dirty = false;
}

void frontend_destroy(Frontend* fe) {
    if (fe->overlay_texture) SDL_DestroyTexture(fe->overlay_texture);
    if (fe->overlay_buffer) {
        free(fe->overlay_buffer);
        fe->overlay_buffer = NULL;
    }
    if (fe->texture) SDL_DestroyTexture(fe->texture);
    if (fe->renderer) SDL_DestroyRenderer(fe->renderer);
    if (fe->window) SDL_DestroyWindow(fe->window);
    if (fe->audio_device) SDL_CloseAudioDevice(fe->audio_device);
    if (fe->controller) SDL_GameControllerClose(fe->controller);
    SDL_Quit();
}

void frontend_present_frame(Frontend* fe, uint16_t* framebuffer) {
    SDL_UpdateTexture(fe->texture, NULL, framebuffer,
                      SCREEN_WIDTH * sizeof(uint16_t));
    SDL_RenderClear(fe->renderer);
    SDL_RenderCopy(fe->renderer, fe->texture, NULL, NULL);

    if (fe->overlay_dirty) {
        SDL_UpdateTexture(fe->overlay_texture, NULL, fe->overlay_buffer,
                          SCREEN_WIDTH * sizeof(uint32_t));
        SDL_RenderCopy(fe->renderer, fe->overlay_texture, NULL, NULL);
    }
    SDL_RenderPresent(fe->renderer);
}

// Map SDL scancodes to GBA buttons
static uint16_t sdl_to_gba_key(SDL_Scancode sc) {
    return keymap_lookup(sc);
}

void frontend_poll_input(Frontend* fe, GBA* gba) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        /* SDL_QUIT must always be honored, even when a modal is open. */
        if (event.type == SDL_QUIT) {
            fe->running = false;
            gba->running = false;
            continue;
        }

        /* If a slot-picker modal is open, route events to it first. */
        if (slot_picker_is_open(fe)) {
            if (slot_picker_handle_event(fe, gba, &event)) {
                continue;
            }
        }

        switch (event.type) {
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

            // F3 toggles input display HUD
            if (event.key.keysym.scancode == SDL_SCANCODE_F3 &&
                !event.key.repeat) {
                fe->input_display_enabled = !fe->input_display_enabled;
                LOG_INFO("Input display %s",
                         fe->input_display_enabled ? "ON" : "OFF");
            }

            // Save state hotkeys
            if (event.key.keysym.scancode == SDL_SCANCODE_F5) {
                fe->save_requested = true;
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F6 &&
                !event.key.repeat) {
                slot_picker_open_label_edit(fe);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F7 &&
                !event.key.repeat) {
                slot_picker_open_list(fe);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F8) {
                fe->load_requested = true;
            }

            // Screenshot hotkey
            if (event.key.keysym.scancode == SDL_SCANCODE_F12 &&
                !event.key.repeat) {
                fe->screenshot_requested = true;
            }

            // Slot selection (number keys 0-9)
            // SDL scancodes: 1-9 are 30-38, 0 is 39 (not contiguous)
            {
                int32_t slot = -1;
                SDL_Scancode sc = event.key.keysym.scancode;
                if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
                    slot = sc - SDL_SCANCODE_1 + 1;
                } else if (sc == SDL_SCANCODE_0) {
                    slot = 0;
                }
                if (slot >= 0 && slot != fe->savestate_slot) {
                    fe->savestate_slot = slot;
                    LOG_INFO("Save state slot: %d", slot);
                }
            }

            // Fast-forward: backtick toggles, Tab holds
            if (event.key.keysym.scancode == SDL_SCANCODE_GRAVE &&
                !event.key.repeat) {
                fe->ff_toggle = !fe->ff_toggle;
                LOG_INFO("Fast-forward %s", fe->ff_toggle ? "ON" : "OFF");
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_TAB) {
                fe->ff_hold = true;
            }
#ifdef ENABLE_REWIND
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) {
                fe->rewind_hold = true;
            }
#endif
            if (event.key.keysym.scancode == SDL_SCANCODE_F11 &&
                !event.key.repeat) {
                fe->fullscreen = !fe->fullscreen;
                SDL_SetWindowFullscreen(fe->window,
                    fe->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                LOG_INFO("Fullscreen %s", fe->fullscreen ? "ON" : "OFF");
            }

            // Pause toggle
            if (event.key.keysym.scancode == SDL_SCANCODE_SPACE &&
                !event.key.repeat) {
                fe->paused = !fe->paused;
                if (!fe->paused) fe->step_pending = false;  // discard any queued step
                LOG_INFO("Paused %s", fe->paused ? "ON" : "OFF");
            }

            // Frame advance: backslash steps one frame. Allow key-repeat so hold-to-step works.
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSLASH) {
                if (frame_advance_request(fe) == FRAME_ADVANCE_STEPPED) {
                    LOG_INFO("Frame advance");
                }
            }

            // Mute toggle
            if (event.key.keysym.scancode == SDL_SCANCODE_M &&
                !event.key.repeat) {
                fe->muted = !fe->muted;
                if (fe->audio_device) SDL_ClearQueuedAudio(fe->audio_device);
                frontend_set_mute_indicator(fe, fe->muted);
                LOG_INFO("Mute %s", fe->muted ? "ON" : "OFF");
            }
            break;
        }

        case SDL_KEYUP: {
            uint16_t key = sdl_to_gba_key(event.key.keysym.scancode);
            if (key) input_release(&gba->input, key);
            if (event.key.keysym.scancode == SDL_SCANCODE_TAB) {
                fe->ff_hold = false;
            }
#ifdef ENABLE_REWIND
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) {
                fe->rewind_hold = false;
            }
#endif
            break;
        }

        case SDL_CONTROLLERDEVICEADDED:
            if (!fe->controller && SDL_IsGameController(event.cdevice.which)) {
                fe->controller = SDL_GameControllerOpen(event.cdevice.which);
                if (fe->controller)
                    LOG_INFO("Controller connected: %s",
                             SDL_GameControllerName(fe->controller));
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (fe->controller &&
                event.cdevice.which ==
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(fe->controller))) {
                LOG_INFO("Controller disconnected");
                SDL_GameControllerClose(fe->controller);
                fe->controller = NULL;
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            uint16_t key = 0;
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A:             key = KEY_A;      break;
            case SDL_CONTROLLER_BUTTON_B:             key = KEY_B;      break;
            case SDL_CONTROLLER_BUTTON_X:             key = KEY_A;      break;
            case SDL_CONTROLLER_BUTTON_Y:             key = KEY_B;      break;
            case SDL_CONTROLLER_BUTTON_START:         key = KEY_START;  break;
            case SDL_CONTROLLER_BUTTON_BACK:          key = KEY_SELECT; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:       key = KEY_UP;     break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     key = KEY_DOWN;   break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     key = KEY_LEFT;   break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    key = KEY_RIGHT;  break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  key = KEY_L;      break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: key = KEY_R;      break;
            default: break;
            }
            if (key) {
                uint16_t prev = fe->controller_keys;
                if (event.type == SDL_CONTROLLERBUTTONDOWN)
                    fe->controller_keys |= key;
                else
                    fe->controller_keys &= ~key;
                uint16_t changed = prev ^ fe->controller_keys;
                if (changed & key) {
                    if (fe->controller_keys & key)
                        input_press(&gba->input, key);
                    else
                        input_release(&gba->input, key);
                }
            }
            break;
        }

        case SDL_CONTROLLERAXISMOTION: {
            #define AXIS_THRESHOLD 8000
            uint16_t prev = fe->controller_keys;
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                if (event.caxis.value < -AXIS_THRESHOLD)
                    fe->controller_keys |= KEY_LEFT;
                else
                    fe->controller_keys &= ~KEY_LEFT;
                if (event.caxis.value > AXIS_THRESHOLD)
                    fe->controller_keys |= KEY_RIGHT;
                else
                    fe->controller_keys &= ~KEY_RIGHT;
            } else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                if (event.caxis.value < -AXIS_THRESHOLD)
                    fe->controller_keys |= KEY_UP;
                else
                    fe->controller_keys &= ~KEY_UP;
                if (event.caxis.value > AXIS_THRESHOLD)
                    fe->controller_keys |= KEY_DOWN;
                else
                    fe->controller_keys &= ~KEY_DOWN;
            }
            uint16_t changed = prev ^ fe->controller_keys;
            if (changed) {
                uint16_t dirs[] = {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN};
                for (int i = 0; i < 4; i++) {
                    if (changed & dirs[i]) {
                        if (fe->controller_keys & dirs[i])
                            input_press(&gba->input, dirs[i]);
                        else
                            input_release(&gba->input, dirs[i]);
                    }
                }
            }
            #undef AXIS_THRESHOLD
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

    if (fe->muted) {
        /* Drain APU ring without queueing to SDL so emulation pacing still works */
        apu->read_pos = apu->write_pos;
        return;
    }

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
    if (fe->audio_device != 0 && !fe->muted) {
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
