#include "frontend/rom_picker.h"

RomPickResult rom_picker_choose(char* out, size_t out_size) {
    (void)out;
    (void)out_size;
    return ROM_PICK_UNAVAILABLE;
}

void rom_picker_hide_console(void) {}
