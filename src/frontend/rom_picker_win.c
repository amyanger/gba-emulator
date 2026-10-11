#include "frontend/rom_picker.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

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

RomPickResult rom_picker_choose_bios(char* out, size_t out_size) {
    wchar_t path[1024] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"GBA BIOS (*.bin)\0*.bin\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.lpstrTitle = L"Choose a GBA BIOS file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return ROM_PICK_CANCELLED;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)out_size, NULL, NULL);
    return n > 0 ? ROM_PICK_OK : ROM_PICK_CANCELLED;
}

RomPickResult rom_picker_choose_nds(char* out, size_t out_size) {
    wchar_t path[1024] = L"";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Nintendo DS ROMs (*.nds)\0*.nds\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = (DWORD)(sizeof(path) / sizeof(path[0]));
    ofn.lpstrTitle = L"Choose your HeartGold or SoulSilver ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return ROM_PICK_CANCELLED;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)out_size, NULL, NULL);
    return n > 0 ? ROM_PICK_OK : ROM_PICK_CANCELLED;
}

RomPickResult rom_picker_choose_folder(char* out, size_t out_size) {
    wchar_t path[MAX_PATH] = L"";
    BROWSEINFOW bi = {0};
    bi.lpszTitle = L"Choose your GBA games folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    /* The new-style dialog needs COM on this thread. */
    HRESULT co = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&bi);
    bool ok = item && SHGetPathFromIDListW(item, path);
    if (item) CoTaskMemFree(item);
    if (SUCCEEDED(co)) CoUninitialize();
    if (!ok) return ROM_PICK_CANCELLED;
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)out_size, NULL, NULL);
    return n > 0 ? ROM_PICK_OK : ROM_PICK_CANCELLED;
}

/* Launched from Explorer (double-click or a ROM dropped on the .exe), the
 * console belongs to us alone; from a terminal it is shared. */
bool rom_picker_hide_console(void) {
    DWORD pids[2];
    if (GetConsoleProcessList(pids, 2) != 1) return false;
    FreeConsole();
    return true;
}
