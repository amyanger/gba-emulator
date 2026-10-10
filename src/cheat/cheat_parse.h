#ifndef CHEAT_PARSE_H
#define CHEAT_PARSE_H

#include <stddef.h>
#include "cheat/cheat_types.h"

/* Pulls code pairs out of text pasted from a website: any spacing, dashes,
 * colons, CR/LF, labels and comments. 8 hex digits followed by 4 or 8 hex
 * digits (or 12/16 run together) is a code. The first label line becomes the
 * suggested name. On any malformed line the whole paste is rejected and err
 * explains which line and why. */
bool cheat_parse_text(const char* text, CheatRawList* out, char* err, size_t err_len);

#endif // CHEAT_PARSE_H
