#include "headless/input_script.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 256
#define DELIMS " \t\r\n"

static const struct {
    const char* name;
    uint16_t mask;
} s_keys[] = {
    {"A", KEY_A},         {"B", KEY_B},       {"SELECT", KEY_SELECT},
    {"START", KEY_START}, {"RIGHT", KEY_RIGHT}, {"LEFT", KEY_LEFT},
    {"UP", KEY_UP},       {"DOWN", KEY_DOWN}, {"R", KEY_R},
    {"L", KEY_L},
};

/* Split off the next whitespace-delimited token, NUL-terminating it in place.
   (strtok_r is not available on MSVC.) */
static char* next_token(char** cursor) {
    char* p = *cursor + strspn(*cursor, DELIMS);
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }
    char* end = p + strcspn(p, DELIMS);
    if (*end != '\0') *end++ = '\0';
    *cursor = end;
    return p;
}

void input_script_init(InputScript* script) {
    memset(script, 0, sizeof(*script));
}

static bool parse_frame(const char* tok, int32_t* out) {
    if (tok[0] < '0' || tok[0] > '9') return false;  // no sign, no blanks
    char* end = NULL;
    errno = 0;
    long v = strtol(tok, &end, 10);
    if (errno != 0 || *end != '\0' || v > INT32_MAX) return false;
    *out = (int32_t)v;
    return true;
}

static bool parse_key(const char* tok, uint16_t* out) {
    for (size_t i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++) {
        if (strcmp(tok, s_keys[i].name) == 0) {
            *out = s_keys[i].mask;
            return true;
        }
    }
    return false;
}

bool input_script_parse_line(InputScript* script, const char* line, int line_no) {
    char buf[LINE_MAX_LEN];
    size_t len = strlen(line);
    if (len >= sizeof(buf)) {
        LOG_ERROR("input script line %d: line too long", line_no);
        return false;
    }
    memcpy(buf, line, len + 1);

    char* hash = strchr(buf, '#');
    if (hash) *hash = '\0';

    char* cursor = buf;
    char* frame_tok = next_token(&cursor);
    if (!frame_tok) return true;  // blank or comment-only line
    char* action_tok = next_token(&cursor);
    char* key_tok = next_token(&cursor);
    if (!action_tok || !key_tok || next_token(&cursor)) {
        LOG_ERROR("input script line %d: expected '<frame> <press|release> <KEY>'",
                  line_no);
        return false;
    }

    InputScriptEvent ev;
    if (!parse_frame(frame_tok, &ev.frame)) {
        LOG_ERROR("input script line %d: bad frame '%s'", line_no, frame_tok);
        return false;
    }
    if (strcmp(action_tok, "press") == 0) {
        ev.press = true;
    } else if (strcmp(action_tok, "release") == 0) {
        ev.press = false;
    } else {
        LOG_ERROR("input script line %d: bad action '%s' (want press|release)",
                  line_no, action_tok);
        return false;
    }
    if (!parse_key(key_tok, &ev.key)) {
        LOG_ERROR("input script line %d: unknown key '%s'", line_no, key_tok);
        return false;
    }
    if (script->count > 0 && ev.frame < script->events[script->count - 1].frame) {
        LOG_ERROR("input script line %d: frame %d is before previous frame %d",
                  line_no, ev.frame, script->events[script->count - 1].frame);
        return false;
    }
    if (script->count >= INPUT_SCRIPT_MAX_EVENTS) {
        LOG_ERROR("input script line %d: more than %d events", line_no,
                  INPUT_SCRIPT_MAX_EVENTS);
        return false;
    }
    script->events[script->count++] = ev;
    return true;
}

bool input_script_load(InputScript* script, const char* path) {
    input_script_init(script);
    FILE* f = fopen(path, "r");
    if (!f) {
        LOG_ERROR("Failed to open input script: %s", path);
        return false;
    }
    char line[LINE_MAX_LEN];
    int line_no = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof(line), f)) {
        line_no++;
        if (!strchr(line, '\n') && !feof(f)) {
            LOG_ERROR("input script line %d: line too long", line_no);
            ok = false;
            break;
        }
        ok = input_script_parse_line(script, line, line_no);
    }
    if (ok && ferror(f)) {
        LOG_ERROR("Failed to read input script: %s", path);
        ok = false;
    }
    fclose(f);
    return ok;
}

void input_script_apply(InputScript* script, int32_t frame, InputState* input) {
    while (script->next < script->count && script->events[script->next].frame <= frame) {
        const InputScriptEvent* ev = &script->events[script->next++];
        if (ev->press) {
            input_press(input, ev->key);
        } else {
            input_release(input, ev->key);
        }
    }
}
