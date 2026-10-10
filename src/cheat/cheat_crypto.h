#ifndef CHEAT_CRYPTO_H
#define CHEAT_CRYPTO_H

#include <stdint.h>

/* Cheat device ciphers, from GBATEK "GBA Cheat Devices".
 * GameShark v1/v2 and Pro Action Replay v3 share a TEA variant with
 * different seeds. CodeBreaker encrypts only after a "9" key line. */

extern const uint32_t CHEAT_SEEDS_GSA[4];
extern const uint32_t CHEAT_SEEDS_PAR3[4];

void cheat_tea_encrypt(uint32_t* a, uint32_t* v, const uint32_t seeds[4]);
void cheat_tea_decrypt(uint32_t* a, uint32_t* v, const uint32_t seeds[4]);

#define CHEAT_CB_BITS 48   /* bits in a 6-byte CodeBreaker code */

typedef struct {
    uint8_t table[CHEAT_CB_BITS];   /* GBATEK swaplist */
    uint8_t seedlist[14];           /* GBATEK seedlist, little-endian */
} CbCrypt;

/* Sets up decryption from a CodeBreaker "9xxxxxxx yyyy" key line. */
void cheat_cb_reseed(CbCrypt* cb, uint32_t op1, uint16_t op2);
void cheat_cb_decrypt(const CbCrypt* cb, uint32_t* op1, uint16_t* op2);

#endif // CHEAT_CRYPTO_H
