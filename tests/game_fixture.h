#ifndef GAME_FIXTURE_H
#define GAME_FIXTURE_H

#include <stdint.h>
#include <string.h>

static inline void fixture_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void fixture_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* order[slot] = substruct type at that slot, for personality % 24 = n:
 * the n-th permutation of {0,1,2,3} in lexicographic order. */
static inline void fixture_order(uint32_t n, uint8_t order[4]) {
    uint8_t pool[4] = {0, 1, 2, 3};
    uint32_t fact[4] = {6, 2, 1, 1};
    uint32_t left = 4;
    for (int i = 0; i < 4; i++) {
        uint32_t k = n / fact[i];
        n %= fact[i];
        order[i] = pool[k];
        for (uint32_t j = k; j + 1 < left; j++) pool[j] = pool[j + 1];
        left--;
    }
}

/* plain[type] is the decrypted 12-byte substruct of that type.
 * flags: byte 0x13 (bit1 hasSpecies, bit2 isEgg, bit0 isBadEgg). */
static inline void fixture_encode_mon(uint8_t raw[100], uint32_t personality, uint32_t ot_id,
                               uint8_t plain[4][12], uint8_t flags) {
    uint8_t order[4];
    uint8_t secure[48];
    uint16_t sum = 0;
    memset(raw, 0, 100);
    fixture_put32(raw + 0x00, personality);
    fixture_put32(raw + 0x04, ot_id);
    raw[0x13] = flags;
    fixture_order(personality % 24, order);
    for (int slot = 0; slot < 4; slot++) memcpy(secure + 12 * slot, plain[order[slot]], 12);
    for (int i = 0; i < 48; i += 2) sum = (uint16_t)(sum + (secure[i] | (secure[i + 1] << 8)));
    fixture_put16(raw + 0x1C, sum);
    uint32_t key = personality ^ ot_id;
    for (int i = 0; i < 48; i += 4) {
        uint32_t w = (uint32_t)secure[i] | ((uint32_t)secure[i + 1] << 8) |
                     ((uint32_t)secure[i + 2] << 16) | ((uint32_t)secure[i + 3] << 24);
        fixture_put32(raw + 0x20 + i, w ^ key);
    }
}

/* Plain substructs with recognizable values: species, item, exp, friendship,
 * moves 33/45/0/0, EVs 1..6, IVs all 31, abilityNum 1. */
static inline void fixture_plain_mon(uint8_t plain[4][12], uint16_t species) {
    memset(plain, 0, 48);
    fixture_put16(plain[0] + 0x0, species);
    fixture_put16(plain[0] + 0x2, 139);        /* held item */
    fixture_put32(plain[0] + 0x4, 1000);       /* experience */
    plain[0][0x9] = 70;                        /* friendship */
    fixture_put16(plain[1] + 0x0, 33);
    fixture_put16(plain[1] + 0x2, 45);
    plain[1][0x8] = 35; plain[1][0x9] = 40;
    for (int i = 0; i < 6; i++) plain[2][i] = (uint8_t)(i + 1);
    fixture_put32(plain[3] + 0x4, 0x3FFFFFFFu | (1u << 31));
}

#endif // GAME_FIXTURE_H
