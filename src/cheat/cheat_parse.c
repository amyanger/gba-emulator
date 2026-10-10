#include "cheat/cheat_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LINE_MAX_LEN 256
#define TOKENS_MAX   32

typedef struct {
    const char* s;
    uint32_t len;
    bool hex;          /* every character is a hex digit */
    bool has_digit;    /* contains 0-9, so it cannot be an English word */
} Token;

static uint32_t hex_value(const char* s, uint32_t len) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = s[i];
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0')
                                            : (uint32_t)(tolower((unsigned char)c) - 'a' + 10);
        v = (v << 4) | d;
    }
    return v;
}

/* Tokens are runs of letters and digits; everything else separates them. */
static uint32_t tokenize(const char* line, Token* t) {
    uint32_t n = 0;
    const char* p = line;
    while (*p && n < TOKENS_MAX) {
        while (*p && !isalnum((unsigned char)*p)) p++;
        if (!*p) break;
        const char* start = p;
        bool hex = true, digit = false;
        while (*p && isalnum((unsigned char)*p)) {
            if (!isxdigit((unsigned char)*p)) hex = false;
            if (isdigit((unsigned char)*p)) digit = true;
            p++;
        }
        t[n++] = (Token){start, (uint32_t)(p - start), hex, digit};
    }
    return n;
}

static void set_err(char* err, size_t len, uint32_t line_no, const char* line, const char* why) {
    if (!err || len == 0) return;
    snprintf(err, len, "Line %u: \"%.40s\" %s", (unsigned)line_no, line, why);
}

static void trim_copy(char* dst, size_t dst_len, const char* src) {
    while (*src && isspace((unsigned char)*src)) src++;
    size_t n = strlen(src);
    while (n > 0 && (isspace((unsigned char)src[n - 1]) || src[n - 1] == ':')) n--;
    if (n >= dst_len) n = dst_len - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Parses one line. Returns codes found (0 for a label line), or -1 with err set. */
static int32_t parse_line(const char* line, uint32_t line_no, CheatRawList* out, char* err,
                          size_t err_len) {
    Token t[TOKENS_MAX];
    uint32_t n = tokenize(line, t);
    bool used[TOKENS_MAX] = {false};
    int32_t found = 0;

    for (uint32_t i = 0; i < n; i++) {
        if (!t[i].hex) continue;
        uint32_t a, b;
        uint8_t digits;
        if (t[i].len == 16 || t[i].len == 12) {
            a = hex_value(t[i].s, 8);
            digits = (uint8_t)(t[i].len - 8);
            b = hex_value(t[i].s + 8, digits);
            used[i] = true;
        } else if (t[i].len == 8 && i + 1 < n && t[i + 1].hex &&
                   (t[i + 1].len == 4 || t[i + 1].len == 8)) {
            a = hex_value(t[i].s, 8);
            digits = (uint8_t)t[i + 1].len;
            b = hex_value(t[i + 1].s, digits);
            used[i] = used[i + 1] = true;
            i++;
        } else {
            continue;
        }
        if (out->count >= CHEAT_MAX_LINES) {
            snprintf(err, err_len, "Too many code lines (at most %d per cheat)", CHEAT_MAX_LINES);
            return -1;
        }
        out->lines[out->count++] = (CheatRawLine){a, b, digits};
        found++;
    }

    /* A leftover number that looks like half a code (8 digits, or next to
     * another hex group as in "82025838 3E7") is a typo: refuse the paste
     * rather than guess. A number among words ("Max Money 999999") is a label. */
    for (uint32_t i = 0; i < n; i++) {
        if (used[i] || !t[i].hex || !t[i].has_digit || t[i].len < 6) continue;
        bool hex_neighbor = (i > 0 && t[i - 1].hex) || (i + 1 < n && t[i + 1].hex);
        if (t[i].len == 8 || hex_neighbor) {
            set_err(err, err_len, line_no, line,
                    "is not a code: expected 8 digits, then 4 or 8 digits");
            return -1;
        }
    }
    return found;
}

bool cheat_parse_text(const char* text, CheatRawList* out, char* err, size_t err_len) {
    memset(out, 0, sizeof(*out));
    if (err && err_len) err[0] = '\0';
    if (!text) text = "";

    char line[LINE_MAX_LEN];
    uint32_t line_no = 0;
    const char* p = text;
    while (*p) {
        size_t len = strcspn(p, "\r\n");
        size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, p, copy);
        line[copy] = '\0';
        p += len;
        if (*p == '\r' && p[1] == '\n') p += 2;
        else if (*p) p++;
        line_no++;

        int32_t found = parse_line(line, line_no, out, err, err_len);
        if (found < 0) return false;
        if (found == 0 && out->name[0] == '\0') {
            char label[CHEAT_NAME_LEN];
            trim_copy(label, sizeof(label), line);
            if (label[0] && label[0] != '#') snprintf(out->name, sizeof(out->name), "%s", label);
        }
    }

    if (out->count == 0) {
        snprintf(err, err_len, "No codes found");
        return false;
    }
    for (uint32_t i = 1; i < out->count; i++) {
        if (out->lines[i].digits != out->lines[0].digits) {
            snprintf(err, err_len,
                     "This mixes CodeBreaker codes (8+4 digits) with GameShark or Action Replay "
                     "codes (8+8 digits). Paste one kind at a time.");
            return false;
        }
    }
    return true;
}
