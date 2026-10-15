#include "game/logline.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int logline_tokens(const char* line, char* buf, size_t buf_size, char** tok, int max) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    if (len == 0 || len >= buf_size) return 0;
    memcpy(buf, line, len);
    buf[len] = '\0';
    /* Split on spaces by hand: strtok_r isn't available on MSVC. */
    int n = 0;
    for (char* p = buf; *p;) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        if (n == max) return 0;
        tok[n++] = p;
        while (*p && *p != ' ') p++;
    }
    return n;
}

bool logline_field(char* tok, char** key, char** value) {
    char* eq = strchr(tok, '=');
    if (!eq || eq == tok) return false;
    *eq = '\0';
    *key = tok;
    *value = eq + 1;
    return true;
}

bool logline_u32(const char* s, int base, uint32_t max, uint32_t* out) {
    char* end;
    /* 10 digits covers any u32; also guards against strtoul wrapping on 32-bit long. */
    if (*s == '\0' || *s == '-' || *s == '+' || *s == ' ' || strlen(s) > 10) return false;
    errno = 0;
    unsigned long v = strtoul(s, &end, base);
    if (*end != '\0' || errno == ERANGE || v > max) return false;
    *out = (uint32_t)v;
    return true;
}

bool logline_u16(const char* s, uint16_t* out) {
    uint32_t v;
    if (!logline_u32(s, 10, 0xFFFF, &v)) return false;
    *out = (uint16_t)v;
    return true;
}

bool logline_u8(const char* s, uint8_t* out) {
    uint32_t v;
    if (!logline_u32(s, 10, 255, &v)) return false;
    *out = (uint8_t)v;
    return true;
}

bool logline_bool(const char* s, bool* out) {
    uint32_t v;
    if (!logline_u32(s, 10, 1, &v)) return false;
    *out = v != 0;
    return true;
}

bool logline_name(const char* v, const char* const* names, int count, uint8_t* out) {
    for (int i = 0; i < count; i++)
        if (strcmp(v, names[i]) == 0) {
            *out = (uint8_t)i;
            return true;
        }
    return false;
}

void logline_hex_encode(char* out, const uint8_t* bytes, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = hex[bytes[i] >> 4];
        out[2 * i + 1] = hex[bytes[i] & 15];
    }
    out[2 * n] = '\0';
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool logline_hex_decode(const char* s, uint8_t* bytes, size_t n) {
    if (strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hex_digit(s[2 * i]), lo = hex_digit(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        bytes[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}
