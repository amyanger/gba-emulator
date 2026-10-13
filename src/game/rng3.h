#ifndef RNG3_H
#define RNG3_H

#include <stdint.h>

/* The game's Random() LCG (pret include/random.h ISO_RANDOMIZE1). */
#define RNG3_MULT 0x41C64E6Du
#define RNG3_ADD 0x00006073u

uint32_t rng3_next(uint32_t s);             /* one Random() step */
uint16_t rng3_hi(uint32_t s);               /* s >> 16, the value Random() returns */
uint32_t rng3_jump(uint32_t s, uint32_t n); /* n steps in O(log n) */
/* Steps from a to b (mod 2^32), via each state's index from seed 0. O(32). */
uint32_t rng3_distance(uint32_t a, uint32_t b);
uint32_t rng3_index(uint32_t s); /* steps from seed 0 to s (mod 2^32) */

#endif // RNG3_H
