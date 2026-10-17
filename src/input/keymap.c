#include "keymap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_keyboard.h>

#include "common.h"
#include "fileio.h"
#include "input/input.h"

static uint16_t s_table[SDL_NUM_SCANCODES];
static uint16_t s_pad[KEYMAP_PAD_COUNT];

static void set_default(SDL_Scancode sc, uint16_t mask) {
    if (sc > 0 && sc < SDL_NUM_SCANCODES) s_table[sc] = mask;
}

void keymap_reset_defaults(void) {
    memset(s_table, 0, sizeof(s_table));
    memset(s_pad, 0, sizeof(s_pad));
    s_pad[SDL_CONTROLLER_BUTTON_A]             = KEY_A;
    s_pad[SDL_CONTROLLER_BUTTON_B]             = KEY_B;
    s_pad[SDL_CONTROLLER_BUTTON_START]         = KEY_START;
    s_pad[SDL_CONTROLLER_BUTTON_BACK]          = KEY_SELECT;
    s_pad[SDL_CONTROLLER_BUTTON_DPAD_UP]       = KEY_UP;
    s_pad[SDL_CONTROLLER_BUTTON_DPAD_DOWN]     = KEY_DOWN;
    s_pad[SDL_CONTROLLER_BUTTON_DPAD_LEFT]     = KEY_LEFT;
    s_pad[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]    = KEY_RIGHT;
    s_pad[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]  = KEY_L;
    s_pad[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = KEY_R;
    set_default(SDL_SCANCODE_Z,      KEY_A);
    set_default(SDL_SCANCODE_X,      KEY_B);
    set_default(SDL_SCANCODE_RETURN, KEY_START);
    set_default(SDL_SCANCODE_RSHIFT, KEY_SELECT);
    set_default(SDL_SCANCODE_UP,     KEY_UP);
    set_default(SDL_SCANCODE_DOWN,   KEY_DOWN);
    set_default(SDL_SCANCODE_LEFT,   KEY_LEFT);
    set_default(SDL_SCANCODE_RIGHT,  KEY_RIGHT);
    set_default(SDL_SCANCODE_A,      KEY_L);
    set_default(SDL_SCANCODE_S,      KEY_R);
}

uint16_t keymap_lookup(SDL_Scancode sc) {
    if (sc <= 0 || sc >= SDL_NUM_SCANCODES) return 0;
    return s_table[sc];
}

static int ieq(const char* a, const char* b);

uint16_t keymap_pad_lookup(int32_t pad) {
    if (pad < 0 || pad >= KEYMAP_PAD_COUNT) return 0;
    return s_pad[pad];
}

void keymap_bind_key(uint16_t action, SDL_Scancode sc) {
    if (action == 0 || sc <= 0 || sc >= SDL_NUM_SCANCODES) return;
    for (int i = 0; i < SDL_NUM_SCANCODES; i++)
        if (s_table[i] == action) s_table[i] = 0;
    s_table[sc] = action;
}

void keymap_bind_pad(uint16_t action, int32_t pad) {
    if (action == 0 || pad < 0 || pad >= KEYMAP_PAD_COUNT) return;
    for (int32_t i = 0; i < KEYMAP_PAD_COUNT; i++)
        if (s_pad[i] == action) s_pad[i] = 0;
    s_pad[pad] = action;
}

void keymap_unbind(uint16_t action, bool pad) {
    if (action == 0) return;
    if (pad) {
        for (int32_t i = 0; i < KEYMAP_PAD_COUNT; i++)
            if (s_pad[i] == action) s_pad[i] = 0;
    } else {
        for (int i = 0; i < SDL_NUM_SCANCODES; i++)
            if (s_table[i] == action) s_table[i] = 0;
    }
}

SDL_Scancode keymap_key_for(uint16_t action) {
    if (action == 0) return SDL_SCANCODE_UNKNOWN;
    for (int i = 1; i < SDL_NUM_SCANCODES; i++)
        if (s_table[i] == action) return (SDL_Scancode)i;
    return SDL_SCANCODE_UNKNOWN;
}

int32_t keymap_pad_for(uint16_t action) {
    if (action == 0) return KEYMAP_PAD_NONE;
    for (int32_t i = 0; i < KEYMAP_PAD_COUNT; i++)
        if (s_pad[i] == action) return i;
    return KEYMAP_PAD_NONE;
}

const char* keymap_pad_name(int32_t pad) {
    if (pad == KEYMAP_PAD_LT) return "lefttrigger";
    if (pad == KEYMAP_PAD_RT) return "righttrigger";
    if (pad < 0 || pad >= SDL_CONTROLLER_BUTTON_MAX) return NULL;
    return SDL_GameControllerGetStringForButton((SDL_GameControllerButton)pad);
}

int32_t keymap_pad_from_name(const char* name) {
    if (!name || !*name) return KEYMAP_PAD_NONE;
    if (ieq(name, "lefttrigger")) return KEYMAP_PAD_LT;
    if (ieq(name, "righttrigger")) return KEYMAP_PAD_RT;
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(name);
    return b == SDL_CONTROLLER_BUTTON_INVALID ? KEYMAP_PAD_NONE : (int32_t)b;
}

static int ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

uint16_t keymap_button_from_name(const char* name) {
    if (!name) return 0;
    if (ieq(name, "A"))      return KEY_A;
    if (ieq(name, "B"))      return KEY_B;
    if (ieq(name, "START"))  return KEY_START;
    if (ieq(name, "SELECT")) return KEY_SELECT;
    if (ieq(name, "UP"))     return KEY_UP;
    if (ieq(name, "DOWN"))   return KEY_DOWN;
    if (ieq(name, "LEFT"))   return KEY_LEFT;
    if (ieq(name, "RIGHT"))  return KEY_RIGHT;
    if (ieq(name, "L"))      return KEY_L;
    if (ieq(name, "R"))      return KEY_R;
    if (ieq(name, "TURBO_A")) return KEYMAP_TURBO_A;
    if (ieq(name, "TURBO_B")) return KEYMAP_TURBO_B;
    return 0;
}

static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = 0;
    return s;
}

bool keymap_parse_line(char* line, char** button_out, char** key_out) {
    if (!line) return false;
    /* Strip trailing comment (anything from # onward). */
    char* hash = strchr(line, '#');
    if (hash) *hash = 0;

    char* eq = strchr(line, '=');
    if (!eq) return false;
    *eq = 0;

    char* lhs = trim(line);
    char* rhs = trim(eq + 1);
    if (*lhs == 0 || *rhs == 0) return false;

    *button_out = lhs;
    *key_out = rhs;
    return true;
}

bool keymap_load(const char* path) {
    if (!path) return false;
    FILE* fp = fopen(path, "r");
    if (!fp) {
        LOG_ERROR("keymap: cannot open %s", path);
        return false;
    }

    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        char* button = NULL;
        char* key = NULL;
        if (!keymap_parse_line(line, &button, &key)) continue;

        bool is_pad = (button[0] == 'P' || button[0] == 'p') && (button[1] == 'A' || button[1] == 'a') &&
                      (button[2] == 'D' || button[2] == 'd') && button[3] == '_';
        uint16_t mask = keymap_button_from_name(is_pad ? button + 4 : button);
        if (mask == 0) {
            LOG_WARN("keymap: unknown button '%s' on line %d", button, lineno);
            continue;
        }
        if (ieq(key, "none")) {
            keymap_unbind(mask, is_pad);
            continue;
        }
        if (is_pad) {
            int32_t pad = keymap_pad_from_name(key);
            if (pad == KEYMAP_PAD_NONE) {
                LOG_WARN("keymap: unknown pad input '%s' on line %d", key, lineno);
                continue;
            }
            keymap_bind_pad(mask, pad);
        } else {
            SDL_Scancode sc = SDL_GetScancodeFromName(key);
            if (sc == SDL_SCANCODE_UNKNOWN) {
                LOG_WARN("keymap: unknown key '%s' on line %d", key, lineno);
                continue;
            }
            keymap_bind_key(mask, sc);
        }
    }

    fclose(fp);
    LOG_INFO("keymap: loaded from %s", path);
    return true;
}

static const char* action_name(uint16_t action) {
    switch (action) {
    case KEY_A:          return "A";
    case KEY_B:          return "B";
    case KEY_START:      return "START";
    case KEY_SELECT:     return "SELECT";
    case KEY_UP:         return "UP";
    case KEY_DOWN:       return "DOWN";
    case KEY_LEFT:       return "LEFT";
    case KEY_RIGHT:      return "RIGHT";
    case KEY_L:          return "L";
    case KEY_R:          return "R";
    case KEYMAP_TURBO_A: return "TURBO_A";
    case KEYMAP_TURBO_B: return "TURBO_B";
    default:             return NULL;
    }
}

bool keymap_key_bindable(SDL_Scancode sc) {
    if (sc <= 0 || sc >= SDL_NUM_SCANCODES) return false;
    const char* n = SDL_GetScancodeName(sc);
    if (!n || !*n || strchr(n, '#')) return false;
    return SDL_GetScancodeFromName(n) == sc;
}

bool keymap_save(const char* path) {
    static const uint16_t order[] = {KEY_A, KEY_B, KEY_START, KEY_SELECT, KEY_UP, KEY_DOWN,
                                     KEY_LEFT, KEY_RIGHT, KEY_L, KEY_R, KEYMAP_TURBO_A,
                                     KEYMAP_TURBO_B};
    if (!path) return false;
    char tmp[1024];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE* f = fopen(tmp, "w");
    if (!f) return false;
    bool ok = fprintf(f, "# Keyboard (NAME=key) and controller (PAD_NAME=input) bindings\n") >= 0;
    for (size_t a = 0; a < sizeof(order) / sizeof(order[0]); a++) {
        const char* name = action_name(order[a]);
        int nkeys = 0, npads = 0;
        for (int i = 1; i < SDL_NUM_SCANCODES; i++) {
            if (s_table[i] != order[a] || !keymap_key_bindable((SDL_Scancode)i)) continue;
            if (fprintf(f, "%s=%s\n", name, SDL_GetScancodeName((SDL_Scancode)i)) < 0) ok = false;
            nkeys++;
        }
        if (nkeys == 0 && fprintf(f, "%s=None\n", name) < 0) ok = false;
        for (int32_t i = 0; i < KEYMAP_PAD_COUNT; i++) {
            if (s_pad[i] != order[a]) continue;
            const char* pn = keymap_pad_name(i);
            if (!pn || fprintf(f, "PAD_%s=%s\n", name, pn) < 0) ok = false;
            npads++;
        }
        if (npads == 0 && fprintf(f, "PAD_%s=None\n", name) < 0) ok = false;
    }
    if (fclose(f) != 0) ok = false;
    if (!ok || !fileio_replace(tmp, path)) {
        remove(tmp);
        return false;
    }
    return true;
}
