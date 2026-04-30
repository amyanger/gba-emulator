#include "test_harness.h"
#include "gba.h"

/* Helper: build a GBA in a clean reset state, with no ROM loaded.
 * gba_run_frame works against the bus's empty memory and HLE BIOS,
 * which is sufficient for timing-edge tests that don't depend on
 * actual instructions executing. */
static void ppu_test_setup(GBA* gba) {
    gba_init(gba);
}

TEST(vcount_wraps_to_zero_after_full_frame) {
    GBA gba;
    ppu_test_setup(&gba);
    gba_run_frame(&gba);
    /* Loop runs 228 times; each iter increments vcount; wraps 227 -> 0. */
    ASSERT_EQ(gba.ppu.vcount, 0);
}

/* Suite registration (called from test_runner.c). */
void run_ppu_tests(void) {
    TEST_SUITE("ppu");
    RUN_TEST(vcount_wraps_to_zero_after_full_frame);
}
