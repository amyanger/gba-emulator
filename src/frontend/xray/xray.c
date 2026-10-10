#include "xray.h"
#include "gba.h"

static bool xray_create_window(XRayState* state) {
    /* Open on the display holding the game window (it has keyboard focus when F2 is pressed). */
    int display = 0;
    SDL_Window* focus = SDL_GetKeyboardFocus();
    if (focus) {
        int d = SDL_GetWindowDisplayIndex(focus);
        if (d >= 0) display = d;
    }

    state->window = SDL_CreateWindow("GBA X-Ray", SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                                     SDL_WINDOWPOS_CENTERED_DISPLAY(display), XRAY_W_PT,
                                     XRAY_H_PT, SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!state->window) {
        LOG_ERROR("X-Ray: SDL window creation failed: %s", SDL_GetError());
        return false;
    }

    state->renderer = SDL_CreateRenderer(state->window, -1,
                                         SDL_RENDERER_ACCELERATED);
    if (!state->renderer) {
        LOG_ERROR("X-Ray: SDL renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(state->window);
        state->window = NULL;
        return false;
    }

    /* The pixel ratio is only known once the window exists. */
    int win_w = 0, win_h = 0, out_w = 0, out_h = 0;
    SDL_GetWindowSize(state->window, &win_w, &win_h);
    SDL_GetRendererOutputSize(state->renderer, &out_w, &out_h);
    float ratio = win_w > 0 ? (float)out_w / (float)win_w : 1.0f;

    SDL_Rect usable = {0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(display, &usable) != 0) usable.w = usable.h = 0;
    XRayFit fit = xray_fit(usable.w, usable.h, ratio);
    SDL_SetWindowSize(state->window, fit.win_w, fit.win_h);
    if (usable.w > 0) {
        /* Center in the usable area so the title bar stays below the menu bar. */
        int room_h = usable.h - XRAY_TITLEBAR_PT - fit.win_h;
        SDL_SetWindowPosition(state->window, usable.x + (usable.w - fit.win_w) / 2,
                              usable.y + XRAY_TITLEBAR_PT + (room_h > 0 ? room_h / 2 : 0));
    }
    SDL_ShowWindow(state->window);

    ui_font_init();
    state->window_id = SDL_GetWindowID(state->window);
    state->ever_opened = true;
    LOG_INFO("X-Ray window created (%dx%d pt, scale %.2f)", fit.win_w, fit.win_h,
             (double)fit.scale);
    return true;
}

static void xray_destroy_window(XRayState* state) {
    if (state->texture) {
        SDL_DestroyTexture(state->texture);
        state->texture = NULL;
    }
    state->tex_w = 0;
    state->tex_h = 0;
    if (state->renderer) {
        SDL_DestroyRenderer(state->renderer);
        state->renderer = NULL;
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
        state->window = NULL;
    }
    state->window_id = 0;
}

void xray_destroy(XRayState* state) {
    xray_destroy_window(state);
}

void xray_toggle(XRayState* state) {
    if (state->active) {
        /* Hide the window */
        state->active = false;
        if (state->window) {
            SDL_HideWindow(state->window);
        }
        LOG_INFO("X-Ray: disabled");
    } else {
        /* Show (or create) the window */
        if (!state->window) {
            if (!xray_create_window(state)) return;
        } else {
            SDL_ShowWindow(state->window);
            SDL_RaiseWindow(state->window);
        }
        state->active = true;
        LOG_INFO("X-Ray: enabled");
    }
}

void xray_render(XRayState* state, GBA* gba) {
    if (!state->active || !state->window) return;

    xray_decay_flash(state);
    xray_capture_ppu_layers(&gba->ppu, state);
    xray_capture_audio(&gba->apu, state);

    int out_w = 0, out_h = 0, cw, ch;
    float density;
    SDL_GetRendererOutputSize(state->renderer, &out_w, &out_h);
    if (out_w <= 0 || out_h <= 0) return;
    xray_canvas_size(out_w, out_h, &cw, &ch, &density);

    /* Recreated when the window moves to a display with a different density. */
    if (!state->texture || cw != state->tex_w || ch != state->tex_h) {
        if (state->texture) SDL_DestroyTexture(state->texture);
        state->texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_ARGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, cw, ch);
        if (!state->texture) {
            LOG_ERROR("X-Ray: SDL texture creation failed: %s", SDL_GetError());
            return;
        }
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(state->texture, SDL_ScaleModeLinear);
#endif
        state->tex_w = cw;
        state->tex_h = ch;
    }

    UiCanvas canvas;
    ui_canvas_init(&canvas, state->framebuffer, cw, ch, density);
    xray_draw(&canvas, gba, state);

    SDL_UpdateTexture(state->texture, NULL, state->framebuffer, cw * (int)sizeof(uint32_t));
    SDL_RenderClear(state->renderer);
    SDL_RenderCopy(state->renderer, state->texture, NULL, NULL);
    SDL_RenderPresent(state->renderer);
}
