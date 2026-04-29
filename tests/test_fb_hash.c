#include "test_harness.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "headless/fb_hash.h"

#define FB_W 240
#define FB_H 160
#define FB_PIXELS (FB_W * FB_H)

TEST(fb_hash_empty_input_is_offset_basis) {
    ASSERT_EQ_HEX(fb_hash_fnv1a(NULL, 0), 0x811c9dc5u);
}

TEST(fb_hash_changes_on_single_pixel_change) {
    static uint16_t fb_a[FB_PIXELS];
    static uint16_t fb_b[FB_PIXELS];
    memset(fb_a, 0, sizeof(fb_a));
    memset(fb_b, 0, sizeof(fb_b));
    fb_b[12345] = 0x7FFF;

    uint32_t h_a = fb_hash_fnv1a(fb_a, FB_PIXELS);
    uint32_t h_b = fb_hash_fnv1a(fb_b, FB_PIXELS);
    ASSERT_NEQ(h_a, h_b);
}

TEST(fb_hash_is_byte_order_stable) {
    uint16_t pixels[2] = {0x1234, 0xABCD};

    /* Manually fold the little-endian byte sequence 0x34, 0x12, 0xCD, 0xAB
       through FNV-1a so the test pins the byte order, not just the result. */
    uint32_t expected = 0x811c9dc5u;
    const uint8_t bytes[4] = {0x34, 0x12, 0xCD, 0xAB};
    for (size_t i = 0; i < sizeof(bytes); i++) {
        expected ^= (uint32_t)bytes[i];
        expected *= 0x01000193u;
    }

    ASSERT_EQ_HEX(fb_hash_fnv1a(pixels, 2), expected);
}

void run_fb_hash_tests(void) {
    TEST_SUITE("fb_hash");
    RUN_TEST(fb_hash_empty_input_is_offset_basis);
    RUN_TEST(fb_hash_changes_on_single_pixel_change);
    RUN_TEST(fb_hash_is_byte_order_stable);
}
