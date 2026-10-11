#include "test_harness.h"
#include "hgss/hgss_msg.h"

static void w16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* Builds a 2-entry bank (seed 0x1234) holding lines a and b (HGSS_CH_END-terminated). */
static uint32_t build_bank(uint8_t* bank, const uint16_t* a, uint32_t alen, const uint16_t* b, uint32_t blen) {
    const uint16_t seed = 0x1234;
    const uint16_t* line[2] = {a, b};
    uint32_t len[2] = {alen, blen};
    w16(bank, 2);
    w16(bank + 2, seed);
    uint32_t off = 4 + 2 * 8;
    for (uint32_t i = 1; i <= 2; i++) {
        uint32_t k = (seed * 765u * i) & 0xFFFFu;
        k |= k << 16;
        w32(bank + 4 + 8 * (i - 1), off ^ k);
        w32(bank + 8 + 8 * (i - 1), len[i - 1] ^ k);
        uint16_t key = (uint16_t)((i * 596947u) & 0xFFFFu);
        for (uint32_t c = 0; c < len[i - 1]; c++) {
            w16(bank + off + 2 * c, (uint16_t)(line[i - 1][c] ^ key));
            key = (uint16_t)(key + 18749u);
        }
        off += 2 * len[i - 1];
    }
    return off;
}

TEST(msg_decode_round_trip) {
    static uint8_t bank[256];
    const uint16_t a[] = {0x012B, 0x0145, HGSS_CH_END};                          /* "Aa" */
    const uint16_t b[] = {0x0121, HGSS_CH_NEWLINE, 0x0122, HGSS_CH_END};         /* "0\n1" */
    uint32_t len = build_bank(bank, a, 3, b, 4);
    ASSERT_EQ(hgss_msg_count(bank, len), 2);
    uint16_t out[16];
    uint32_t n = 0;
    ASSERT_TRUE(hgss_msg_decode(bank, len, 1, out, 16, &n));
    ASSERT_EQ(n, 3);
    ASSERT_EQ_HEX(out[1], HGSS_CH_NEWLINE);
    ASSERT_EQ_HEX(out[3], HGSS_CH_END);
    ASSERT_TRUE(!hgss_msg_decode(bank, len, 2, out, 16, &n));
    ASSERT_TRUE(!hgss_msg_decode(bank, 10, 0, out, 16, &n));   /* table truncated */
}

TEST(msg_expand_strvar_and_drop_other_ctrl) {
    HgssStrVars v;
    memset(&v, 0xFF, sizeof(v));
    v.s[0][0] = 0x012B; v.s[0][1] = 0x012C; v.s[0][2] = HGSS_CH_END;           /* "AB" */
    const uint16_t in[] = {HGSS_CH_CTRL, 0x0101, 1, 0, 0x01AB,                  /* {nick}! */
                           HGSS_CH_CTRL, 0x0300, 1, 7, 0x01AC, HGSS_CH_END};    /* color code dropped, ? */
    uint16_t out[16];
    uint32_t n = hgss_msg_expand(in, 10, &v, out, 16);
    ASSERT_EQ(n, 4);
    ASSERT_EQ_HEX(out[0], 0x012B);
    ASSERT_EQ_HEX(out[1], 0x012C);
    ASSERT_EQ_HEX(out[2], 0x01AB);
    ASSERT_EQ_HEX(out[3], 0x01AC);
    ASSERT_EQ_HEX(out[4], HGSS_CH_END);
    const uint16_t yn[] = {0x012B, HGSS_CH_CTRL, 0x0200, 0, HGSS_CH_END};
    ASSERT_TRUE(hgss_msg_has_yesno(yn, 4));
    ASSERT_TRUE(!hgss_msg_has_yesno(in, 10));
}

TEST(msg_expand_truncates_safely) {
    HgssStrVars v;
    memset(&v, 0xFF, sizeof(v));
    for (int i = 0; i < HGSS_STRVAR_MAX - 1; i++) v.s[0][i] = 0x012B;
    v.s[0][HGSS_STRVAR_MAX - 1] = HGSS_CH_END;
    const uint16_t in[] = {HGSS_CH_CTRL, 0x0101, 1, 0, HGSS_CH_CTRL, 0x0101, 1, 0, HGSS_CH_END};
    uint16_t out[10];
    uint32_t n = hgss_msg_expand(in, 8, &v, out, 10);
    ASSERT_EQ(n, 9);
    ASSERT_EQ_HEX(out[9], HGSS_CH_END);
}

TEST(gen3_charset_maps) {
    ASSERT_EQ_HEX(hgss_char_from_gen3(0xBB), 0x012B);   /* A */
    ASSERT_EQ_HEX(hgss_char_from_gen3(0xEE), 0x015E);   /* z */
    ASSERT_EQ_HEX(hgss_char_from_gen3(0xA1), 0x0121);   /* 0 */
    ASSERT_EQ_HEX(hgss_char_from_gen3(0x00), 0x01DE);   /* space */
    ASSERT_EQ_HEX(hgss_char_from_gen3(0xB4), 0x01B3);   /* ’ */
    ASSERT_EQ_HEX(hgss_char_from_gen3(0xB5), 0x01BB);   /* ♂ */
    ASSERT_EQ_HEX(hgss_char_from_gen3(0x7F), 0x01AC);   /* unmapped -> ? */
    const uint8_t name[] = {0xCA, 0xDD, 0xDF, 0xD5, 0xFF};   /* "Pika" */
    uint16_t out[8];
    hgss_str_from_gen3(name, 10, out, 8);
    ASSERT_EQ_HEX(out[0], 0x013A);
    ASSERT_EQ_HEX(out[4], HGSS_CH_END);
}

void run_hgss_msg_tests(void) {
    RUN_TEST(msg_decode_round_trip);
    RUN_TEST(msg_expand_strvar_and_drop_other_ctrl);
    RUN_TEST(msg_expand_truncates_safely);
    RUN_TEST(gen3_charset_maps);
}
