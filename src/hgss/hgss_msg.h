#ifndef HGSS_MSG_H
#define HGSS_MSG_H

#include <stdbool.h>
#include <stdint.h>

#define HGSS_CH_END      0xFFFFu
#define HGSS_CH_CTRL     0xFFFEu
#define HGSS_CH_NEWLINE  0xE000u
#define HGSS_CH_SCROLL   0x25BCu
#define HGSS_CH_NEWBOX   0x25BDu
#define HGSS_CH_SPACE    0x01DEu
#define HGSS_MSG_MAX     512        /* u16 units per decoded line */
#define HGSS_STRVARS     5          /* 0 nickname, 1 species, 2 player, 3 location, 4 held item */
#define HGSS_STRVAR_MAX  24

typedef struct {
    uint16_t s[HGSS_STRVARS][HGSS_STRVAR_MAX];   /* HGSS charcodes, HGSS_CH_END-terminated */
} HgssStrVars;

/* Entry `index` (0-based) of an encrypted msg bank -> raw charcodes incl. control
 * codes, HGSS_CH_END-terminated. *n excludes the terminator. */
bool hgss_msg_decode(const uint8_t* bank, uint32_t bank_len, uint32_t index, uint16_t* out, uint32_t cap,
                     uint32_t* n);
uint16_t hgss_msg_count(const uint8_t* bank, uint32_t bank_len);
/* Replaces STRVAR control codes (cmd 0x0100-0x01FF, buffer = arg0 & 0xFF) with
 * vars; drops every other control code. Output HGSS_CH_END-terminated. */
uint32_t hgss_msg_expand(const uint16_t* in, uint32_t n, const HgssStrVars* vars, uint16_t* out, uint32_t cap);
/* True if the line contains a control code with cmd == 0x0200 (YESNO). */
bool hgss_msg_has_yesno(const uint16_t* in, uint32_t n);
/* Gen 3 (Emerald) text byte -> HGSS charcode; '?' (0x01AC) when unmapped. */
uint16_t hgss_char_from_gen3(uint8_t c);
/* Gen 3 string (0xFF-terminated, max len bytes) -> HGSS charcodes, HGSS_CH_END-terminated. */
void hgss_str_from_gen3(const uint8_t* src, uint32_t len, uint16_t* dst, uint32_t cap);

#endif // HGSS_MSG_H
