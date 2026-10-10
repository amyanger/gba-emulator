#include "frontend.h"
#include "frame_advance.h"
#include "input_display.h"
#include "slot_picker.h"
#include "frontend/hud_draw.h"
#include "gba.h"
#include "apu/apu.h"
#include "input/input.h"
#include "input/keymap.h"
#include "ui/ui_theme.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
#endif

/* SDL reads these at video init, so call before the first SDL_Init. */
void frontend_set_hints(void) {
    /* Per-monitor DPI awareness so Windows does not bitmap-stretch the window.
     * With DPI scaling on, SDL reports window sizes in points like macOS. */
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
#ifdef _WIN32
    /* Use the exe's icon resource (ID 1, res/gba_emulator.rc) for the window. */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
}

bool frontend_init(Frontend* fe, int scale) {
    memset(fe, 0, sizeof(*fe));
    toast_init(&fe->toasts);

    frontend_set_hints();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        LOG_ERROR("SDL init failed: %s", SDL_GetError());
        return false;
    }

    fe->scale = scale;
    fe->window = SDL_CreateWindow("GBA Emulator", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH * scale,
                                  SCREEN_HEIGHT * scale,
                                  SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!fe->window) {
        LOG_ERROR("SDL window creation failed: %s", SDL_GetError());
        return false;
    }

    fe->renderer = SDL_CreateRenderer(fe->window, -1, SDL_RENDERER_ACCELERATED);
    if (!fe->renderer) {
        LOG_ERROR("SDL renderer creation failed: %s", SDL_GetError());
        return false;
    }
    /* Rects are computed in drawable pixels, so logical scaling stays off. */
    SDL_RenderSetLogicalSize(fe->renderer, 0, 0);

    // 15-bit color texture (ABGR1555)
    fe->texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ABGR1555,
                                    SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!fe->texture) {
        LOG_ERROR("SDL texture creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(fe->texture, SDL_ScaleModeNearest);

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

    /* The panel texture and buffer are sized by frontend_apply_layout. */
    fe->panel_visible = false;
    fe->panel_page = GAME_PAGE_AUTO;

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
    fe->audio_device = 0;
    fe->audio_target_bytes = 0;
    fe->controller_keys = 0;
    fe->slot_picker.mode = SLOT_PICKER_CLOSED;
    frontend_apply_layout(fe);

    fe->controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            fe->controller = SDL_GameControllerOpen(i);
            if (fe->controller) {
                LOG_INFO("Controller connected: %s",
                         SDL_GameControllerName(fe->controller));
                frontend_toast(fe, TOAST_INFO, "Controller connected",
                               SDL_GameControllerName(fe->controller));
                break;
            }
        }
    }

    LOG_INFO("Frontend initialized (%dx scale)", scale);
    return true;
}

/* Fills the density and display fields of a layout input for the window's current display. */
static void frontend_measure(Frontend* fe, LayoutInput* in) {
    int win_w = 0, win_h = 0, out_w = 0, out_h = 0;
    SDL_GetWindowSize(fe->window, &win_w, &win_h);
    SDL_GetRendererOutputSize(fe->renderer, &out_w, &out_h);
    in->pt_to_px = (win_w > 0 && out_w > 0) ? (float)out_w / (float)win_w : 1.0f;
    /* Snap to 1/8 so a measured 2.2498 is treated as 2.25. */
    in->pt_to_px = (float)(int)(in->pt_to_px * 8.0f + 0.5f) / 8.0f;
    if (in->pt_to_px <= 0.0f) in->pt_to_px = 1.0f;

    /* X11 has no point/pixel split, so the only density hint is DPI. SDL2 derives
     * X11 DPI from the monitor's physical size, not from the desktop scale setting,
     * so ordinary monitors report noisy values around 1.0-1.5x. Trust it only when
     * it clearly indicates a high-DPI screen (>= 1.75x); otherwise use 1.0. */
    in->os_scale = 1.0f;
    int display = SDL_GetWindowDisplayIndex(fe->window);
    if (display < 0) display = 0;
    const char* driver = SDL_GetCurrentVideoDriver();
    float hdpi = 0.0f;
    if (driver && strcmp(driver, "x11") == 0 &&
        SDL_GetDisplayDPI(display, NULL, &hdpi, NULL) == 0 && hdpi > 0.0f) {
        float s = (float)(int)(hdpi / 96.0f * 4.0f + 0.5f) / 4.0f;
        if (s > 3.0f) s = 3.0f;
        if (s >= 1.75f) in->os_scale = s;
    }

    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(display, &usable) == 0) {
        in->usable_w_pt = usable.w;
        in->usable_h_pt = usable.h;
    } else {
        in->usable_w_pt = in->usable_h_pt = 0;
    }
}

/* Moves the window back inside the usable area of the display it was on before a resize
 * grew it. Uses the planned size, since X11 reports the new size only later. */
static void frontend_keep_on_screen(Frontend* fe, int display) {
    SDL_Rect bounds;
    if (SDL_GetDisplayUsableBounds(display, &bounds) != 0) return;

    int x = 0, y = 0, top = 0;
    SDL_GetWindowPosition(fe->window, &x, &y);
    if (SDL_GetWindowBordersSize(fe->window, &top, NULL, NULL, NULL) != 0) {
        /* SDL2 has no Cocoa borders getter; y there is the content top, under a 28pt title bar. */
        const char* driver = SDL_GetCurrentVideoDriver();
        top = (driver && strcmp(driver, "cocoa") == 0) ? 28 : 0;
    }

    PxRect usable = {bounds.x, bounds.y, bounds.w, bounds.h};
    int new_x = x, new_y = y;
    panel_layout_clamp_window(&new_x, &new_y, fe->plan.window_w, fe->plan.window_h, top,
                              &usable);
    if (new_x != x || new_y != y) SDL_SetWindowPosition(fe->window, new_x, new_y);
}

/* Recomputes the plan and rects. resize_window is false when reacting to a size change
 * the window already went through, so a window manager that clamps our request cannot
 * bounce resize events back and forth. */
static void frontend_layout(Frontend* fe, bool resize_window) {
    LayoutInput in;
    in.game_scale = fe->scale;
    in.panel_visible = fe->panel_visible;
    frontend_measure(fe, &in);
    fe->plan = panel_layout_plan(&in);

    if (resize_window && !fe->fullscreen) {
        int cur_w = 0, cur_h = 0;
        SDL_GetWindowSize(fe->window, &cur_w, &cur_h);
        if (cur_w != fe->plan.window_w || cur_h != fe->plan.window_h) {
            int display = SDL_GetWindowDisplayIndex(fe->window);
            if (display < 0) display = 0;
            SDL_SetWindowSize(fe->window, fe->plan.window_w, fe->plan.window_h);
            frontend_keep_on_screen(fe, display);
            /* The new size or position can land on another display or change the pixel ratio. */
            frontend_measure(fe, &in);
            fe->plan = panel_layout_plan(&in);
        }
    }

    int out_w = 0, out_h = 0;
    SDL_GetRendererOutputSize(fe->renderer, &out_w, &out_h);
    panel_layout_rects(&fe->plan, fe->panel_visible, out_w, out_h, &fe->game_rect,
                       &fe->panel_rect);

    /* The canvas density follows the rect, which can be smaller than the plan when the
     * drawable is too small for the panel. */
    int full_w = (int)(PANEL_W_PT * fe->plan.density + 0.5f);
    int full_h = (int)(PANEL_H_PT * fe->plan.density + 0.5f);
    if (fe->panel_rect.w == full_w && fe->panel_rect.h == full_h) {
        fe->panel_density = fe->plan.density;
    } else {
        float dw = (float)fe->panel_rect.w / PANEL_W_PT;
        float dh = (float)fe->panel_rect.h / PANEL_H_PT;
        fe->panel_density = dw < dh ? dw : dh;
    }

    if (fe->panel_rect.w != fe->panel_px_w || fe->panel_rect.h != fe->panel_px_h) {
        if (fe->panel_texture) SDL_DestroyTexture(fe->panel_texture);
        fe->panel_texture = NULL;
        free(fe->panel_buffer);
        fe->panel_buffer = NULL;
        fe->panel_px_w = fe->panel_px_h = 0;
        if (fe->panel_rect.w > 0 && fe->panel_rect.h > 0) {
            fe->panel_buffer = (uint32_t*)calloc((size_t)fe->panel_rect.w * (size_t)fe->panel_rect.h,
                                                 sizeof(uint32_t));
            fe->panel_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                  SDL_TEXTUREACCESS_STREAMING,
                                                  fe->panel_rect.w, fe->panel_rect.h);
            if (!fe->panel_buffer || !fe->panel_texture) {
                LOG_ERROR("Failed to create the panel texture: %s", SDL_GetError());
                if (fe->panel_texture) SDL_DestroyTexture(fe->panel_texture);
                fe->panel_texture = NULL;
                free(fe->panel_buffer);
                fe->panel_buffer = NULL;
            } else {
                /* Drawn 1:1, so nearest keeps every pixel as rendered. */
                SDL_SetTextureScaleMode(fe->panel_texture, SDL_ScaleModeNearest);
                fe->panel_px_w = fe->panel_rect.w;
                fe->panel_px_h = fe->panel_rect.h;
            }
        }
    }
    fe->panel_dirty = true;

    int hud_w = fe->game_rect.w;
    int hud_h = (int)ceilf(hud_strip_height_pt() * fe->plan.density);
    if (hud_h > fe->game_rect.h) hud_h = fe->game_rect.h;
    if (hud_w != fe->hud_px_w || hud_h != fe->hud_px_h) {
        if (fe->hud_texture) SDL_DestroyTexture(fe->hud_texture);
        fe->hud_texture = NULL;
        free(fe->hud_buffer);
        fe->hud_buffer = NULL;
        fe->hud_px_w = fe->hud_px_h = 0;
        if (hud_w > 0 && hud_h > 0) {
            fe->hud_buffer = (uint32_t*)calloc((size_t)hud_w * (size_t)hud_h, sizeof(uint32_t));
            fe->hud_texture = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING, hud_w, hud_h);
            if (!fe->hud_buffer || !fe->hud_texture) {
                LOG_ERROR("Failed to create the HUD texture: %s", SDL_GetError());
                if (fe->hud_texture) SDL_DestroyTexture(fe->hud_texture);
                fe->hud_texture = NULL;
                free(fe->hud_buffer);
                fe->hud_buffer = NULL;
            } else {
                SDL_SetTextureBlendMode(fe->hud_texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(fe->hud_texture, SDL_ScaleModeNearest);
                fe->hud_px_w = hud_w;
                fe->hud_px_h = hud_h;
            }
        }
    }
    fe->hud_dirty = true;
}

void frontend_apply_layout(Frontend* fe) {
    frontend_layout(fe, true);
}

void frontend_toast(Frontend* fe, ToastKind kind, const char* title, const char* detail) {
    toast_push(&fe->toasts, SDL_GetTicks(), kind, title, detail);
}

void frontend_toast_keyed(Frontend* fe, const char* key, ToastKind kind, const char* title,
                          const char* detail) {
    toast_push_keyed(&fe->toasts, SDL_GetTicks(), key, kind, title, detail);
}

void frontend_update_hud(Frontend* fe, uint32_t now_ms) {
    toast_set_badge(&fe->toasts, BADGE_PAUSED, fe->paused);
    toast_set_badge(&fe->toasts, BADGE_FAST, fe->ff_hold || fe->ff_toggle);
    toast_set_badge(&fe->toasts, BADGE_MUTED, fe->muted);
    toast_tick(&fe->toasts, now_ms);
    fe->hud_shown = fe->hud_texture && hud_visible(&fe->toasts);
    if (!fe->hud_shown) {
        fe->hud_dirty = true;   /* redraw when something appears again */
        fe->toasts.changed = false;
        return;
    }
    if (fe->toasts.changed || fe->hud_dirty || toast_animating(&fe->toasts, now_ms)) {
        UiCanvas canvas;
        ui_canvas_init(&canvas, fe->hud_buffer, fe->hud_px_w, fe->hud_px_h, fe->plan.density);
        hud_draw(&canvas, &fe->toasts, now_ms);
        SDL_UpdateTexture(fe->hud_texture, NULL, fe->hud_buffer,
                          fe->hud_px_w * (int)sizeof(uint32_t));
        fe->toasts.changed = false;
        fe->hud_dirty = false;
    }
}

void frontend_destroy(Frontend* fe) {
    if (fe->overlay_texture) SDL_DestroyTexture(fe->overlay_texture);
    if (fe->overlay_buffer) {
        free(fe->overlay_buffer);
        fe->overlay_buffer = NULL;
    }
    if (fe->panel_texture) SDL_DestroyTexture(fe->panel_texture);
    if (fe->panel_buffer) {
        free(fe->panel_buffer);
        fe->panel_buffer = NULL;
    }
    if (fe->hud_texture) SDL_DestroyTexture(fe->hud_texture);
    free(fe->hud_buffer);
    fe->hud_buffer = NULL;
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
    /* Letterbox areas use the panel background so the window reads as one surface. */
    SDL_SetRenderDrawColor(fe->renderer, (UI_BG >> 16) & 0xFF, (UI_BG >> 8) & 0xFF,
                           UI_BG & 0xFF, 0xFF);
    SDL_RenderClear(fe->renderer);
    SDL_Rect game_rect = {fe->game_rect.x, fe->game_rect.y, fe->game_rect.w, fe->game_rect.h};
    SDL_RenderCopy(fe->renderer, fe->texture, NULL, &game_rect);

    if (fe->overlay_dirty) {
        SDL_UpdateTexture(fe->overlay_texture, NULL, fe->overlay_buffer,
                          SCREEN_WIDTH * sizeof(uint32_t));
        SDL_RenderCopy(fe->renderer, fe->overlay_texture, NULL, &game_rect);
    }

    if (fe->hud_shown && fe->hud_texture) {
        SDL_Rect hud_rect = {fe->game_rect.x, fe->game_rect.y, fe->hud_px_w, fe->hud_px_h};
        SDL_RenderCopy(fe->renderer, fe->hud_texture, NULL, &hud_rect);
    }

    if (fe->panel_visible && fe->panel_texture) {
        SDL_Rect panel_rect = {fe->panel_rect.x, fe->panel_rect.y, fe->panel_px_w, fe->panel_px_h};
        SDL_RenderCopy(fe->renderer, fe->panel_texture, NULL, &panel_rect);
    }
    SDL_RenderPresent(fe->renderer);
}

// Map SDL scancodes to GBA buttons
static uint16_t sdl_to_gba_key(SDL_Scancode sc) {
    return keymap_lookup(sc);
}

static void frontend_handle_window_event(Frontend* fe, const SDL_Event* event) {
#ifdef ENABLE_XRAY
    /* Handle X-Ray window close button */
    if (event->window.event == SDL_WINDOWEVENT_CLOSE &&
        g_xray && g_xray->window_id == event->window.windowID) {
        xray_toggle(g_xray);
    }
#endif
    if (event->window.windowID == SDL_GetWindowID(fe->window)) {
        if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            frontend_layout(fe, false);
        }
#if SDL_VERSION_ATLEAST(2, 0, 18)
        if (event->window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
            frontend_layout(fe, true);
        }
#endif
    }
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

        /* Window events come before the slot picker, which swallows everything,
         * so the layout still follows resizes while it is open. */
        if (event.type == SDL_WINDOWEVENT) {
            frontend_handle_window_event(fe, &event);
            continue;
        }

        /* If a slot-picker modal is open, route events to it first. */
        if (slot_picker_is_open(fe)) {
            if (slot_picker_handle_event(fe, gba, &event)) {
                continue;
            }
        }

        switch (event.type) {
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
                frontend_toast_keyed(fe, "input", TOAST_INFO,
                                     fe->input_display_enabled ? "Input display on" : "Input display off",
                                     NULL);
            }

            if (event.key.keysym.scancode == SDL_SCANCODE_F9 && !event.key.repeat) {
                fe->panel_visible = !fe->panel_visible;
                frontend_apply_layout(fe);
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F10 && !event.key.repeat) {
                fe->panel_page = (uint8_t)game_panel_next_page((GamePage)fe->panel_page);
                fe->panel_follow.restore = false;
            }
            /* [ and ] move the party selection unless the keymap binds them. */
            if ((event.key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET ||
                 event.key.keysym.scancode == SDL_SCANCODE_RIGHTBRACKET) &&
                fe->panel_visible && keymap_lookup(event.key.keysym.scancode) == 0 &&
                game_panel_resolve_page((GamePage)fe->panel_page, fe->last_snap.context) ==
                    GAME_PAGE_PARTY) {
                int delta = event.key.keysym.scancode == SDL_SCANCODE_LEFTBRACKET ? -1 : 1;
                fe->panel_sel = game_panel_select(fe->panel_sel, delta, fe->last_snap.party_count);
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
                    char detail[16];
                    snprintf(detail, sizeof(detail), "Slot %d", (int)slot);
                    frontend_toast(fe, TOAST_INFO, "Slot selected", detail);
                }
            }

            // Fast-forward: backtick toggles, Tab holds
            if (event.key.keysym.scancode == SDL_SCANCODE_GRAVE &&
                !event.key.repeat) {
                fe->ff_toggle = !fe->ff_toggle;
                LOG_INFO("Fast-forward %s", fe->ff_toggle ? "ON" : "OFF");
                frontend_toast_keyed(fe, "ff", TOAST_INFO,
                                     fe->ff_toggle ? "Fast forward on" : "Fast forward off", NULL);
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
                frontend_apply_layout(fe);
                LOG_INFO("Fullscreen %s", fe->fullscreen ? "ON" : "OFF");
                frontend_toast_keyed(fe, "fullscreen", TOAST_INFO,
                                     fe->fullscreen ? "Fullscreen on" : "Fullscreen off", NULL);
            }

            // Pause toggle
            if (event.key.keysym.scancode == SDL_SCANCODE_SPACE &&
                !event.key.repeat) {
                fe->paused = !fe->paused;
                if (!fe->paused) fe->step_pending = false;  // discard any queued step
                LOG_INFO("Paused %s", fe->paused ? "ON" : "OFF");
                frontend_toast_keyed(fe, "pause", TOAST_INFO, fe->paused ? "Paused" : "Resumed", NULL);
            }

            // Frame advance: backslash steps one frame. Allow key-repeat so hold-to-step works.
            if (event.key.keysym.scancode == SDL_SCANCODE_BACKSLASH) {
                if (frame_advance_request(fe) == FRAME_ADVANCE_STEPPED) {
                    LOG_INFO("Frame advance");
                    frontend_toast(fe, TOAST_INFO, "Frame advance", NULL);
                }
            }

            // Mute toggle
            if (event.key.keysym.scancode == SDL_SCANCODE_M &&
                !event.key.repeat) {
                fe->muted = !fe->muted;
                if (fe->audio_device) SDL_ClearQueuedAudio(fe->audio_device);
                LOG_INFO("Mute %s", fe->muted ? "ON" : "OFF");
                frontend_toast_keyed(fe, "mute", TOAST_INFO, fe->muted ? "Muted" : "Sound on", NULL);
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
                if (fe->controller) {
                    LOG_INFO("Controller connected: %s",
                             SDL_GameControllerName(fe->controller));
                    frontend_toast(fe, TOAST_INFO, "Controller connected",
                                   SDL_GameControllerName(fe->controller));
                }
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            if (fe->controller &&
                event.cdevice.which ==
                    SDL_JoystickInstanceID(
                        SDL_GameControllerGetJoystick(fe->controller))) {
                LOG_INFO("Controller disconnected");
                frontend_toast(fe, TOAST_INFO, "Controller disconnected", NULL);
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

    /* Frame sync drains the queue down to this target each frame. It must
     * comfortably exceed one device pull (have.samples frames), or every
     * callback empties the queue and SDL pads with silence — audio comes out
     * as stutter/gaps. 3 pulls keeps >= 2 pulls of margin after each drain. */
    fe->audio_target_bytes =
        3u * (uint32_t)have.samples * have.channels * (uint32_t)sizeof(int16_t);

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
         * Target is sized from the device pull size (see frontend_audio_init);
         * a fixed small target underruns on devices with large I/O buffers.
         * Cap iterations to avoid hanging if the audio device stalls. */
        uint32_t target = fe->audio_target_bytes;
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
