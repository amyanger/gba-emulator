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

RomPickResult rom_picker_choose(char* out, size_t out_size) {
    for (size_t i = 0; i < sizeof(dialog_cmds) / sizeof(dialog_cmds[0]); i++) {
        FILE* p = popen(dialog_cmds[i], "r");
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

void rom_picker_hide_console(void) {}
