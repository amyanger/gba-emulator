#ifndef SESSION_LOG_H
#define SESSION_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Shared plumbing of the append-only per-trainer logs next to the ROM (.run, .nuzlocke,
 * .hunt): the file name, a header-checked line reader that survives a crash mid-write,
 * and the trainer ID debounce. Each session keeps its own line format and write policy. */

#define SESSION_ATTACH_FRAMES 30   /* a trainer ID must read the same this long before we switch logs */

/* "<rom>.<TID hex>.<ext>", or fixed_path (headless) when set. False when neither path is
 * known or the name doesn't fit; out is then unusable. */
bool session_log_path(char* out, size_t n, const char* rom_path, const char* fixed_path, uint32_t tid,
                      const char* ext);

typedef enum {
    SESSION_LOG_MISSING, /* no file yet: ours to create */
    SESSION_LOG_EMPTY,   /* empty (a crash before the first write): ours to start */
    SESSION_LOG_FOREIGN, /* first line isn't exactly the header: leave the file alone */
    SESSION_LOG_OK       /* header read; call session_log_next until it returns false */
} SessionLogOpen;

typedef struct {
    FILE* f;
    bool ends_nl;  /* the last line read ended with a newline */
    uint32_t bad;  /* lines skipped: too long, or cut off by a crash. Callers add parse failures. */
} SessionLogReader;

/* r->f is open only for SESSION_LOG_OK. */
SessionLogOpen session_log_open(SessionLogReader* r, const char* path, const char* header);
/* Next whole line into buf (with its newline). Overlong lines and a last line with no
 * newline are skipped and counted in r->bad. False at end of file. */
bool session_log_next(SessionLogReader* r, char* buf, size_t n);
/* Closes the file and ends a torn last line, so the next append starts on a line of its
 * own. False when that repair couldn't be written. */
bool session_log_close(SessionLogReader* r, const char* path);

typedef struct {
    uint32_t tid;
    uint8_t count;
} SessionTidGate;

/* One frame of the trainer ID debounce. True when the caller should attach to `tid`: it
 * differs from the attached one and has read the same for SESSION_ATTACH_FRAMES. A 0 ID
 * (a game that hasn't started, or a misread) never attaches. */
bool session_tid_step(SessionTidGate* g, bool attached, uint32_t attached_tid, uint32_t tid);

#endif // SESSION_LOG_H
