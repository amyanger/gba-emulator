#ifndef ROM_ZIP_H
#define ROM_ZIP_H

#include <stdint.h>

enum {
    ROM_ZIP_OK = 0,
    ROM_ZIP_NOT_ZIP = 1,
    ROM_ZIP_DAMAGED = 2,
    ROM_ZIP_NO_GAME = 3,
    ROM_ZIP_UNSUPPORTED = 4,
    ROM_ZIP_MULTI = 5,
    ROM_ZIP_BAD_CRC = 6,
    ROM_ZIP_TOO_BIG = 7,  /* C side only: the zip or the game inside is over the size limit */
    ROM_ZIP_CANT_READ = 8 /* C side only: the zip file couldn't be read */
};

/* Implemented in rust/hgss (zip.rs). Unpacks the one .gba in a zip into out. *out_len is
 * less than *size when the game didn't fit (the CRC is then unchecked). name gets the
 * game's base name, or "" when it isn't safe to use as a file name. */
uint32_t rom_zip_read(const uint8_t* zip, uint32_t zip_len, uint8_t* out, uint32_t out_cap,
                      uint32_t* out_len, uint32_t* size, uint32_t* crc, char* name, uint32_t name_cap);

const char* rom_zip_reason(uint32_t code);

#endif // ROM_ZIP_H
