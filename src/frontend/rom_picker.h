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

/* Per-OS file picker filtered to .bin, for the GBA BIOS. Same contract as above. */
RomPickResult rom_picker_choose_bios(char* out, size_t out_size);

/* Per-OS file picker filtered to .nds, for the HeartGold/SoulSilver ROM. Same contract as above. */
RomPickResult rom_picker_choose_nds(char* out, size_t out_size);

/* Per-OS folder picker for the library. Writes a UTF-8 path on ROM_PICK_OK. */
RomPickResult rom_picker_choose_folder(char* out, size_t out_size);

/* macOS hands a ROM dropped on the app icon at launch to SDL as SDL_DROPFILE
 * right after video init: wait briefly for it. SDL video must be initialized.
 * Always false on other OSes, which pass it in argv. */
bool rom_picker_take_launch_drop(char* out, size_t out_size);

/* Error popup for users who launched without a terminal. Callers still log. */
void rom_picker_alert(const char* message);

#endif // ROM_PICKER_H
