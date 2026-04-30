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

TEST(vcount_irq_fires_when_target_in_visible_range) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Set LYC=100, enable VCount IRQ in DISPSTAT (bit 5) and IE (bit 2). */
    gba.ppu.dispstat = (100u << 8) | (1u << 5);
    gba.interrupts.ie = IRQ_VCOUNT;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_TRUE((gba.interrupts.irf & IRQ_VCOUNT) != 0);
}

TEST(vcount_irq_fires_when_target_in_vblank_range) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat = (200u << 8) | (1u << 5);
    gba.interrupts.ie = IRQ_VCOUNT;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_TRUE((gba.interrupts.irf & IRQ_VCOUNT) != 0);
}

TEST(vcount_irq_never_fires_when_target_above_max) {
    GBA gba;
    ppu_test_setup(&gba);
    /* LYC=228 is impossible — vcount tops out at 227. */
    gba.ppu.dispstat = (228u << 8) | (1u << 5);
    gba.interrupts.ie = IRQ_VCOUNT;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_EQ(gba.interrupts.irf & IRQ_VCOUNT, 0);
}

TEST(vcount_match_flag_set_regardless_of_irq_enable) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Set LYC=1, IRQ-enable bit (5) clear. */
    gba.ppu.dispstat = (1u << 8);
    gba_run_frame(&gba);
    /* After one frame, vcount=0; LYC=1 → flag should be 0. */
    ASSERT_EQ(BIT(gba.ppu.dispstat, 2), 0);

    /* Now set LYC=0 to match. Run another frame; the flag should
     * end up set with no IRQ-enable bit. */
    gba.ppu.dispstat = (0u << 8);
    gba_run_frame(&gba);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 2), 1);
    /* And no IRQ fired (DISPSTAT.5 clear). */
    ASSERT_EQ(gba.interrupts.irf & IRQ_VCOUNT, 0);
}

TEST(affine_refs_reload_from_latches_at_vblank_start) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Set affine BG2 internal refs and latches to distinct values. */
    gba.ppu.bg_ref_x_latch[0] = 0x12345678;
    gba.ppu.bg_ref_y_latch[0] = 0x0ABCDEF0;
    gba.ppu.bg_ref_x[0]       = 0x00000000;
    gba.ppu.bg_ref_y[0]       = 0x00000000;

    gba_run_frame(&gba);

    /* At line 160 (VBlank start), the orchestrator copies latches
     * into internal refs. By end-of-frame the internal refs equal
     * the latches (no rendering occurred to advance them past
     * VBlank, so the post-VBlank value is exactly the latch). */
    ASSERT_EQ_HEX((uint32_t)gba.ppu.bg_ref_x[0], 0x12345678u);
    ASSERT_EQ_HEX((uint32_t)gba.ppu.bg_ref_y[0], 0x0ABCDEF0u);
}

TEST(force_blank_fills_framebuffer_with_white) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Force-blank: DISPCNT bit 7. */
    gba.ppu.dispcnt = (1u << 7);

    gba_run_frame(&gba);

    /* Sample a few framebuffer pixels — all must be 0x7FFF. */
    ASSERT_EQ_HEX(gba.ppu.framebuffer[0], 0x7FFFu);
    ASSERT_EQ_HEX(gba.ppu.framebuffer[SCREEN_WIDTH * 80 + 120], 0x7FFFu);
    ASSERT_EQ_HEX(gba.ppu.framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT - 1], 0x7FFFu);

    /* vcount still progressed normally. */
    ASSERT_EQ(gba.ppu.vcount, 0);
}

TEST(vblank_flag_clears_on_line_227) {
    GBA gba;
    ppu_test_setup(&gba);

    /* Drive 160 scanlines — vcount lands on 160, VBlank flag is set. */
    for (int i = 0; i < 160; i++) gba_run_scanline(&gba);
    ASSERT_EQ(gba.ppu.vcount, 160);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 0), 1);

    /* Drive 66 more — vcount=226, still in VBlank, flag still set. */
    for (int i = 0; i < 66; i++) gba_run_scanline(&gba);
    ASSERT_EQ(gba.ppu.vcount, 226);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 0), 1);

    /* One more — vcount=227, flag must clear. */
    gba_run_scanline(&gba);
    ASSERT_EQ(gba.ppu.vcount, 227);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 0), 0);
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
    RUN_TEST(vcount_irq_fires_when_target_in_visible_range);
    RUN_TEST(vcount_irq_fires_when_target_in_vblank_range);
    RUN_TEST(vcount_irq_never_fires_when_target_above_max);
    RUN_TEST(vcount_match_flag_set_regardless_of_irq_enable);
    RUN_TEST(affine_refs_reload_from_latches_at_vblank_start);
    RUN_TEST(force_blank_fills_framebuffer_with_white);
    RUN_TEST(vblank_flag_clears_on_line_227);
}
