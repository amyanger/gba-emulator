#include "test_harness.h"

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
}
