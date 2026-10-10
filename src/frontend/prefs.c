#include "frontend/prefs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static bool replace_file(const char* src, const char* dst) {
#ifdef _WIN32
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(src, dst) == 0;
#endif
}

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

void prefs_init(Prefs* p, const char* path) {
    memset(p, 0, sizeof(*p));
    if (path && strlen(path) < sizeof(p->path)) memcpy(p->path, path, strlen(path) + 1);
}

bool prefs_load(Prefs* p) {
    char line[256];
    if (!p->path[0]) return false;
    FILE* f = fopen(p->path, "r");
    if (!f) return false;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        if (n > 0 && line[n - 1] != '\n' && !feof(f)) {
            /* Longer than the buffer: drop the rest so its tail is not read as a line. */
            int ch;
            while ((ch = fgetc(f)) != EOF && ch != '\n') {}
            continue;
        }
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim(line);
        char* val = trim(eq + 1);
        if (key[0] && key[0] != '#') prefs_set(p, key, val);
    }
    fclose(f);
    return true;
}

bool prefs_save(const Prefs* p) {
    char tmp[PREFS_PATH_LEN + 8];
    bool ok = true;
    if (!p->path[0]) return false;
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", p->path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return false;
    FILE* f = fopen(tmp, "w");
    if (!f) return false;
    for (uint32_t i = 0; i < p->count; i++)
        if (fprintf(f, "%s=%s\n", p->items[i].key, p->items[i].val) < 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok || !replace_file(tmp, p->path)) {
        remove(tmp);
        return false;
    }
    return true;
}

const char* prefs_get(const Prefs* p, const char* key) {
    for (uint32_t i = 0; i < p->count; i++)
        if (strcmp(p->items[i].key, key) == 0) return p->items[i].val;
    return NULL;
}

bool prefs_get_bool(const Prefs* p, const char* key, bool fallback) {
    const char* v = prefs_get(p, key);
    if (!v) return fallback;
    if (strcmp(v, "1") == 0 || strcmp(v, "true") == 0) return true;
    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0) return false;
    return fallback;
}

bool prefs_set(Prefs* p, const char* key, const char* val) {
    size_t kl = strlen(key), vl = strlen(val);
    if (kl == 0 || kl >= PREFS_KEY_LEN || vl >= PREFS_VAL_LEN || strchr(key, '=') || strchr(key, '\n') ||
        strchr(val, '\n'))
        return false;
    for (uint32_t i = 0; i < p->count; i++) {
        if (strcmp(p->items[i].key, key) == 0) {
            memcpy(p->items[i].val, val, vl + 1);
            return true;
        }
    }
    if (p->count >= PREFS_MAX) return false;
    memcpy(p->items[p->count].key, key, kl + 1);
    memcpy(p->items[p->count].val, val, vl + 1);
    p->count++;
    return true;
}

bool prefs_set_bool(Prefs* p, const char* key, bool v) { return prefs_set(p, key, v ? "1" : "0"); }
