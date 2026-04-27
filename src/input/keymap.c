#include "keymap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_keyboard.h>

#include "common.h"
#include "input/input.h"

static uint16_t s_table[SDL_NUM_SCANCODES];

static void set_default(SDL_Scancode sc, uint16_t mask) {
    if (sc > 0 && sc < SDL_NUM_SCANCODES) s_table[sc] = mask;
}

void keymap_reset_defaults(void) {
    memset(s_table, 0, sizeof(s_table));
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

    keymap_reset_defaults();

    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof(line), fp)) {
        lineno++;
        char* button = NULL;
        char* key = NULL;
        if (!keymap_parse_line(line, &button, &key)) continue;

        uint16_t mask = keymap_button_from_name(button);
        if (mask == 0) {
            LOG_WARN("keymap: unknown button '%s' on line %d", button, lineno);
            continue;
        }
        SDL_Scancode sc = SDL_GetScancodeFromName(key);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            LOG_WARN("keymap: unknown key '%s' on line %d", key, lineno);
            continue;
        }

        /* First clear any prior mapping for this button so re-binding is total. */
        for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
            if (s_table[i] == mask) s_table[i] = 0;
        }
        s_table[sc] = mask;
    }

    fclose(fp);
    LOG_INFO("keymap: loaded from %s", path);
    return true;
}
