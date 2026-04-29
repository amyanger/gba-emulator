#ifndef HEADLESS_FB_HASH_H
#define HEADLESS_FB_HASH_H

#include <stddef.h>
#include <stdint.h>

/* FNV-1a 32-bit hash over a GBA framebuffer. Each pixel is folded as a pair
   of little-endian bytes (low byte first) so the result is identical on
   big- and little-endian hosts. */
uint32_t fb_hash_fnv1a(const uint16_t* fb, size_t pixels);

#endif // HEADLESS_FB_HASH_H
