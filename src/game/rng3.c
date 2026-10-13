#include "game/rng3.h"

uint32_t rng3_next(uint32_t s) {
    return s * RNG3_MULT + RNG3_ADD;
}

uint16_t rng3_hi(uint32_t s) {
    return (uint16_t)(s >> 16);
}

uint32_t rng3_jump(uint32_t s, uint32_t n) {
    /* Square-and-multiply on the affine map x -> m*x + a. */
    uint32_t m = RNG3_MULT, a = RNG3_ADD;
    while (n) {
        if (n & 1u)
            s = s * m + a;
        a = a * (m + 1u);
        m = m * m;
        n >>= 1;
    }
    return s;
}

/* Discrete log mod 2^32: stepping 2^i flips state bit i and leaves lower bits
 * alone, so fix the index one bit at a time from seed 0 (factcheck F2). */
uint32_t rng3_index(uint32_t s) {
    uint32_t cur = 0, idx = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if ((cur ^ s) & (1u << i)) {
            cur = rng3_jump(cur, 1u << i);
            idx |= 1u << i;
        }
    }
    return idx;
}

uint32_t rng3_distance(uint32_t a, uint32_t b) {
    return rng3_index(b) - rng3_index(a);
}
