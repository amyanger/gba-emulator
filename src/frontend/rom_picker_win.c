#include "frontend/rom_picker.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

RomPickResult rom_picker_choose(char* out, size_t out_size) {
    wchar_t path[1024] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"GBA ROMs (*.gba)\0*.gba\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.lpstrTitle = L"Choose a GBA ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return ROM_PICK_CANCELLED;

    /* The UTF-8 code page in the manifest makes fopen accept this. */
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)out_size, NULL, NULL);
    return n > 0 ? ROM_PICK_OK : ROM_PICK_CANCELLED;
}

/* Launched from Explorer (double-click or a ROM dropped on the .exe), the
 * console belongs to us alone; from a terminal it is shared. */
void rom_picker_hide_console(void) {
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) == 1) FreeConsole();
}
