#ifndef _WIN32
#define _DEFAULT_SOURCE  /* realpath, d_type-free dirent */
#endif
#include "frontend/library.h"
#include "cartridge/cartridge.h"
#include "cartridge/rom_zip.h"
#include "game/game_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#endif

#define LIB_HEADER_TITLE 0xA0
#define LIB_HEADER_CODE  0xAC

static void copy_str(char* dst, size_t size, const char* src) {
    if (size == 0) return;
    size_t n = strlen(src);
    if (n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void library_init(Library* lib, const char* file) {
    memset(lib, 0, sizeof(*lib));
    lib->banner = -1;
    if (file && strlen(file) < sizeof(lib->file)) copy_str(lib->file, sizeof(lib->file), file);
}

/* ---- library.txt ---- */

bool library_load(Library* lib) {
    char line[LIB_PATH_LEN + 64];
    if (!lib->file[0]) return false;
    FILE* f = fopen(lib->file, "r");
    if (!f) return false;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] != '\n' && !feof(f)) {
            /* Longer than any path we store: skip the rest of it. */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {}
            continue;
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (strncmp(line, "folder=", 7) == 0) {
            if (strlen(line + 7) < LIB_PATH_LEN) copy_str(lib->folder, sizeof(lib->folder), line + 7);
        } else if (strncmp(line, "recent=", 7) == 0 && lib->recent_count < LIB_RECENTS) {
            char* bar = strchr(line + 7, '|');
            char* end;
            if (!bar || !bar[1] || strlen(bar + 1) >= LIB_PATH_LEN) continue;
            long long when = strtoll(line + 7, &end, 10);
            if (end != bar || when < 0) continue;
            LibRecent* r = &lib->recents[lib->recent_count++];
            r->when = when;
            copy_str(r->path, sizeof(r->path), bar + 1);
        }
    }
    fclose(f);
    return true;
}

bool library_save(const Library* lib) {
    char tmp[LIB_PATH_LEN + 8];
    if (!lib->file[0]) return false;
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", lib->file);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return false;
    FILE* f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = true;
    if (lib->folder[0]) ok &= fprintf(f, "folder=%s\n", lib->folder) >= 0;
    for (uint32_t i = 0; i < lib->recent_count; i++)
        ok &= fprintf(f, "recent=%lld|%s\n", (long long)lib->recents[i].when, lib->recents[i].path) >= 0;
    ok &= fclose(f) == 0;
    if (!ok) {
        remove(tmp);
        return false;
    }
#ifdef _WIN32
    wchar_t wtmp[LIB_PATH_LEN + 8], wdst[LIB_PATH_LEN];
    if (!MultiByteToWideChar(CP_UTF8, 0, tmp, -1, wtmp, LIB_PATH_LEN + 8) ||
        !MultiByteToWideChar(CP_UTF8, 0, lib->file, -1, wdst, LIB_PATH_LEN))
        return false;
    return MoveFileExW(wtmp, wdst, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(tmp, lib->file) == 0;
#endif
}

bool library_set_folder(Library* lib, const char* folder) {
    if (!folder || !folder[0] || strlen(folder) >= sizeof(lib->folder)) return false;
    /* Absolute, so folder games and recents name the same file the same way. */
    if (!library_abspath(folder, lib->folder, sizeof(lib->folder)))
        copy_str(lib->folder, sizeof(lib->folder), folder);
    return true;
}

bool library_default_folder(Library* lib, const char* folder) {
    if (lib->folder[0]) return false;
    return library_set_folder(lib, folder);
}

void library_add_recent(Library* lib, const char* path, int64_t now) {
    if (!path || !path[0] || strlen(path) >= LIB_PATH_LEN) return;
    uint32_t keep = 0;
    LibRecent rest[LIB_RECENTS];
    for (uint32_t i = 0; i < lib->recent_count; i++)
        if (strcmp(lib->recents[i].path, path) != 0 && keep < LIB_RECENTS - 1) rest[keep++] = lib->recents[i];
    lib->recents[0].when = now;
    copy_str(lib->recents[0].path, sizeof(lib->recents[0].path), path);
    memcpy(&lib->recents[1], rest, keep * sizeof(rest[0]));
    lib->recent_count = keep + 1;
}

/* ---- Headers and names ---- */

void library_parse_header(const uint8_t* hdr, uint32_t len, char title[13], char code[5]) {
    title[0] = code[0] = '\0';
    if (len < LIB_HEADER_CODE + 4) return;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 12; i++) {
        uint8_t ch = hdr[LIB_HEADER_TITLE + i];
        if (ch == 0) break;
        title[n++] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?';
    }
    while (n > 0 && title[n - 1] == ' ') n--;
    title[n] = '\0';
    bool printable = true;
    for (uint32_t i = 0; i < 4; i++) {
        uint8_t ch = hdr[LIB_HEADER_CODE + i];
        if (ch < 0x21 || ch > 0x7E) printable = false;
    }
    if (printable) {
        memcpy(code, hdr + LIB_HEADER_CODE, 4);
        code[4] = '\0';
    }
}

/* Whether the first n bytes of s end in ext (lowercase), ignoring ASCII case. */
static bool ends_with(const char* s, size_t n, const char* ext) {
    size_t e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++)
        if ((s[n - e + i] | 0x20) != ext[i]) return false;
    return true;
}

static const char* base_name(const char* path) {
    const char* s = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    if (b && (!s || b > s)) s = b;
    return s ? s + 1 : path;
}

void library_display_name(const char* path, char* out, size_t out_size) {
    const char* base = base_name(path);
    const char* dot = strrchr(base, '.');
    size_t len = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    if (ends_with(base, len, ".gba") && len > 4) len -= 4; /* "game.gba.zip" */
    size_t n = 0;
    bool word_start = true;
    if (out_size == 0) return;
    for (size_t i = 0; i < len && n + 1 < out_size; i++) {
        char ch = base[i];
        if (ch == '_' || ch == '-') ch = ' ';
        if (word_start && ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        word_start = ch == ' ';
        out[n++] = ch;
    }
    /* Never end on a partial UTF-8 sequence when the name is cut. */
    if (n < len)
        while (n > 0 && ((unsigned char)base[n] & 0xC0) == 0x80) n--;
    out[n] = '\0';
}

bool library_is_gba(const char* name) {
    size_t n = strlen(name);
    if (n < 5 || name[0] == '.') return false;
    return ends_with(name, n, ".gba") || ends_with(name, n, ".zip");
}

bool library_abspath(const char* path, char* out, size_t out_size) {
#ifdef _WIN32
    wchar_t wpath[LIB_PATH_LEN], wfull[LIB_PATH_LEN];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, LIB_PATH_LEN)) return false;
    DWORD n = GetFullPathNameW(wpath, LIB_PATH_LEN, wfull, NULL);
    if (n == 0 || n >= LIB_PATH_LEN) return false;
    return WideCharToMultiByte(CP_UTF8, 0, wfull, -1, out, (int)out_size, NULL, NULL) > 0;
#else
    char buf[PATH_MAX];
    if (!realpath(path, buf) || strlen(buf) >= out_size) return false;
    copy_str(out, out_size, buf);
    return true;
#endif
}

/* ---- Scanning ---- */

static FILE* open_file(const char* path) {
    return fopen(path, "rb");  /* UTF-8 paths work on Windows through the manifest code page */
}

/* Reads n bytes at off into buf + off; false on a short read. */
static bool read_at(FILE* f, uint8_t* buf, long off, long n) {
    return fseek(f, off, SEEK_SET) == 0 && fread(buf + off, 1, (size_t)n, f) == (size_t)n;
}

/* The game header, size and CRC of a zipped game, from the zip's directory, so nothing past
 * the header is unpacked. When the zip's first file is the game, only the zip's start and end
 * are read and the buffer between stays zero; any other zip, or one that looks damaged that
 * way (a directory bigger than the end read), is read whole. */
static bool read_zip_game(FILE* f, long size, LibGame* g) {
    if (size <= 0 || size > ROM_ZIP_MAX_FILE) return false;
    uint8_t* zip = (uint8_t*)calloc(1, (size_t)size);
    if (!zip) return false;
    uint8_t hdr[LIB_HEADER_LEN];
    char name[256];
    uint32_t got = 0, game_size = 0, crc = 0, rc = ROM_ZIP_CANT_READ;
    long head = size < 0x10000 ? size : 0x10000, tail = size < 0x20000 ? size : 0x20000;
    bool sparse = read_at(f, zip, 0, head) && read_at(f, zip, size - tail, tail);
    uint32_t nlen = head >= 30 ? (uint32_t)(zip[26] | zip[27] << 8) : 0;
    // shortcut: assumes the game's first 0xC0 bytes are packed within the first 64 KB.
    if (sparse && nlen >= 4 && 30 + nlen <= (uint32_t)head &&
        ends_with((const char*)zip + 30, nlen, ".gba"))
        rc = rom_zip_read(zip, (uint32_t)size, hdr, sizeof(hdr), &got, &game_size, &crc, name, sizeof(name));
    if ((rc == ROM_ZIP_CANT_READ || rc == ROM_ZIP_DAMAGED) && size > head + tail && read_at(f, zip, 0, size))
        rc = rom_zip_read(zip, (uint32_t)size, hdr, sizeof(hdr), &got, &game_size, &crc, name, sizeof(name));
    free(zip);
    if (rc != ROM_ZIP_OK) return false; /* not listed: no game inside, or unreadable */
    library_parse_header(hdr, got, g->title, g->code);
    const GameProfile* p = game_profile_find(g->code);
    g->size = game_size;
    // The CRC comes from the zip's directory; loading the game checks it again.
    g->companion = p && got > 0xBC && hdr[0xBC] == p->version && game_size == p->rom_size &&
                   crc == p->rom_crc32;
    return true;
}

/* Reads the header and size; for files that look like a supported ROM, checks the whole CRC. */
static bool read_game(const char* path, LibGame* g) {
    static uint8_t chunk[64 * 1024];
    FILE* f = open_file(path);
    if (!f) return false;
    memset(g, 0, sizeof(*g));
    copy_str(g->path, sizeof(g->path), path);
    library_display_name(path, g->name, sizeof(g->name));
    size_t got = fread(chunk, 1, LIB_HEADER_LEN, f);
    if (got >= 4 && memcmp(chunk, "PK\x03\x04", 4) == 0 && fseek(f, 0, SEEK_END) == 0) {
        bool ok = read_zip_game(f, ftell(f), g);
        fclose(f);
        return ok;
    }
    library_parse_header(chunk, (uint32_t)got, g->title, g->code);
    const GameProfile* p = game_profile_find(g->code);
    bool version_ok = p && got > 0xBC && chunk[0xBC] == p->version;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        g->size = size > 0 ? (uint32_t)size : 0;
    }
    if (version_ok && g->size == p->rom_size && fseek(f, 0, SEEK_SET) == 0) {
        uint32_t crc = 0;
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) crc = game_crc32_update(crc, chunk, (uint32_t)n);
        g->companion = crc == p->rom_crc32;
    }
    fclose(f);
    return true;
}

static bool file_exists(const char* path) {
    FILE* f = open_file(path);
    if (!f) return false;
    fclose(f);
    return true;
}

static bool has_game(const Library* lib, const char* path) {
    for (uint32_t i = 0; i < lib->game_count; i++)
        if (strcmp(lib->games[i].path, path) == 0) return true;
    return false;
}

static void add_folder_file(Library* lib, const char* name) {
    char path[LIB_PATH_LEN];
    if (lib->folder_games >= LIB_FOLDER_MAX || !library_is_gba(name)) return;
    size_t fl = strlen(lib->folder);
    bool sep = fl > 0 && (lib->folder[fl - 1] == '/' || lib->folder[fl - 1] == '\\');
#ifdef _WIN32
    const char* slash = "\\";  /* matches GetFullPathNameW, so recents compare equal */
#else
    const char* slash = "/";
#endif
    int n = snprintf(path, sizeof(path), "%s%s%s", lib->folder, sep ? "" : slash, name);
    if (n < 0 || (size_t)n >= sizeof(path)) return;
    if (read_game(path, &lib->games[lib->game_count])) {
        lib->game_count++;
        lib->folder_games++;
    }
}

static void scan_folder(Library* lib) {
#ifdef _WIN32
    wchar_t pattern[LIB_PATH_LEN + 4];
    char spec[LIB_PATH_LEN + 4];
    int n = snprintf(spec, sizeof(spec), "%s\\*", lib->folder);
    if (n < 0 || (size_t)n >= sizeof(spec) || !MultiByteToWideChar(CP_UTF8, 0, spec, -1, pattern, LIB_PATH_LEN + 4)) {
        lib->folder_missing = true;
        return;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        lib->folder_missing = GetLastError() != ERROR_FILE_NOT_FOUND;
        return;
    }
    do {
        char name[LIB_PATH_LEN];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof(name), NULL, NULL) > 0)
            add_folder_file(lib, name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(lib->folder);
    if (!d) {
        lib->folder_missing = true;
        return;
    }
    struct dirent* e;
    while ((e = readdir(d)) != NULL) add_folder_file(lib, e->d_name);
    closedir(d);
#endif
}

static int compare_games(const void* a, const void* b) {
    const LibGame* x = (const LibGame*)a;
    const LibGame* y = (const LibGame*)b;
    for (size_t i = 0;; i++) {
        int cx = (unsigned char)x->name[i], cy = (unsigned char)y->name[i];
        if (cx >= 'A' && cx <= 'Z') cx += 'a' - 'A';
        if (cy >= 'A' && cy <= 'Z') cy += 'a' - 'A';
        if (cx != cy) return cx - cy;
        if (cx == 0) break;
    }
    return strcmp(x->path, y->path);
}

void library_scan(Library* lib) {
    lib->game_count = 0;
    lib->folder_games = 0;
    lib->folder_missing = false;
    lib->banner = -1;
    if (lib->folder[0]) scan_folder(lib);

    /* Recents: drop the ones whose file is gone, add those outside the folder. */
    uint32_t keep = 0;
    for (uint32_t i = 0; i < lib->recent_count; i++) {
        const LibRecent* r = &lib->recents[i];
        if (!file_exists(r->path)) continue;
        lib->recents[keep++] = *r;
        if (!has_game(lib, r->path) && lib->game_count < LIB_GAMES_MAX &&
            read_game(r->path, &lib->games[lib->game_count]))
            lib->game_count++;
    }
    lib->recent_count = keep;

    for (uint32_t i = 0; i < lib->game_count; i++)
        for (uint32_t j = 0; j < lib->recent_count; j++)
            if (strcmp(lib->games[i].path, lib->recents[j].path) == 0) {
                lib->games[i].played = lib->recents[j].when;
                break;
            }
    qsort(lib->games, lib->game_count, sizeof(lib->games[0]), compare_games);

    if (lib->recent_count > 0)
        for (uint32_t i = 0; i < lib->game_count; i++)
            if (strcmp(lib->games[i].path, lib->recents[0].path) == 0) lib->banner = (int32_t)i;
}
