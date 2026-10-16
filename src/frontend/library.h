#ifndef LIBRARY_H
#define LIBRARY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The game library shown at launch: one ROM folder plus recently played
 * games, stored in library.txt. No SDL, no heap. */

#define LIB_PATH_LEN    1024
#define LIB_NAME_LEN    64
#define LIB_RECENTS     10
#define LIB_FOLDER_MAX  256   /* .gba files taken from the folder */
#define LIB_GAMES_MAX   (LIB_FOLDER_MAX + LIB_RECENTS)
#define LIB_HEADER_LEN  0xC0

typedef struct {
    char path[LIB_PATH_LEN];
    char name[LIB_NAME_LEN];   /* from the file name: "jsmolka_arm.gba" -> "Jsmolka Arm" */
    char title[13];            /* header 0xA0, printable ASCII, trimmed; "" when blank */
    char code[5];              /* header 0xAC; "" when blank */
    uint32_t size;
    int64_t played;            /* unix time, 0 = never */
    bool companion;            /* the exact ROM Pokemon Companion supports */
} LibGame;

typedef struct {
    char path[LIB_PATH_LEN];
    int64_t when;
} LibRecent;

typedef struct {
    char file[LIB_PATH_LEN];   /* library.txt; "" = never touches disk */
    char folder[LIB_PATH_LEN]; /* "" = not chosen yet */
    uint32_t recent_count;
    LibRecent recents[LIB_RECENTS];  /* newest first */

    /* Filled by library_scan */
    uint32_t game_count;
    LibGame games[LIB_GAMES_MAX];    /* sorted by name */
    int32_t banner;                  /* games[] index of "Continue playing", -1 = none */
    bool folder_missing;             /* folder set but unreadable */
    uint32_t folder_games;           /* how many came from the folder */
} Library;

void library_init(Library* lib, const char* file);
bool library_load(Library* lib);         /* false when missing or unreadable; a bad line is skipped */
bool library_save(const Library* lib);   /* temp file + rename */
bool library_set_folder(Library* lib, const char* folder);
/* --library-folder: sets the folder only when none is stored yet. */
bool library_default_folder(Library* lib, const char* folder);
/* Moves path to the front of the recents (absolute path expected). */
void library_add_recent(Library* lib, const char* path, int64_t now);
/* Rebuilds games[] from the folder and the recents; drops recents whose file is gone. */
void library_scan(Library* lib);

/* Helpers, exposed for tests. */
void library_parse_header(const uint8_t* hdr, uint32_t len, char title[13], char code[5]);
void library_display_name(const char* path, char* out, size_t out_size);
bool library_is_gba(const char* name);
/* Absolute form of path in out; false when it doesn't fit or can't be resolved. */
bool library_abspath(const char* path, char* out, size_t out_size);

#endif // LIBRARY_H
