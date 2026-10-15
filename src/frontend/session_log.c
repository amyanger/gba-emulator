#include "frontend/session_log.h"
#include <string.h>

bool session_log_path(char* out, size_t n, const char* rom_path, const char* fixed_path, uint32_t tid,
                      const char* ext) {
    bool fixed = fixed_path && fixed_path[0];
    if (!fixed && !(rom_path && rom_path[0])) return false;
    int len = fixed ? snprintf(out, n, "%s", fixed_path)
                    : snprintf(out, n, "%s.%08X.%s", rom_path, (unsigned)tid, ext);
    return len > 0 && (size_t)len < n;
}

SessionLogOpen session_log_open(SessionLogReader* r, const char* path, const char* header) {
    char line[64];
    memset(r, 0, sizeof(*r));
    FILE* f = fopen(path, "r");
    if (!f) return SESSION_LOG_MISSING;
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return SESSION_LOG_EMPTY;
    }
    size_t hl = strlen(header);
    if (strncmp(line, header, hl) != 0 || (line[hl] != '\0' && line[hl] != '\n' && line[hl] != '\r')) {
        fclose(f);
        return SESSION_LOG_FOREIGN;
    }
    r->f = f;
    r->ends_nl = line[strlen(line) - 1] == '\n';
    return SESSION_LOG_OK;
}

bool session_log_next(SessionLogReader* r, char* buf, size_t n) {
    while (fgets(buf, (int)n, r->f)) {
        size_t len = strlen(buf);
        if (len == 0) {   /* starts with a NUL byte: corrupt, and there's no last char to look at */
            r->bad++;
            continue;
        }
        if (len + 1 == n && buf[len - 1] != '\n') {   /* overlong: skip the rest of it */
            int c;
            while ((c = fgetc(r->f)) != EOF && c != '\n') {}
            r->ends_nl = c == '\n';
            r->bad++;
            continue;
        }
        r->ends_nl = buf[len - 1] == '\n';
        /* A last line with no newline was cut off mid-write (crash): don't trust it. */
        if (r->ends_nl) return true;
        r->bad++;
    }
    return false;
}

bool session_log_close(SessionLogReader* r, const char* path) {
    if (!r->f) return true;
    fclose(r->f);
    r->f = NULL;
    if (r->ends_nl) return true;
    FILE* f = fopen(path, "a");
    bool ok = f != NULL && fputc('\n', f) != EOF;
    if (f && fclose(f) != 0) ok = false;
    return ok;
}

bool session_tid_step(SessionTidGate* g, bool attached, uint32_t attached_tid, uint32_t tid) {
    if (attached && tid == attached_tid) {
        g->count = 0;
        return false;
    }
    if (tid == 0) return false;
    if (tid != g->tid) {
        g->tid = tid;
        g->count = 0;
    }
    if (++g->count < SESSION_ATTACH_FRAMES) return false;
    g->count = 0;
    return true;
}
