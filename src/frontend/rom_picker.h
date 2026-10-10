#ifndef ROM_PICKER_H
#define ROM_PICKER_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ROM_PICK_OK,
    ROM_PICK_CANCELLED,
    ROM_PICK_UNAVAILABLE, // no file picker on this system (Linux without zenity/kdialog)
} RomPickResult;

/* Per-OS file picker filtered to .gba. Writes a UTF-8 path on ROM_PICK_OK. */
RomPickResult rom_picker_choose(char* out, size_t out_size);

/* Windows: drop the console when launched from Explorer. Returns true if it
 * did (stderr is then invisible). No-op returning false elsewhere. */
bool rom_picker_hide_console(void);

/* Startup without a ROM argument. Takes a ROM dropped on the app icon at
 * launch (macOS), otherwise shows the picker. Leaves SDL video initialized on
 * success; on failure it has already told the user and called SDL_Quit. */
bool rom_picker_launch(char* out, size_t out_size);

/* Error popup for users who launched without a terminal. Callers still log. */
void rom_picker_alert(const char* message);

#endif // ROM_PICKER_H
