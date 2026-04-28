#include "test_harness.h"
#include <unistd.h>

// Include the .c directly to access static functions (crc32)
// This file must NOT be linked alongside savestate.o
#include "savestate/savestate.c"

// --- CRC32 tests ---

TEST(crc32_empty) {
    uint8_t dummy = 0;
    uint32_t result = crc32(&dummy, 0);
    ASSERT_EQ_HEX(result, 0x00000000);
}

TEST(crc32_known_vector) {
    // Standard CRC32 test: "123456789" -> 0xCBF43926
    const uint8_t data[] = "123456789";
    uint32_t result = crc32(data, 9);
    ASSERT_EQ_HEX(result, 0xCBF43926);
}

TEST(crc32_single_byte) {
    uint8_t data[] = { 0x00 };
    uint32_t result = crc32(data, 1);
    ASSERT_EQ_HEX(result, 0xD202EF8D);
}

// --- Slot path tests ---

TEST(slot_path_basic) {
    char buf[256];
    savestate_slot_path("roms/emerald.gba", 1, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "roms/emerald.gba.ss1");
}

TEST(slot_path_nested_dir) {
    char buf[256];
    savestate_slot_path("/home/user/roms/fire_red.gba", 3, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/home/user/roms/fire_red.gba.ss3");
}

TEST(slot_path_no_extension) {
    char buf[256];
    savestate_slot_path("myrom", 0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "myrom.ss0");
}

TEST(slot_path_slot_9) {
    char buf[256];
    savestate_slot_path("test.gba", 9, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "test.gba.ss9");
}

TEST(slot_path_small_buffer) {
    char buf[10];
    memset(buf, 'X', sizeof(buf));
    savestate_slot_path("roms/emerald.gba", 1, buf, sizeof(buf));
    // Should be truncated but not overflow. buf[9] must be '\0'
    ASSERT_EQ(buf[9], '\0');
}

// --- Buffer API tests (rewind feature) ---

#include "gba.h"

TEST(savestate_buffer_roundtrip) {
    GBA gba;
    gba_init(&gba);

    /* Plant a fingerprint in EWRAM */
    gba.bus.ewram[0]      = 0xDE;
    gba.bus.ewram[1]      = 0xAD;
    gba.bus.ewram[1024]   = 0xBE;
    gba.bus.ewram[1025]   = 0xEF;

    uint8_t* buf = NULL;
    size_t   buf_size = 0;
    SaveStateResult r = savestate_save_to_buffer(&gba, &buf, &buf_size);
    ASSERT_EQ(r, SS_OK);
    ASSERT_TRUE(buf != NULL);
    ASSERT_TRUE(buf_size > 0);

    /* Trash EWRAM */
    memset(gba.bus.ewram, 0, sizeof(gba.bus.ewram));

    r = savestate_load_from_buffer(&gba, buf, buf_size);
    ASSERT_EQ(r, SS_OK);
    ASSERT_EQ(gba.bus.ewram[0],    0xDE);
    ASSERT_EQ(gba.bus.ewram[1],    0xAD);
    ASSERT_EQ(gba.bus.ewram[1024], 0xBE);
    ASSERT_EQ(gba.bus.ewram[1025], 0xEF);

    free(buf);
    gba_destroy(&gba);
}

TEST(savestate_sio_roundtrip) {
    GBA src;
    gba_init(&src);

    /* Plant non-default SIO state on the source GBA */
    src.sio.siocnt        = 0x4082; /* multiplayer + IRQ + start */
    src.sio.siomlt_send   = 0xCAFE;
    src.sio.siomulti[0]   = 0x1111;
    src.sio.siomulti[1]   = 0x2222;
    src.sio.siomulti[2]   = 0x3333;
    src.sio.siomulti[3]   = 0x4444;
    src.sio.rcnt          = 0x0000;
    src.sio.mode          = SIO_MODE_MULTIPLAYER;
    src.sio.serial_mode_enabled = true;
    src.sio.transfer_active = true;
    src.sio.transfer_cycles_remaining = 768;

    uint8_t* buf = NULL;
    size_t   buf_size = 0;
    SaveStateResult r = savestate_save_to_buffer(&src, &buf, &buf_size);
    ASSERT_EQ(r, SS_OK);
    ASSERT_TRUE(buf != NULL);

    /* Load into a fresh GBA — its sio.interrupts pointer must end up
     * pointing at &dst.interrupts, NOT at the source's IRQ controller. */
    GBA dst;
    gba_init(&dst);
    r = savestate_load_from_buffer(&dst, buf, buf_size);
    ASSERT_EQ(r, SS_OK);

    /* Data fields restored exactly */
    ASSERT_EQ_HEX(dst.sio.siocnt,      0x4082);
    ASSERT_EQ_HEX(dst.sio.siomlt_send, 0xCAFE);
    ASSERT_EQ_HEX(dst.sio.siomulti[0], 0x1111);
    ASSERT_EQ_HEX(dst.sio.siomulti[1], 0x2222);
    ASSERT_EQ_HEX(dst.sio.siomulti[2], 0x3333);
    ASSERT_EQ_HEX(dst.sio.siomulti[3], 0x4444);
    ASSERT_EQ((int)dst.sio.mode, (int)SIO_MODE_MULTIPLAYER);
    ASSERT_TRUE(dst.sio.serial_mode_enabled);
    ASSERT_TRUE(dst.sio.transfer_active);
    ASSERT_EQ(dst.sio.transfer_cycles_remaining, 768);

    /* Pointer fields correctly re-wired (no dangling pointer to src) */
    ASSERT_TRUE(dst.sio.interrupts == &dst.interrupts);
    ASSERT_TRUE(dst.sio.peer == NULL);

    free(buf);
    gba_destroy(&src);
    gba_destroy(&dst);
}

extern uint32_t test_savestate_crc32(const uint8_t*, size_t);

TEST(savestate_v5_loads_with_empty_label) {
    GBA gba;
    gba_init(&gba);

    /* Save a v6 buffer */
    uint8_t* buf = NULL;
    size_t size = 0;
    ASSERT_EQ(SS_OK, savestate_save_to_buffer(&gba, &buf, &size));

    /* Synthesize a v5 buffer: remove the 32-byte label region at offset 0x30..0x4F,
     * then patch version=5, fsize, and recompute body CRC. */
    size_t v5_size = size - 32; /* HEADER_SIZE_V5 = 48; HEADER_SIZE = 80; diff = 32 */
    uint8_t* v5_buf = (uint8_t*)malloc(v5_size);
    ASSERT_TRUE(v5_buf != NULL);

    /* Copy header bytes 0..0x2F (first 48 bytes of v6 = all of v5 header) */
    memcpy(v5_buf, buf, 0x30);
    /* Copy body (starts at offset 0x50 in v6, goes to offset 0x30 in v5) */
    memcpy(v5_buf + 0x30, buf + 0x50, size - 0x50);

    /* Patch version to 5 */
    v5_buf[4] = 5;
    v5_buf[5] = 0;
    v5_buf[6] = 0;
    v5_buf[7] = 0;

    /* Patch fsize */
    uint32_t v5_sz = (uint32_t)v5_size;
    v5_buf[8]  = (uint8_t)(v5_sz & 0xFF);
    v5_buf[9]  = (uint8_t)((v5_sz >> 8) & 0xFF);
    v5_buf[10] = (uint8_t)((v5_sz >> 16) & 0xFF);
    v5_buf[11] = (uint8_t)((v5_sz >> 24) & 0xFF);

    /* Recompute body CRC over bytes [48..v5_size) */
    uint32_t new_crc = test_savestate_crc32(v5_buf + 48, v5_size - 48);
    v5_buf[12] = (uint8_t)(new_crc & 0xFF);
    v5_buf[13] = (uint8_t)((new_crc >> 8) & 0xFF);
    v5_buf[14] = (uint8_t)((new_crc >> 16) & 0xFF);
    v5_buf[15] = (uint8_t)((new_crc >> 24) & 0xFF);

    /* Load the synthesized v5 buffer — must succeed */
    gba_init(&gba);
    ASSERT_EQ(SS_OK, savestate_load_from_buffer(&gba, v5_buf, v5_size));

    /* Label must be empty */
    char label[32];
    ASSERT_EQ(true, savestate_buffer_peek_label(v5_buf, v5_size, label, sizeof(label)));
    ASSERT_STR_EQ("", label);

    free(buf);
    free(v5_buf);
    gba_destroy(&gba);
}

TEST(savestate_save_writes_v6_with_label) {
    GBA gba;
    gba_init(&gba);
    uint8_t* buf = NULL;
    size_t size = 0;
    ASSERT_EQ(SS_OK, savestate_save_to_buffer(&gba, &buf, &size));
    ASSERT_EQ(true, savestate_buffer_set_label(buf, size, "before-rival"));

    char label[32];
    ASSERT_EQ(true, savestate_buffer_peek_label(buf, size, label, sizeof(label)));
    ASSERT_STR_EQ("before-rival", label);

    ASSERT_EQ(6, buf[4]);

    gba_init(&gba);
    ASSERT_EQ(SS_OK, savestate_load_from_buffer(&gba, buf, size));

    free(buf);
    gba_destroy(&gba);
}

TEST(savestate_label_truncates_at_31_bytes) {
    GBA gba;
    gba_init(&gba);
    uint8_t* buf = NULL;
    size_t size = 0;
    ASSERT_EQ(SS_OK, savestate_save_to_buffer(&gba, &buf, &size));

    char big[51];
    memset(big, 'A', 50);
    big[50] = '\0';
    ASSERT_EQ(true, savestate_buffer_set_label(buf, size, big));

    char out[32];
    ASSERT_EQ(true, savestate_buffer_peek_label(buf, size, out, sizeof(out)));
    ASSERT_EQ(31, (int)strlen(out));
    free(buf);
    gba_destroy(&gba);
}

TEST(savestate_label_filters_control_chars) {
    GBA gba;
    gba_init(&gba);
    uint8_t* buf = NULL;
    size_t size = 0;
    ASSERT_EQ(SS_OK, savestate_save_to_buffer(&gba, &buf, &size));

    ASSERT_EQ(true, savestate_buffer_set_label(buf, size,
              "ab\x01""c\x1F""d\x7F""e"));

    char out[32];
    savestate_buffer_peek_label(buf, size, out, sizeof(out));
    ASSERT_STR_EQ("abcde", out);
    free(buf);
    gba_destroy(&gba);
}

TEST(savestate_peek_label_reads_without_full_load) {
    GBA gba;
    gba_init(&gba);

    /* Save with a label. */
    uint8_t* buf = NULL;
    size_t size = 0;
    ASSERT_EQ(SS_OK, savestate_save_to_buffer(&gba, &buf, &size));
    ASSERT_EQ(true, savestate_buffer_set_label(buf, size, "test-label"));

    /* Write to a temp file. */
    char path[] = "/tmp/gba_peek_label_XXXXXX";
    int fd = mkstemp(path);
    ASSERT_TRUE(fd >= 0);
    ssize_t wrote = write(fd, buf, size);
    close(fd);
    ASSERT_EQ((ssize_t)size, wrote);

    /* Mutate the in-memory GBA so we can detect any restoration. */
    uint32_t marker = 0xDEADBEEF;
    gba.cpu.regs[0] = marker;

    /* Peek — should read the label without touching GBA state. */
    char label[32];
    ASSERT_EQ(true, savestate_peek_label(path, label, sizeof(label)));
    ASSERT_STR_EQ("test-label", label);
    ASSERT_EQ(marker, gba.cpu.regs[0]);  /* unchanged */

    remove(path);
    free(buf);
    gba_destroy(&gba);
}

TEST(savestate_upgrade_v5_to_v6_preserves_body) {
    GBA gba;
    gba_init(&gba);

    /* Save a v6 buffer. */
    uint8_t* v6_orig = NULL;
    size_t v6_size = 0;
    ASSERT_EQ(SS_OK, savestate_save_to_buffer(&gba, &v6_orig, &v6_size));

    /* Synthesize a v5 buffer by removing the 32-byte label region
     * (offset 0x30..0x4F), then patching version=5 and fsize. */
    size_t v5_size = v6_size - 32;
    uint8_t* v5 = (uint8_t*)malloc(v5_size);
    ASSERT_TRUE(v5 != NULL);
    memcpy(v5, v6_orig, 0x30);
    memcpy(v5 + 0x30, v6_orig + 0x50, v6_size - 0x50);

    v5[4] = 5; v5[5] = 0; v5[6] = 0; v5[7] = 0;
    uint32_t fsize = (uint32_t)v5_size;
    v5[8]  = fsize & 0xFF;
    v5[9]  = (fsize >> 8) & 0xFF;
    v5[10] = (fsize >> 16) & 0xFF;
    v5[11] = (fsize >> 24) & 0xFF;
    /* Recompute body CRC for the synthesized v5 buffer so it passes load. */
    uint32_t crc = test_savestate_crc32(v5 + 48, v5_size - 48);
    v5[12] = crc & 0xFF;
    v5[13] = (crc >> 8) & 0xFF;
    v5[14] = (crc >> 16) & 0xFF;
    v5[15] = (crc >> 24) & 0xFF;

    /* Upgrade. */
    uint8_t* upgraded = NULL;
    size_t upgraded_size = 0;
    ASSERT_EQ(SS_OK, savestate_buffer_upgrade_v5(v5, v5_size,
                                                 &upgraded, &upgraded_size));
    ASSERT_EQ(upgraded_size, v6_size);
    ASSERT_EQ(upgraded[4], 6);

    /* The upgraded body (chunks) must equal the original v6 body.
     * v6_orig has an empty label (zeroed) so header bytes 0x50..end match. */
    ASSERT_EQ(0, memcmp(upgraded + 0x50, v6_orig + 0x50, v6_size - 0x50));

    /* Loading the upgraded buffer must succeed. */
    gba_init(&gba);
    ASSERT_EQ(SS_OK, savestate_load_from_buffer(&gba, upgraded, upgraded_size));

    free(v5);
    free(v6_orig);
    free(upgraded);
    gba_destroy(&gba);
}

void run_savestate_tests(void) {
    TEST_SUITE("savestate");
    RUN_TEST(crc32_empty);
    RUN_TEST(crc32_known_vector);
    RUN_TEST(crc32_single_byte);
    RUN_TEST(slot_path_basic);
    RUN_TEST(slot_path_nested_dir);
    RUN_TEST(slot_path_no_extension);
    RUN_TEST(slot_path_slot_9);
    RUN_TEST(slot_path_small_buffer);
    RUN_TEST(savestate_buffer_roundtrip);
    RUN_TEST(savestate_sio_roundtrip);
    RUN_TEST(savestate_v5_loads_with_empty_label);
    RUN_TEST(savestate_save_writes_v6_with_label);
    RUN_TEST(savestate_label_truncates_at_31_bytes);
    RUN_TEST(savestate_label_filters_control_chars);
    RUN_TEST(savestate_peek_label_reads_without_full_load);
    RUN_TEST(savestate_upgrade_v5_to_v6_preserves_body);
}
