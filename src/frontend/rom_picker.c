#include "frontend/rom_picker.h"
#include "common.h"
#include <SDL2/SDL.h>
#include <string.h>

/* macOS delivers "ROM dropped on the app icon" as SDL_DROPFILE right after
 * video init. Other OSes pass it as argv, so they skip the wait. */
#ifdef __APPLE__
#define LAUNCH_DROP_WAIT_MS 200
#else
#define LAUNCH_DROP_WAIT_MS 0
#endif

static bool take_launch_drop(char* out, size_t out_size) {
    uint32_t start = SDL_GetTicks();
    do {
        SDL_Event ev;
        SDL_PumpEvents();
        if (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_DROPFILE, SDL_DROPFILE) == 1) {
            size_t len = strlen(ev.drop.file);
            bool fits = len < out_size;
            if (fits) memcpy(out, ev.drop.file, len + 1);
            SDL_free(ev.drop.file);
            return fits;
        }
        SDL_Delay(10);
    } while (SDL_GetTicks() - start < LAUNCH_DROP_WAIT_MS);
    return false;
}

bool rom_picker_launch(char* out, size_t out_size) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG_ERROR("SDL init failed: %s", SDL_GetError());
        return false;
    }
    if (take_launch_drop(out, out_size)) return true;

    switch (rom_picker_choose(out, out_size)) {
    case ROM_PICK_OK:
        return true;
    case ROM_PICK_UNAVAILABLE:
        rom_picker_alert("No file picker was found on this system.\n\n"
                         "Open a ROM from a terminal instead:\n"
                         "  gba_emulator path/to/rom.gba");
        break;
    case ROM_PICK_CANCELLED:
        break;
    }
    SDL_Quit();
    return false;
}

void rom_picker_alert(const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GBA Emulator", message, NULL);
}
