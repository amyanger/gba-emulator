#ifndef GEN3_TEXT_H
#define GEN3_TEXT_H

#include <stddef.h>
#include <stdint.h>

/* Decode a Gen 3 string (pokeemerald charmap.txt) to ASCII. Stops at 0xFF
 * or src_len, whichever comes first; full-length names have no 0xFF.
 * Non-ASCII glyphs are approximated (e-acute -> e, male/female -> M/F,
 * PK MN -> PkMn); unknown bytes become '?'. */
size_t gen3_decode(const uint8_t* src, size_t src_len, char* out, size_t out_size);

#endif // GEN3_TEXT_H
