#include "cheat_file.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

// Maximum line length in a .cht file
#define LINE_BUF_SIZE 512

// A code line is exactly "XXXXXXXX YYYY" or "XXXXXXXX YYYYYYYY": an
// 8-digit hex address, one space, then a 4-8 digit hex value and nothing
// else.  Anything looser (sscanf prefix matching) would eat cheat names
// that happen to start with hex digits, e.g. "Feebas Fix".
static bool parse_code_line(const char* line, uint32_t* addr, uint32_t* val) {
    static const char hex[] = "0123456789abcdefABCDEF";
    size_t a_len = strspn(line, hex);
    if (a_len != 8 || line[8] != ' ') return false;
    size_t v_len = strspn(line + 9, hex);
    if (v_len < 4 || v_len > 8 || line[9 + v_len] != '\0') return false;

    if (sscanf(line, "%8" SCNx32 " %8" SCNx32, addr, val) != 2) return false;
    return true;
}

// Trim leading/trailing whitespace in-place, return pointer to trimmed start
static char* trim(char* str) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
    return str;
}

// Flush the current cheat entry being accumulated into the engine.
// Returns true if a cheat was added, false if nothing to flush.
static bool flush_entry(CheatEngine* engine, const char* name,
                        CheatFormat format, const CheatCode* codes,
                        uint32_t code_count, bool enabled) {
    if (code_count == 0) return false;
    if (name[0] == '\0') {
        LOG_WARN("[CHEAT] Cheat block with %u codes has no name, skipping",
                 code_count);
        return false;
    }

    int32_t idx = cheat_add(engine, name, format, codes, code_count);
    if (idx < 0) return false;

    engine->cheats[idx].enabled = enabled;
    return true;
}

int32_t cheat_file_load(CheatEngine* engine, const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return -1;

    char line_buf[LINE_BUF_SIZE];
    int32_t loaded = 0;

    // Accumulator for current cheat being parsed
    char cur_name[CHEAT_NAME_LEN] = {0};
    CheatFormat cur_format = CHEAT_FORMAT_GAMESHARK;
    CheatCode cur_codes[CHEAT_MAX_LINES];
    uint32_t cur_code_count = 0;
    bool cur_enabled = true;
    bool in_block = false;

    while (fgets(line_buf, LINE_BUF_SIZE, fp)) {
        char* line = trim(line_buf);

        // Skip empty lines and comments
        if (*line == '\0' || *line == '#') continue;

        // Format header starts a new block
        if (*line == '[') {
            // Flush previous block
            if (in_block) {
                if (flush_entry(engine, cur_name, cur_format,
                                cur_codes, cur_code_count, cur_enabled)) {
                    loaded++;
                }
            }

            // Reset accumulator
            cur_name[0] = '\0';
            cur_code_count = 0;
            cur_enabled = true;
            in_block = true;

            if (strncmp(line, "[GameShark]", 11) == 0 ||
                strncmp(line, "[ActionReplay]", 14) == 0) {
                cur_format = CHEAT_FORMAT_GAMESHARK;
            } else if (strncmp(line, "[CodeBreaker]", 13) == 0) {
                cur_format = CHEAT_FORMAT_CODEBREAKER;
            } else {
                LOG_WARN("[CHEAT] Unknown format header: %s", line);
                in_block = false;
            }
            continue;
        }

        if (!in_block) continue;

        // name=<text>
        if (strncmp(line, "name=", 5) == 0) {
            strncpy(cur_name, line + 5, CHEAT_NAME_LEN - 1);
            cur_name[CHEAT_NAME_LEN - 1] = '\0';
            continue;
        }

        // enabled=true|false
        if (strncmp(line, "enabled=", 8) == 0) {
            cur_enabled = (strcmp(line + 8, "true") == 0);
            continue;
        }

        // Code line: XXXXXXXX YYYYYYYY or XXXXXXXX YYYY (strict match)
        uint32_t addr = 0, val = 0;
        if (parse_code_line(line, &addr, &val)) {
            if (cur_code_count < CHEAT_MAX_LINES) {
                cur_codes[cur_code_count].address = addr;
                cur_codes[cur_code_count].value = val;
                cur_code_count++;
            }
            continue;
        }

        // Documented format: the first free-text line of a block is the
        // cheat's name ("[GameShark]" / name / codes).
        if (cur_name[0] == '\0') {
            strncpy(cur_name, line, CHEAT_NAME_LEN - 1);
            cur_name[CHEAT_NAME_LEN - 1] = '\0';
        } else {
            LOG_WARN("[CHEAT] Ignoring unrecognized line: %s", line);
        }
    }

    // Flush final block
    if (in_block) {
        if (flush_entry(engine, cur_name, cur_format,
                        cur_codes, cur_code_count, cur_enabled)) {
            loaded++;
        }
    }

    fclose(fp);
    return loaded;
}

bool cheat_file_save(const CheatEngine* engine, const char* path) {
    FILE* fp = fopen(path, "w");
    if (!fp) return false;

    fprintf(fp, "# GBA Cheat File\n\n");

    for (uint32_t i = 0; i < engine->cheat_count; i++) {
        const CheatEntry* entry = &engine->cheats[i];

        switch (entry->format) {
        case CHEAT_FORMAT_GAMESHARK:
            fprintf(fp, "[GameShark]\n");
            break;
        case CHEAT_FORMAT_CODEBREAKER:
            fprintf(fp, "[CodeBreaker]\n");
            break;
        }

        fprintf(fp, "name=%s\n", entry->name);
        fprintf(fp, "enabled=%s\n", entry->enabled ? "true" : "false");

        for (uint32_t j = 0; j < entry->code_count; j++) {
            fprintf(fp, "%08X %08X\n", entry->codes[j].address,
                    entry->codes[j].value);
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
    return true;
}
