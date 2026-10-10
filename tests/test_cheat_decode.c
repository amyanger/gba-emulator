#include "test_harness.h"
#include "cheat/cheat_crypto.h"
#include "cheat/cheat_decode.h"

#define EMERALD_ID 0x45455042u   /* "BPEE" at ROM 0xAC, little-endian */

static CheatRawList raw;
static CheatOp ops[CHEAT_MAX_OPS];
static uint32_t nops;
static CheatDecodeInfo info;

static void add(uint32_t a, uint32_t b, uint8_t digits) {
    raw.lines[raw.count++] = (CheatRawLine){a, b, digits};
}

/* Adds a raw GameShark/Action Replay line encrypted for the given seeds. */
static void add_enc(uint32_t a, uint32_t b, const uint32_t* seeds) {
    cheat_tea_encrypt(&a, &b, seeds);
    add(a, b, 8);
}

static bool decode(CheatDevice dev) {
    return cheat_decode(&raw, dev, ops, CHEAT_MAX_OPS, &nops, &info);
}

static void reset(void) { memset(&raw, 0, sizeof(raw)); }

TEST(cheat_decode_cb_writes) {
    reset();
    add(0x82025838, 0x03E7, 4);
    add(0x32025840, 0x0063, 4);
    ASSERT_TRUE(decode(CHEAT_DEV_CODEBREAKER));
    ASSERT_EQ(nops, 2);
    ASSERT_EQ(ops[0].kind, CHEAT_OP_WRITE);
    ASSERT_EQ(ops[0].width, 2);
    ASSERT_EQ_HEX(ops[0].addr, 0x02025838);
    ASSERT_EQ_HEX(ops[0].value, 0x03E7);
    ASSERT_EQ(ops[1].width, 1);
    ASSERT_EQ_HEX(ops[1].value, 0x63);
}

TEST(cheat_decode_cb_if_and_slide) {
    reset();
    add(0x72025838, 0x0001, 4);   /* IF [a] == 1 */
    add(0x42025900, 0x0005, 4);   /* slide 16-bit value 5 ... */
    add(0x0002000A, 0x0004, 4);   /* ... value step 2, count 10, address step 4 */
    ASSERT_TRUE(decode(CHEAT_DEV_CODEBREAKER));
    ASSERT_EQ(nops, 2);
    ASSERT_EQ(ops[0].kind, CHEAT_OP_IF);
    ASSERT_EQ(ops[0].cmp, CHEAT_CMP_EQ);
    ASSERT_EQ(ops[0].skip, 1);
    ASSERT_EQ(ops[1].kind, CHEAT_OP_SLIDE);
    ASSERT_EQ(ops[1].count, 10);
    ASSERT_EQ(ops[1].value_step, 2);
    ASSERT_EQ(ops[1].addr_step, 4);
}

TEST(cheat_decode_cb_markers_and_keys) {
    reset();
    add(0x0000A1B2, 0x000A, 4);   /* game ID */
    add(0x1004F1C0, 0x0007, 4);   /* hook */
    add(0xD0000020, 0x0001, 4);   /* IF A held */
    add(0x82025838, 0x03E7, 4);
    ASSERT_TRUE(decode(CHEAT_DEV_CODEBREAKER));
    ASSERT_EQ(info.markers, 2);
    ASSERT_EQ(ops[2].kind, CHEAT_OP_IF);
    ASSERT_EQ(ops[2].cmp, CHEAT_CMP_KEYS);
    ASSERT_EQ_HEX(ops[2].value, 0x0001);
}

/* With key 9A0F00FF C4D2, "82025838 03E7" decrypts to 0AD2DDFE 23F8, a game
 * ID marker (vector from mGBA, see test_cheat_crypto.c). Undecrypted it
 * would be a RAM write, so this proves the key line switches decryption on. */
TEST(cheat_decode_cb_key_line_enables_decryption) {
    reset();
    add(0x9A0F00FF, 0xC4D2, 4);
    add(0x82025838, 0x03E7, 4);
    ASSERT_TRUE(decode(CHEAT_DEV_CODEBREAKER));
    ASSERT_EQ(info.markers, 2);
    for (uint32_t i = 0; i < nops; i++) ASSERT_TRUE(ops[i].kind == CHEAT_OP_MARKER);
}

TEST(cheat_decode_gsa_encrypted) {
    reset();
    add_enc(0x1202BC24, 0x0000FFFF, CHEAT_SEEDS_GSA);
    add_enc(0xE0020063, 0x0202BC26, CHEAT_SEEDS_GSA);   /* IF [0202BC26]==0x63 skip 2 */
    add_enc(0x0202BC28, 0x00000001, CHEAT_SEEDS_GSA);
    add_enc(0x2202BC2C, 0x12345678, CHEAT_SEEDS_GSA);
    ASSERT_TRUE(decode(CHEAT_DEV_GSA));
    ASSERT_EQ(nops, 4);
    ASSERT_EQ(ops[0].width, 2);
    ASSERT_EQ_HEX(ops[0].addr, 0x0202BC24);
    ASSERT_EQ(ops[1].kind, CHEAT_OP_IF);
    ASSERT_EQ(ops[1].skip, 2);
    ASSERT_EQ_HEX(ops[1].addr, 0x0202BC26);
    ASSERT_EQ_HEX(ops[1].value, 0x63);
    ASSERT_EQ(ops[3].width, 4);
}

TEST(cheat_decode_gsa_unsupported_lines_are_counted) {
    reset();
    add(0x6000ABCD, 0x10001234, 8);   /* ROM patch */
    add(0xDEADFACE, 0x00001234, 8);   /* seed change */
    add(0x1202BC24, 0x0000FFFF, 8);
    ASSERT_TRUE(decode(CHEAT_DEV_GSA_RAW));
    ASSERT_EQ(info.unsupported, 2);
}

TEST(cheat_decode_par3_assign_maps_address) {
    reset();
    add(0x02224ABC, 0x0000270F, 8);   /* [02024ABC] = 270F */
    add(0x00224AC0, 0x00000363, 8);   /* [02024AC0..+3] = 63 (4 bytes) */
    ASSERT_TRUE(decode(CHEAT_DEV_PAR3_RAW));
    ASSERT_EQ(ops[0].kind, CHEAT_OP_WRITE);
    ASSERT_EQ_HEX(ops[0].addr, 0x02024ABC);
    ASSERT_EQ(ops[0].width, 2);
    ASSERT_EQ(ops[1].kind, CHEAT_OP_SLIDE);
    ASSERT_EQ(ops[1].count, 4);
    ASSERT_EQ(ops[1].addr_step, 1);
    ASSERT_EQ_HEX(ops[1].value, 0x63);
}

TEST(cheat_decode_par3_if_block_else_endif) {
    reset();
    add(0x8A224ABC, 0x00000001, 8);   /* IF [02024ABC] (16-bit) == 1, block */
    add(0x00224AC0, 0x00000011, 8);
    add(0x00000000, 0x60000000, 8);   /* ELSE */
    add(0x00224AC0, 0x00000022, 8);
    add(0x00000000, 0x40000000, 8);   /* ENDIF */
    ASSERT_TRUE(decode(CHEAT_DEV_PAR3_RAW));
    ASSERT_EQ(nops, 5);
    ASSERT_EQ(ops[0].kind, CHEAT_OP_IF);
    ASSERT_EQ(ops[0].skip, CHEAT_SKIP_BLOCK);
    ASSERT_EQ(ops[0].width, 2);
    ASSERT_EQ(ops[2].kind, CHEAT_OP_ELSE);
    ASSERT_EQ(ops[4].kind, CHEAT_OP_ENDIF);
}

TEST(cheat_decode_par3_slide_two_lines) {
    reset();
    add(0x00000000, 0x82224ABC, 8);   /* 16-bit slide at 02024ABC */
    add(0x00001234, 0x0105000A, 8);   /* value 1234, value step 1, count 5, addr step 10 halfwords */
    ASSERT_TRUE(decode(CHEAT_DEV_PAR3_RAW));
    ASSERT_EQ(nops, 1);
    ASSERT_EQ(ops[0].kind, CHEAT_OP_SLIDE);
    ASSERT_EQ_HEX(ops[0].addr, 0x02024ABC);
    ASSERT_EQ_HEX(ops[0].value, 0x1234);
    ASSERT_EQ(ops[0].count, 5);
    ASSERT_EQ(ops[0].value_step, 1);
    ASSERT_EQ(ops[0].addr_step, 20);
}

/* GBATEK counts type 5 in bytes, mGBA and VBA-M in halfwords; until a real
 * code settles it, refuse rather than turn data lines into stray writes. */
TEST(cheat_decode_cb_type5_refused) {
    reset();
    add(0x52025838, 0x0008, 4);
    add(0x11223344, 0x5566, 4);
    add(0x77880000, 0x0000, 4);
    ASSERT_TRUE(!decode(CHEAT_DEV_CODEBREAKER));
    ASSERT_TRUE(strstr(info.fail, "type 5") != NULL);
}

TEST(cheat_decode_rejects_write_outside_ram) {
    reset();
    add(0x88000000, 0x0001, 4);   /* write into ROM */
    ASSERT_TRUE(!decode(CHEAT_DEV_CODEBREAKER));
    ASSERT_TRUE(info.fail[0] != '\0');
}

TEST(cheat_detect_par3_with_id_line) {
    reset();
    add_enc(EMERALD_ID, 0x001DC0DE, CHEAT_SEEDS_PAR3);
    add_enc(0x02224ABC, 0x0000270F, CHEAT_SEEDS_PAR3);
    CheatDetect d;
    cheat_detect(&raw, EMERALD_ID, &d);
    ASSERT_EQ(d.device, CHEAT_DEV_PAR3);
    ASSERT_TRUE(d.confident);
    ASSERT_TRUE(d.id_match);
}

TEST(cheat_detect_gsa_encrypted) {
    reset();
    add_enc(0x1202BC24, 0x0000FFFF, CHEAT_SEEDS_GSA);
    add_enc(0x0202BC28, 0x00000001, CHEAT_SEEDS_GSA);
    CheatDetect d;
    cheat_detect(&raw, EMERALD_ID, &d);
    ASSERT_EQ(d.device, CHEAT_DEV_GSA);
    ASSERT_TRUE(d.confident);
    ASSERT_TRUE(!d.id_match);
}

TEST(cheat_detect_codebreaker_by_shape) {
    reset();
    add(0x82025838, 0x03E7, 4);
    CheatDetect d;
    cheat_detect(&raw, EMERALD_ID, &d);
    ASSERT_EQ(d.device, CHEAT_DEV_CODEBREAKER);
    ASSERT_TRUE(d.confident);
}

/* A lone encrypted PAR3 line also "decodes" raw as a GameShark ROM patch
 * (unsupported). Unsupported-only readings must not tie with a real one. */
TEST(cheat_detect_single_encrypted_line) {
    reset();
    add_enc(0x02224ABC, 0x0000270F, CHEAT_SEEDS_PAR3);
    CheatDetect d;
    cheat_detect(&raw, EMERALD_ID, &d);
    ASSERT_EQ(d.device, CHEAT_DEV_PAR3);
    ASSERT_TRUE(d.confident);
}

/* Most pastes are one encrypted line. Over many random RAM writes, detection
 * must almost always be sure and never pick the wrong device. */
static void detect_batch(const uint32_t* seeds, CheatDevice want, uint32_t* unsure, uint32_t* wrong) {
    uint32_t rng = 12345;
    *unsure = *wrong = 0;
    for (int n = 0; n < 2000; n++) {
        rng = rng * 1103515245u + 12345u;
        uint32_t off = (rng >> 8) % 0x3FFF0u;
        uint32_t type = (rng >> 4) % 3;   /* 8/16/32-bit write */
        uint32_t a, b = rng ^ 0x5A5A5A5Au;
        if (want == CHEAT_DEV_GSA) a = (type << 28) | (0x02000000u + (off & ~3u));
        else a = (type << 25) | 0x00200000u | ((off & ~3u) & 0xFFFFFu);   /* PAR3 packed EWRAM address */
        if (type == 0) b &= 0xFFu;
        else if (type == 1) b &= 0xFFFFu;
        reset();
        add_enc(a, b, seeds);
        CheatDetect d;
        cheat_detect(&raw, EMERALD_ID, &d);
        if (!d.confident) (*unsure)++;
        else if (d.device != want) (*wrong)++;
    }
}

TEST(cheat_detect_single_line_batch_gsa) {
    uint32_t unsure, wrong;
    detect_batch(CHEAT_SEEDS_GSA, CHEAT_DEV_GSA, &unsure, &wrong);
    ASSERT_EQ(wrong, 0);
    ASSERT_TRUE(unsure <= 60);   /* <= 3% */
}

TEST(cheat_detect_single_line_batch_par3) {
    uint32_t unsure, wrong;
    detect_batch(CHEAT_SEEDS_PAR3, CHEAT_DEV_PAR3, &unsure, &wrong);
    ASSERT_EQ(wrong, 0);
    ASSERT_TRUE(unsure <= 60);
}

TEST(cheat_detect_garbage_is_not_confident) {
    reset();
    add(0xFFFFFFFF, 0xFFFFFFFF, 8);
    add(0x7FFFFFFF, 0x12345678, 8);
    CheatDetect d;
    cheat_detect(&raw, EMERALD_ID, &d);
    ASSERT_TRUE(!d.confident);
}

void run_cheat_decode_tests(void) {
    TEST_SUITE("cheat_decode");
    RUN_TEST(cheat_decode_cb_writes);
    RUN_TEST(cheat_decode_cb_if_and_slide);
    RUN_TEST(cheat_decode_cb_markers_and_keys);
    RUN_TEST(cheat_decode_cb_key_line_enables_decryption);
    RUN_TEST(cheat_decode_gsa_encrypted);
    RUN_TEST(cheat_decode_gsa_unsupported_lines_are_counted);
    RUN_TEST(cheat_decode_par3_assign_maps_address);
    RUN_TEST(cheat_decode_par3_if_block_else_endif);
    RUN_TEST(cheat_decode_par3_slide_two_lines);
    RUN_TEST(cheat_decode_cb_type5_refused);
    RUN_TEST(cheat_decode_rejects_write_outside_ram);
    RUN_TEST(cheat_detect_par3_with_id_line);
    RUN_TEST(cheat_detect_gsa_encrypted);
    RUN_TEST(cheat_detect_codebreaker_by_shape);
    RUN_TEST(cheat_detect_single_encrypted_line);
    RUN_TEST(cheat_detect_single_line_batch_gsa);
    RUN_TEST(cheat_detect_single_line_batch_par3);
    RUN_TEST(cheat_detect_garbage_is_not_confident);
}
