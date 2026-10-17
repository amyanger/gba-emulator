#ifndef ROM_PATCH_H
#define ROM_PATCH_H

#include <stdint.h>

enum {
    ROM_PATCH_OK = 0,
    ROM_PATCH_NOT_PATCH = 1,
    ROM_PATCH_DAMAGED = 2,
    ROM_PATCH_WRONG_ROM = 3,
    ROM_PATCH_TOO_BIG = 4,
    ROM_PATCH_BAD_RESULT = 5,
    ROM_PATCH_CANT_READ = 6 /* C side only: the patch file couldn't be opened or read */
};

/* Implemented in rust/hgss (patch.rs). Applies an IPS, UPS or BPS patch to src. */
uint32_t rom_patch_apply(const uint8_t* src, uint32_t src_len, const uint8_t* patch,
                         uint32_t patch_len, uint8_t* out, uint32_t out_cap, uint32_t* out_len);

const char* rom_patch_reason(uint32_t code);

#endif // ROM_PATCH_H
