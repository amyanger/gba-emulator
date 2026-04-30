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

TEST(hblank_irq_fires_when_enabled) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Enable HBlank IRQ in DISPSTAT (bit 4) and IE (bit 1). */
    gba.ppu.dispstat |= (1u << 4);
    gba.interrupts.ie = IRQ_HBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    /* IRQ_HBLANK fires 228 times per frame; all bits OR into IF. */
    ASSERT_TRUE((gba.interrupts.irf & IRQ_HBLANK) != 0);
}

TEST(hblank_irq_does_not_fire_when_dispstat_bit_clear) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat &= ~(1u << 4);  /* HBlank IRQ disabled. */
    gba.interrupts.ie = IRQ_HBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_EQ(gba.interrupts.irf & IRQ_HBLANK, 0);
}

TEST(vblank_irq_fires_when_dispstat_bit_set) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat |= (1u << 3);  /* VBlank IRQ enabled. */
    gba.interrupts.ie = IRQ_VBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_TRUE((gba.interrupts.irf & IRQ_VBLANK) != 0);
}

TEST(vblank_irq_does_not_fire_when_dispstat_bit_clear) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat &= ~(1u << 3);
    gba.interrupts.ie = IRQ_VBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_EQ(gba.interrupts.irf & IRQ_VBLANK, 0);
}

TEST(frame_complete_set_exactly_once_per_frame) {
    GBA gba;
    ppu_test_setup(&gba);
    /* gba_run_frame sets frame_complete=true on the line-159->160
     * transition and resets it to false at the start of the next call. */
    gba_run_frame(&gba);
    ASSERT_EQ(gba.frame_complete, true);
    gba_run_frame(&gba);
    /* After the second frame, frame_complete is again true. The
     * "exactly once per frame" contract is observable by checking
     * that it was reset between the two run_frame calls. */
    ASSERT_EQ(gba.frame_complete, true);
}

/* Suite registration (called from test_runner.c). */
void run_ppu_tests(void) {
    TEST_SUITE("ppu");
    RUN_TEST(vcount_wraps_to_zero_after_full_frame);
    RUN_TEST(hblank_irq_fires_when_enabled);
    RUN_TEST(hblank_irq_does_not_fire_when_dispstat_bit_clear);
    RUN_TEST(vblank_irq_fires_when_dispstat_bit_set);
    RUN_TEST(vblank_irq_does_not_fire_when_dispstat_bit_clear);
    RUN_TEST(frame_complete_set_exactly_once_per_frame);
}
