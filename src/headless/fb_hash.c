#include "headless/fb_hash.h"

uint32_t fb_hash_fnv1a(const uint16_t* fb, size_t pixels) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < pixels; i++) {
        uint16_t p = fb[i];
        h ^= (uint32_t)(p & 0xFF);
        h *= 0x01000193u;
        h ^= (uint32_t)((p >> 8) & 0xFF);
        h *= 0x01000193u;
    }
    return h;
}
