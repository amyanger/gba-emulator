#include "cheat/cheat_file.h"
#include "cheat/cheat_parse.h"
#include "fileio.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LINE_BUF_SIZE 512
#define BLOCK_BUF_SIZE 8192

static const char* const k_headers[CHEAT_DEV_COUNT] = {
    "[CodeBreaker]", "[GameShark]", "[ActionReplay]", "[GameSharkRaw]", "[ActionReplayRaw]",
};

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

typedef struct {
    bool open;
    CheatDevice header;      /* device named by the [section] */
    bool has_device_key;     /* device= present: trust the header, no detection */
    bool override;
    bool enabled;
    char name[CHEAT_NAME_LEN];
    char text[BLOCK_BUF_SIZE];   /* the block's code and label lines */
    size_t text_len;
    char raw[BLOCK_BUF_SIZE];    /* every line of the block, header included */
    size_t raw_len;
    bool raw_overflow;
} Block;

static void append_line(char* buf, size_t cap, size_t* len, bool* overflow, const char* line) {
    size_t n = strlen(line);
    if (*len + n + 2 > cap) {
        if (overflow) *overflow = true;
        return;
    }
    memcpy(buf + *len, line, n);
    *len += n;
    buf[(*len)++] = '\n';
    buf[*len] = '\0';
}

/* Keeps an unparseable block for cheat_file_save to write back as it was. */
static void keep_block(CheatEngine* engine, const Block* b) {
    if (b->raw_overflow || engine->kept_len + b->raw_len + 1 > sizeof(engine->kept_text)) {
        engine->kept_overflow = true;
        return;
    }
    memcpy(engine->kept_text + engine->kept_len, b->raw, b->raw_len);
    engine->kept_len += (uint32_t)b->raw_len;
    engine->kept_text[engine->kept_len] = '\0';
}

static void block_start(Block* b, CheatDevice header) {
    memset(b, 0, sizeof(*b));
    b->open = true;
    b->header = header;
    b->enabled = true;
}

static void block_flush(CheatEngine* engine, Block* b, int32_t* loaded, uint32_t* skipped) {
    static CheatRawList raw;
    if (!b->open) return;
    b->open = false;
    char err[128];
    if (!cheat_parse_text(b->text, &raw, err, sizeof(err))) {
        if (b->text_len > 0) {
            LOG_WARN("[CHEAT] Skipping \"%s\": %s", b->name[0] ? b->name : "(unnamed)", err);
            (*skipped)++;
            keep_block(engine, b);
        }
        return;
    }
    const char* name = b->name[0] ? b->name : raw.name[0] ? raw.name : "Cheat";
    CheatDevice dev = b->has_device_key ? b->header : CHEAT_DEV_AUTO;
    if (cheat_add_raw(engine, name, &raw, dev, b->override, b->enabled) >= 0) (*loaded)++;
}

int32_t cheat_file_load(CheatEngine* engine, const char* path, uint32_t* skipped) {
    static Block b;
    uint32_t skipped_local = 0;
    if (!skipped) skipped = &skipped_local;
    *skipped = 0;
    FILE* f = fopen(path, "r");
    if (!f) return -1;

    int32_t loaded = 0;
    char buf[LINE_BUF_SIZE];
    b.open = false;
    while (fgets(buf, sizeof(buf), f)) {
        char* line = trim(buf);
        if (*line == '\0' || *line == '#') continue;
        if (*line == '[') {
            block_flush(engine, &b, &loaded, skipped);
            CheatDevice dev = CHEAT_DEV_AUTO;
            for (int d = 0; d < CHEAT_DEV_COUNT; d++)
                if (strcmp(line, k_headers[d]) == 0) dev = (CheatDevice)d;
            if (dev == CHEAT_DEV_AUTO) {
                LOG_WARN("[CHEAT] Unknown section %s, codes will be auto-detected", line);
                dev = CHEAT_DEV_GSA;
            }
            block_start(&b, dev);
            append_line(b.raw, sizeof(b.raw), &b.raw_len, &b.raw_overflow, line);
            continue;
        }
        if (!b.open) block_start(&b, CHEAT_DEV_GSA);   /* codes before any header */
        append_line(b.raw, sizeof(b.raw), &b.raw_len, &b.raw_overflow, line);
        if (strncmp(line, "enabled=", 8) == 0) {
            b.enabled = strcmp(line + 8, "true") == 0;
        } else if (strncmp(line, "device=", 7) == 0) {
            b.has_device_key = true;
            b.override = strcmp(line + 7, "override") == 0;
        } else if (strncmp(line, "name=", 5) == 0) {
            snprintf(b.name, sizeof(b.name), "%s", line + 5);
        } else {
            append_line(b.text, sizeof(b.text), &b.text_len, NULL, line);
        }
    }
    block_flush(engine, &b, &loaded, skipped);
    fclose(f);
    return loaded;
}

bool cheat_file_save(const CheatEngine* engine, const char* path) {
    if (engine->kept_overflow) return false;   /* would drop hand-written blocks */
    char tmp[1024];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp)) return false;
    FILE* f = fopen(tmp, "w");
    if (!f) return false;

    bool ok = fprintf(f, "# Cheats for this game. Edit in the emulator, or by hand: a [device] line,\n"
                         "# the cheat's name, then its codes.\n") > 0;
    /* Kept text first: text that had no [header] can only stay apart from the
     * cheats if nothing comes before it. */
    if (ok && engine->kept_len > 0) ok = fprintf(f, "%s", engine->kept_text) > 0;
    for (uint32_t i = 0; i < engine->cheat_count && ok; i++) {
        const CheatEntry* e = &engine->cheats[i];
        CheatDevice dev = e->device < CHEAT_DEV_COUNT ? e->device : CHEAT_DEV_GSA;
        ok = fprintf(f, "\n%s\nname=%s\nenabled=%s\n", k_headers[dev], e->name, e->enabled ? "true" : "false") > 0;
        /* A broken cheat's device is only a guess, so let the next load detect again. */
        if (ok && !e->broken) ok = fprintf(f, "device=%s\n", e->device_override ? "override" : "detected") > 0;
        for (uint32_t k = 0; k < e->line_count && ok; k++) {
            const CheatRawLine* l = &e->lines[k];
            ok = (l->digits == 4 ? fprintf(f, "%08X %04X\n", (unsigned)l->a, (unsigned)l->b)
                                 : fprintf(f, "%08X %08X\n", (unsigned)l->a, (unsigned)l->b)) > 0;
        }
    }
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok || !fileio_replace(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}
