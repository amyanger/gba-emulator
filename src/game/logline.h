#ifndef LOGLINE_H
#define LOGLINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Shared text codec of the .run and .nuzlocke logs: "<type> key=value key=value ...". */

/* Copies line into buf without its trailing CR/LF and splits it on spaces into tok[].
 * Returns the token count, or 0 when the line is empty, longer than buf, or has more
 * than max tokens. */
int logline_tokens(const char* line, char* buf, size_t buf_size, char** tok, int max);

/* Splits a "key=value" token in place. False when there is no '=' or the key is empty. */
bool logline_field(char* tok, char** key, char** value);

/* Plain unsigned numbers only (no sign, space or overflow), at most max. */
bool logline_u32(const char* s, int base, uint32_t max, uint32_t* out);
bool logline_u16(const char* s, uint16_t* out);
bool logline_u8(const char* s, uint8_t* out);
bool logline_bool(const char* s, bool* out);

/* Index of v in names[0..count), as out. */
bool logline_name(const char* v, const char* const* names, int count, uint8_t* out);

/* n bytes as 2n uppercase hex digits plus a NUL (out holds 2n + 1). */
void logline_hex_encode(char* out, const uint8_t* bytes, size_t n);
/* Exactly 2n hex digits (either case) into n bytes. */
bool logline_hex_decode(const char* s, uint8_t* bytes, size_t n);

#endif // LOGLINE_H
