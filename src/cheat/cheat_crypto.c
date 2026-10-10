#include "cheat/cheat_crypto.h"

#include <string.h>

const uint32_t CHEAT_SEEDS_GSA[4] = {0x09F4FBBDu, 0x9681884Au, 0x352027E9u, 0xF3DEE5A7u};
const uint32_t CHEAT_SEEDS_PAR3[4] = {0x7AA9648Fu, 0x7FAE6994u, 0xC0EFAAD5u, 0x42712C57u};

#define TEA_DELTA 0x9E3779B9u
#define TEA_ROUNDS 32

/* GBATEK: FOR I=1 TO 32: A += (V*16+S0) XOR (V+I*delta) XOR (V/32+S1);
 *                        V += (A*16+S2) XOR (A+I*delta) XOR (A/32+S3) */
void cheat_tea_encrypt(uint32_t* a, uint32_t* v, const uint32_t s[4]) {
    uint32_t sum = 0;
    for (int i = 0; i < TEA_ROUNDS; i++) {
        sum += TEA_DELTA;
        *a += ((*v << 4) + s[0]) ^ (*v + sum) ^ ((*v >> 5) + s[1]);
        *v += ((*a << 4) + s[2]) ^ (*a + sum) ^ ((*a >> 5) + s[3]);
    }
}

void cheat_tea_decrypt(uint32_t* a, uint32_t* v, const uint32_t s[4]) {
    uint32_t sum = TEA_DELTA * TEA_ROUNDS;
    for (int i = 0; i < TEA_ROUNDS; i++) {
        *v -= ((*a << 4) + s[2]) ^ (*a + sum) ^ ((*a >> 5) + s[3]);
        *a -= ((*v << 4) + s[0]) ^ (*v + sum) ^ ((*v >> 5) + s[1]);
        sum -= TEA_DELTA;
    }
}

/* GBATEK's random function: three LCG steps mixed into one 32-bit value. */
static uint32_t cb_random(uint32_t* state) {
    uint32_t r1 = *state * 0x41C64E6Du + 0x3039u;
    uint32_t r2 = r1 * 0x41C64E6Du + 0x3039u;
    uint32_t r3 = r2 * 0x41C64E6Du + 0x3039u;
    *state = r3;
    return ((r1 << 14) & 0xC0000000u) | ((r2 >> 1) & 0x3FFF8000u) | ((r3 >> 16) & 0x00007FFFu);
}

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

/* GBATEK: swap the 48 bits of the code by a list shuffled with "random MOD
 * 30h". (An exhaustive check over all 2^32 inputs confirmed the device's
 * divide routine gives exactly this modulo.) */
void cheat_cb_reseed(CbCrypt* cb, uint32_t op1, uint16_t op2) {
    /* The code as GBATEK addresses it: bytes 0-3 = op1 little-endian, 4-5 = op2. */
    uint8_t code[6] = {(uint8_t)op1, (uint8_t)(op1 >> 8), (uint8_t)(op1 >> 16), (uint8_t)(op1 >> 24),
                       (uint8_t)op2, (uint8_t)(op2 >> 8)};
    uint32_t r = 0x1111u ^ code[4];
    for (uint32_t i = 0; i < CHEAT_CB_BITS; i++) cb->table[i] = (uint8_t)i;
    for (int i = 0; i <= 0x4F; i++) {
        uint32_t x = cb_random(&r) % CHEAT_CB_BITS;
        uint32_t y = cb_random(&r) % CHEAT_CB_BITS;
        uint8_t t = cb->table[x];
        cb->table[x] = cb->table[y];
        cb->table[y] = t;
    }

    /* seedlist: +0 halfword, +2 word, +6 halfword, +8 word, +12 halfword. */
    memset(cb->seedlist, 0, sizeof(cb->seedlist));
    cb->seedlist[0] = code[0];
    cb->seedlist[1] = code[1];
    r = 0x4EFAD1C3u;
    for (int i = 0; i <= (int)code[3] - 0x91; i++) r = cb_random(&r);
    put32(&cb->seedlist[2], cb_random(&r));
    put16(&cb->seedlist[6], (uint16_t)cb_random(&r));
    r = 0xF254u ^ code[5];
    for (int i = 0; i <= (int)code[5] - 1; i++) r = cb_random(&r);
    put32(&cb->seedlist[8], cb_random(&r));
    put16(&cb->seedlist[12], (uint16_t)cb_random(&r));
}

/* GBATEK decryption, on the code's little-endian byte image. xlatlist turns a
 * bit number (counted from the top byte of the address) into a byte offset. */
void cheat_cb_decrypt(const CbCrypt* cb, uint32_t* op1, uint16_t* op2) {
    static const uint8_t xlat[6] = {3, 2, 1, 0, 5, 4};
    uint8_t c[6] = {(uint8_t)*op1, (uint8_t)(*op1 >> 8), (uint8_t)(*op1 >> 16), (uint8_t)(*op1 >> 24),
                    (uint8_t)*op2, (uint8_t)(*op2 >> 8)};
    for (int i = CHEAT_CB_BITS - 1; i >= 0; i--) {
        int j = cb->table[i];
        uint8_t* p1 = &c[xlat[i >> 3]];
        uint8_t* p2 = &c[xlat[j >> 3]];
        uint8_t b1 = (uint8_t)((*p1 >> (i & 7)) & 1u);
        uint8_t b2 = (uint8_t)((*p2 >> (j & 7)) & 1u);
        *p1 = (uint8_t)((*p1 & ~(1u << (i & 7))) | (uint32_t)b2 << (i & 7));
        *p2 = (uint8_t)((*p2 & ~(1u << (j & 7))) | (uint32_t)b1 << (j & 7));
    }
    put32(&c[0], get32(&c[0]) ^ get32(&cb->seedlist[8]));
    /* GBATEK stores the halfword at seedlist+12 but its listing never uses it;
     * real codes (and the reference vectors) need it XORed into the value. */
    put16(&c[4], (uint16_t)(get16(&c[4]) ^ get16(&cb->seedlist[12])));
    const uint8_t* s = cb->seedlist;
    uint32_t w = (uint32_t)c[3] * 0x01010000u + (uint32_t)c[0] * 0x100u + c[5];
    w = ((uint32_t)get16(&c[1]) * 0x10001u) ^ get32(&s[2]) ^ w;
    w = ((uint32_t)s[0] * 0x01010101u) ^ ((uint32_t)s[1] * 0x01000000u) ^ w;
    uint32_t h = c[5] + (uint32_t)(c[0] ^ c[4]) * 0x100u;
    h = ((uint32_t)s[0] * 0x101u) ^ get16(&s[6]) ^ h;
    *op1 = w;
    *op2 = (uint16_t)h;
}
