#include "frontend/library_run.h"
#include "frontend/library_view.h"
#include "frontend/rom_picker.h"
#include "screenshot/screenshot.h"
#include "common.h"
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DESIGN_W 720.0f
#define DESIGN_H 480.0f

/* Large: keep off the stack. */
static Library s_record;
static LibView s_view;

bool library_file_path(char* out, size_t out_size) {
    char* dir = SDL_GetPrefPath("", "GBA Emulator");
    if (!dir) return false;
    int n = snprintf(out, out_size, "%slibrary.txt", dir);
    SDL_free(dir);
    return n > 0 && (size_t)n < out_size;
}

void library_prepare(Library* lib, const char* default_folder) {
    char file[LIB_PATH_LEN];
    library_init(lib, library_file_path(file, sizeof(file)) ? file : NULL);
    library_load(lib);
    if (default_folder && library_default_folder(lib, default_folder)) library_save(lib);
}

void library_record_launch(const char* rom_path) {
    char abs[LIB_PATH_LEN];
    if (!library_abspath(rom_path, abs, sizeof(abs))) return;
    library_prepare(&s_record, NULL);
    library_add_recent(&s_record, abs, (int64_t)time(NULL));
    if (!library_save(&s_record)) LOG_WARN("Couldn't save the recent games list");
}

/* Pixels per point, so the 720x480 design fills the window however it was scaled. */
static float library_density(int out_w, int out_h, int win_w) {
    float d = win_w > 0 ? (float)out_w / (float)win_w : 1.0f;
    d = (float)(int)(d * 8.0f + 0.5f) / 8.0f;
    float fit_w = (float)out_w / DESIGN_W, fit_h = (float)out_h / DESIGN_H;
    float fit = fit_w < fit_h ? fit_w : fit_h;
    if (fit < d) d = fit;
    return d < 0.5f ? 0.5f : d;
}

typedef struct {
    SDL_Texture* tex;
    uint32_t* px;
    int w, h;
    float density;
} Surface;

static void surface_free(Surface* s) {
    if (s->tex) SDL_DestroyTexture(s->tex);
    free(s->px);
    memset(s, 0, sizeof(*s));
}

static bool surface_fit(Frontend* fe, Surface* s) {
    int out_w = 0, out_h = 0, win_w = 0, win_h = 0;
    SDL_GetRendererOutputSize(fe->renderer, &out_w, &out_h);
    SDL_GetWindowSize(fe->window, &win_w, &win_h);
    s->density = library_density(out_w, out_h, win_w);
    if (out_w == s->w && out_h == s->h && s->tex) return true;
    surface_free(s);
    if (out_w <= 0 || out_h <= 0) return false;
    s->px = (uint32_t*)calloc((size_t)out_w * (size_t)out_h, sizeof(uint32_t));
    s->tex = SDL_CreateTexture(fe->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, out_w, out_h);
    if (!s->px || !s->tex) {
        LOG_ERROR("Couldn't create the library screen: %s", SDL_GetError());
        surface_free(s);
        return false;
    }
    s->w = out_w;
    s->h = out_h;
    return true;
}

/* Window coordinates -> library points. */
static void to_points(Frontend* fe, const Surface* s, int wx, int wy, float* x, float* y) {
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(fe->window, &win_w, &win_h);
    float px_per_unit = win_w > 0 ? (float)s->w / (float)win_w : 1.0f;
    *x = (float)wx * px_per_unit / s->density;
    *y = (float)wy * px_per_unit / s->density;
}

static bool key_action(const SDL_KeyboardEvent* k, LibKey* out) {
    bool cmd = (k->keysym.mod & (KMOD_CTRL | KMOD_GUI)) != 0;
    switch (k->keysym.scancode) {
    case SDL_SCANCODE_UP:       *out = LIB_KEY_UP; return true;
    case SDL_SCANCODE_DOWN:     *out = LIB_KEY_DOWN; return true;
    case SDL_SCANCODE_PAGEUP:   *out = LIB_KEY_PAGE_UP; return true;
    case SDL_SCANCODE_PAGEDOWN: *out = LIB_KEY_PAGE_DOWN; return true;
    case SDL_SCANCODE_HOME:     *out = LIB_KEY_HOME; return true;
    case SDL_SCANCODE_END:      *out = LIB_KEY_END; return true;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: *out = LIB_KEY_ENTER; return !k->repeat;
    case SDL_SCANCODE_ESCAPE:   *out = LIB_KEY_ESCAPE; return !k->repeat;
    case SDL_SCANCODE_O:        *out = LIB_KEY_OPEN; return cmd && !k->repeat;
    default:                    return false;
    }
}

static bool copy_out(const char* path, char* out, size_t out_size) {
    size_t n = strlen(path);
    if (n >= out_size) return false;
    memcpy(out, path, n + 1);
    return true;
}

static void set_error(LibView* v, const char* msg) {
    snprintf(v->error, sizeof(v->error), "%s", msg);
}

bool library_run(Frontend* fe, Library* lib, const char* error, char* out, size_t out_size) {
    Surface surf = {0};
    LibView* v = &s_view;
    bool picked = false, done = false, dirty = true;

    memset(v, 0, sizeof(*v));
    if (error) set_error(v, error);
    library_scan(lib);
    if (!surface_fit(fe, &surf)) return false;
    library_view_reset(v, lib, (float)surf.w / surf.density, (float)surf.h / surf.density);

    while (!done) {
        if (dirty) {
            UiCanvas canvas;
            ui_canvas_init(&canvas, surf.px, surf.w, surf.h, surf.density);
            library_view_draw(&canvas, v, lib, (int64_t)time(NULL));
            SDL_UpdateTexture(surf.tex, NULL, surf.px, surf.w * (int)sizeof(uint32_t));
            SDL_RenderClear(fe->renderer);
            SDL_RenderCopy(fe->renderer, surf.tex, NULL, NULL);
            SDL_RenderPresent(fe->renderer);
            dirty = false;
        }

        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 1000)) {
            dirty = true;  /* keep "min ago" fresh */
            continue;
        }
        LibAction act = LIB_ACT_NONE;
        float x, y;
        LibKey key;
        switch (e.type) {
        case SDL_QUIT:
            act = LIB_ACT_QUIT;
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_EXPOSED ||
                e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED) {
                if (!surface_fit(fe, &surf)) {
                    done = true;
                    break;
                }
                v->w = (float)surf.w / surf.density;
                v->h = (float)surf.h / surf.density;
                library_view_wheel(v, lib, 0.0f);  /* re-clamp the scroll */
                dirty = true;
            }
            break;
        case SDL_KEYDOWN:
            if (key_action(&e.key, &key)) act = library_view_key(v, lib, key);
            else if (!(e.key.keysym.mod & (KMOD_CTRL | KMOD_GUI | KMOD_ALT))) {
                SDL_Keycode sym = e.key.keysym.sym;
                if ((sym >= SDLK_a && sym <= SDLK_z) || (sym >= SDLK_0 && sym <= SDLK_9))
                    act = library_view_char(v, lib, (char)sym);
            }
            break;
        case SDL_MOUSEMOTION:
            to_points(fe, &surf, e.motion.x, e.motion.y, &x, &y);
            act = library_view_mouse_move(v, lib, x, y);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button != SDL_BUTTON_LEFT) break;
            to_points(fe, &surf, e.button.x, e.button.y, &x, &y);
            act = library_view_click(v, lib, x, y, SDL_GetTicks());
            break;
        case SDL_MOUSEWHEEL:
#if SDL_VERSION_ATLEAST(2, 0, 18)
            act = library_view_wheel(v, lib, e.wheel.preciseY);
#else
            act = library_view_wheel(v, lib, (float)e.wheel.y);
#endif
            break;
        case SDL_DROPFILE:
            picked = copy_out(e.drop.file, out, out_size);
            SDL_free(e.drop.file);
            done = picked;
            break;
        default:
            break;
        }

        switch (act) {
        case LIB_ACT_REDRAW:
            dirty = true;
            break;
        case LIB_ACT_PLAY: {
            const char* path = library_view_selected_path(v, lib);
            if (path && copy_out(path, out, out_size)) picked = done = true;
            break;
        }
        case LIB_ACT_OPEN_FILE:
            switch (rom_picker_choose(out, out_size)) {
            case ROM_PICK_OK: picked = done = true; break;
            case ROM_PICK_UNAVAILABLE: set_error(v, "No file picker found on this system"); break;
            case ROM_PICK_CANCELLED: break;
            }
            dirty = true;
            break;
        case LIB_ACT_CHOOSE_FOLDER: {
            char folder[LIB_PATH_LEN];
            RomPickResult r = rom_picker_choose_folder(folder, sizeof(folder));
            if (r == ROM_PICK_OK && library_set_folder(lib, folder)) {
                if (!library_save(lib)) LOG_WARN("Couldn't save the library folder");
                library_scan(lib);
                v->error[0] = '\0';
                library_view_reset(v, lib, v->w, v->h);
            } else if (r == ROM_PICK_UNAVAILABLE) {
                set_error(v, "No folder picker found on this system");
            }
            dirty = true;
            break;
        }
        case LIB_ACT_QUIT:
            done = true;
            break;
        default:
            break;
        }
    }
    surface_free(&surf);
    return picked;
}

bool library_render_png(const char* folder, float density, const char* png_path) {
    static Library lib;
    library_init(&lib, NULL);
    if (folder) library_set_folder(&lib, folder);
    library_scan(&lib);
    memset(&s_view, 0, sizeof(s_view));
    library_view_reset(&s_view, &lib, DESIGN_W, DESIGN_H);
    int w = (int)(DESIGN_W * density + 0.5f), h = (int)(DESIGN_H * density + 0.5f);
    uint32_t* px = (uint32_t*)calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if (!px) return false;
    UiCanvas canvas;
    ui_canvas_init(&canvas, px, w, h, density);
    library_view_draw(&canvas, &s_view, &lib, (int64_t)time(NULL));
    bool ok = screenshot_save_argb(px, w, h, png_path);
    free(px);
    return ok;
}
