#include "game/gen3_text.h"

static const char* gen3_glyph(uint8_t c, char* one) {
    one[1] = '\0';
    if (c >= 0xA1 && c <= 0xAA) { one[0] = (char)('0' + (c - 0xA1)); return one; }
    if (c >= 0xBB && c <= 0xD4) { one[0] = (char)('A' + (c - 0xBB)); return one; }
    if (c >= 0xD5 && c <= 0xEE) { one[0] = (char)('a' + (c - 0xD5)); return one; }
    switch (c) {
    case 0x00: case 0xFE: return " ";
    case 0x1B: return "e";
    case 0x53: return "Pk";
    case 0x54: return "Mn";
    case 0x5C: return "(";
    case 0x5D: return ")";
    case 0xAB: return "!";
    case 0xAC: return "?";
    case 0xAD: return ".";
    case 0xAE: return "-";
    case 0xAF: return ".";
    case 0xB0: return "...";
    case 0xB1: case 0xB2: return "\"";
    case 0xB3: case 0xB4: return "'";
    case 0xB5: return "M";
    case 0xB6: return "F";
    case 0xB8: return ",";
    case 0xBA: return "/";
    case 0xF0: return ":";
    default: return "?";
    }
}

size_t gen3_decode(const uint8_t* src, size_t src_len, char* out, size_t out_size) {
    size_t n = 0;
    char one[2];
    if (out_size == 0) return 0;
    for (size_t i = 0; i < src_len && src[i] != 0xFF; i++) {
        for (const char* s = gen3_glyph(src[i], one); *s; s++) {
            if (n + 1 >= out_size) {
                out[n] = '\0';
                return n;
            }
            out[n++] = *s;
        }
    }
    out[n] = '\0';
    return n;
}
