#define _POSIX_C_SOURCE 200809L
#include "frontend/rom_picker.h"
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

/* Desktop dialog tools, tried in order. They are optional programs found at
 * runtime, not libraries we link. Fixed strings: no user input reaches the shell. */
static const char* const dialog_cmds[] = {
    "zenity --file-selection --title='Choose a GBA ROM' --file-filter='GBA ROMs | *.gba *.GBA' 2>/dev/null",
    "kdialog --title 'Choose a GBA ROM' --getopenfilename . 'GBA ROMs (*.gba *.GBA)' 2>/dev/null",
};

static const char* const folder_cmds[] = {
    "zenity --file-selection --directory --title='Choose your GBA games folder' 2>/dev/null",
    "kdialog --title 'Choose your GBA games folder' --getexistingdirectory . 2>/dev/null",
};

static RomPickResult run_dialogs(const char* const* cmds, size_t count, char* out, size_t out_size) {
    for (size_t i = 0; i < count; i++) {
        FILE* p = popen(cmds[i], "r");
        if (!p) continue;
        bool got = fgets(out, (int)out_size, p) != NULL;
        int status = pclose(p);
        /* The shell exits 127 when the tool isn't installed: try the next one. */
        if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) == 127) continue;
        if (WEXITSTATUS(status) != 0 || !got) return ROM_PICK_CANCELLED;
        char* nl = strchr(out, '\n');
        if (!nl) return ROM_PICK_CANCELLED; // path longer than the buffer
        *nl = '\0';
        return out[0] ? ROM_PICK_OK : ROM_PICK_CANCELLED;
    }
    return ROM_PICK_UNAVAILABLE;
}

RomPickResult rom_picker_choose(char* out, size_t out_size) {
    return run_dialogs(dialog_cmds, sizeof(dialog_cmds) / sizeof(dialog_cmds[0]), out, out_size);
}

RomPickResult rom_picker_choose_folder(char* out, size_t out_size) {
    return run_dialogs(folder_cmds, sizeof(folder_cmds) / sizeof(folder_cmds[0]), out, out_size);
}

bool rom_picker_hide_console(void) { return false; }
