#include "hgss/hgss_msg.h"

#include <string.h>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t hgss_msg_count(const uint8_t* bank, uint32_t bank_len) {
    return bank_len < 4 ? 0 : rd16(bank);
}

bool hgss_msg_decode(const uint8_t* bank, uint32_t bank_len, uint32_t index, uint16_t* out, uint32_t cap,
                     uint32_t* n) {
    uint16_t count = hgss_msg_count(bank, bank_len);
    if (index >= count || cap == 0 || 4 + 8 * (index + 1) > bank_len) return false;
    uint32_t i = index + 1;
    uint32_t k = (rd16(bank + 2) * 765u * i) & 0xFFFFu;
    k |= k << 16;
    uint32_t off = rd32(bank + 4 + 8 * index) ^ k;
    uint32_t len = rd32(bank + 8 + 8 * index) ^ k;
    if (off > bank_len || len > (bank_len - off) / 2) return false;
    uint16_t key = (uint16_t)((i * 596947u) & 0xFFFFu);
    uint32_t w = 0;
    for (uint32_t c = 0; c < len && w + 1 < cap; c++) {
        uint16_t ch = (uint16_t)(rd16(bank + off + 2 * c) ^ key);
        key = (uint16_t)(key + 18749u);
        if (ch == HGSS_CH_END) break;
        out[w++] = ch;
    }
    out[w] = HGSS_CH_END;
    *n = w;
    return true;
}

uint32_t hgss_msg_expand(const uint16_t* in, uint32_t n, const HgssStrVars* vars, uint16_t* out, uint32_t cap) {
    uint32_t w = 0;
    if (cap == 0) return 0;
    for (uint32_t i = 0; i < n && in[i] != HGSS_CH_END; i++) {
        if (in[i] == HGSS_CH_CTRL) {
            if (i + 2 >= n) break;
            uint16_t cmd = in[i + 1], argc = in[i + 2];
            if (cmd >= 0x0100 && cmd <= 0x01FF && argc >= 1 && i + 3 < n) {
                uint16_t buf = (uint16_t)(in[i + 3] & 0xFF);
                if (buf < HGSS_STRVARS)
                    for (uint32_t k = 0; k < HGSS_STRVAR_MAX && vars->s[buf][k] != HGSS_CH_END && w + 1 < cap; k++)
                        out[w++] = vars->s[buf][k];
            }
            i += 2u + argc;
            continue;
        }
        if (w + 1 < cap) out[w++] = in[i];
    }
    out[w] = HGSS_CH_END;
    return w;
}

bool hgss_msg_has_yesno(const uint16_t* in, uint32_t n) {
    for (uint32_t i = 0; i + 1 < n && in[i] != HGSS_CH_END; i++) {
        if (in[i] != HGSS_CH_CTRL) continue;
        if (in[i + 1] == 0x0200) return true;
        if (i + 2 < n) i += 2u + in[i + 2];
    }
    return false;
}

/* Gen 3 charset: pokeemerald charmap.txt; HGSS: pokeheartgold charmap.txt. */
uint16_t hgss_char_from_gen3(uint8_t c) {
    if (c >= 0xBB && c <= 0xD4) return (uint16_t)(0x012B + (c - 0xBB));   /* A-Z */
    if (c >= 0xD5 && c <= 0xEE) return (uint16_t)(0x0145 + (c - 0xD5));   /* a-z */
    if (c >= 0xA1 && c <= 0xAA) return (uint16_t)(0x0121 + (c - 0xA1));   /* 0-9 */
    switch (c) {
    case 0x00: return HGSS_CH_SPACE;
    case 0xAB: return 0x01AB;   /* ! */
    case 0xAC: return 0x01AC;   /* ? */
    case 0xAD: return 0x01AE;   /* . */
    case 0xAE: return 0x01BE;   /* - */
    case 0xB0: return 0x01AF;   /* … */
    case 0xB1: return 0x01B4;   /* “ */
    case 0xB2: return 0x01B5;   /* ” */
    case 0xB3: return 0x01B2;   /* ‘ */
    case 0xB4: return 0x01B3;   /* ’ */
    case 0xB5: return 0x01BB;   /* ♂ */
    case 0xB6: return 0x01BC;   /* ♀ */
    case 0xB8: return 0x01AD;   /* , */
    case 0xBA: return 0x01B1;   /* / */
    case 0xF0: return 0x01C4;   /* : */
    default: return 0x01AC;
    }
}

void hgss_str_from_gen3(const uint8_t* src, uint32_t len, uint16_t* dst, uint32_t cap) {
    uint32_t w = 0;
    if (cap == 0) return;
    for (uint32_t i = 0; i < len && src[i] != 0xFF && w + 1 < cap; i++) dst[w++] = hgss_char_from_gen3(src[i]);
    dst[w] = HGSS_CH_END;
}
