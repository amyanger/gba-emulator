#include "test_harness.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "screenshot/screenshot.h"

TEST(screenshot_path_appends_utc_timestamp) {
    /* 2026-04-27 14:30:22 UTC = 1777300222 */
    char buf[256];
    screenshot_path("/home/user/roms/emerald.gba", (time_t)1777300222, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "/home/user/roms/emerald.gba.20260427-143022.png");
}

TEST(screenshot_path_handles_null_rom) {
    char buf[64];
    screenshot_path(NULL, (time_t)0, buf, sizeof(buf));
    /* "screenshot.19700101-000000.png" */
    ASSERT_STR_EQ(buf, "screenshot.19700101-000000.png");
}

TEST(screenshot_path_truncates_safely) {
    char buf[8];
    screenshot_path("/very/long/rom/path/emerald.gba", (time_t)0, buf, sizeof(buf));
    /* Must be null-terminated; snprintf semantics. */
    ASSERT_EQ(buf[7], '\0');
}

TEST(screenshot_save_writes_valid_png) {
    /* Build a 240x160 framebuffer with a recognizable gradient. */
    static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT];
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint16_t r = (uint16_t)(x & 0x1F);
            uint16_t g = (uint16_t)(y & 0x1F);
            uint16_t b = (uint16_t)((x ^ y) & 0x1F);
            fb[y * SCREEN_WIDTH + x] = (uint16_t)(r | (g << 5) | (b << 10));
        }
    }

    const char* path = "test_screenshot_output.png";
    remove(path);

    bool ok = screenshot_save(fb, path);
    ASSERT_TRUE(ok);

    /* Verify PNG magic: 0x89 'P' 'N' 'G' \r \n 0x1A \n */
    FILE* fp = fopen(path, "rb");
    ASSERT_TRUE(fp != NULL);
    uint8_t magic[8] = {0};
    size_t got = fread(magic, 1, sizeof(magic), fp);
    fclose(fp);
    ASSERT_EQ(got, (size_t)8);

    static const uint8_t expected[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    ASSERT_MEM_EQ(magic, expected, 8);

    remove(path);
}

TEST(screenshot_save_rejects_null_inputs) {
    static uint16_t fb[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};
    ASSERT_TRUE(!screenshot_save(NULL, "x.png"));
    ASSERT_TRUE(!screenshot_save(fb, NULL));
}

void run_screenshot_tests(void) {
    TEST_SUITE("screenshot");
    RUN_TEST(screenshot_path_appends_utc_timestamp);
    RUN_TEST(screenshot_path_handles_null_rom);
    RUN_TEST(screenshot_path_truncates_safely);
    RUN_TEST(screenshot_save_writes_valid_png);
    RUN_TEST(screenshot_save_rejects_null_inputs);
}
