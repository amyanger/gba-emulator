#include "test_harness.h"
#include "interrupt/interrupt.h"
#include "ppu/ppu.h"
#include "timer/timer.h"

TEST(interrupt_init_clears_state) {
    InterruptController ic;
    /* Pre-poison so init has something to clear. */
    ic.ime = true;
    ic.ie  = 0xBEEF;
    ic.irf = 0xCAFE;

    interrupt_init(&ic);
    ASSERT_EQ(ic.ime, false);
    ASSERT_EQ(ic.ie, 0);
    ASSERT_EQ(ic.irf, 0);
}

TEST(interrupt_request_sets_irf_bit) {
    InterruptController ic;
    interrupt_init(&ic);

    interrupt_request(&ic, IRQ_VBLANK);
    ASSERT_EQ(ic.irf & IRQ_VBLANK, IRQ_VBLANK);

    /* Multiple requests OR together. */
    interrupt_request(&ic, IRQ_HBLANK);
    ASSERT_EQ(ic.irf & (IRQ_VBLANK | IRQ_HBLANK), IRQ_VBLANK | IRQ_HBLANK);
}

TEST(interrupt_acknowledge_clears_only_set_bits) {
    /* Real hardware: writing 1 to IF clears those bits. Acknowledge
     * with a value clears only the bits set in the value, leaves
     * the rest alone. */
    InterruptController ic;
    interrupt_init(&ic);
    ic.irf = IRQ_VBLANK | IRQ_HBLANK | IRQ_VCOUNT;

    interrupt_acknowledge(&ic, IRQ_HBLANK);
    ASSERT_EQ(ic.irf, IRQ_VBLANK | IRQ_VCOUNT);

    /* Acknowledging a bit that isn't set is a no-op. */
    interrupt_acknowledge(&ic, IRQ_TIMER0);
    ASSERT_EQ(ic.irf, IRQ_VBLANK | IRQ_VCOUNT);
}

TEST(interrupt_pending_requires_ime_ie_irf_match) {
    InterruptController ic;
    interrupt_init(&ic);

    /* IME=0 → never pending even if IE&IF would match. */
    ic.ime = false;
    ic.ie  = IRQ_VBLANK;
    ic.irf = IRQ_VBLANK;
    ASSERT_EQ(interrupt_pending(&ic), false);

    /* IME=1 but IE doesn't gate this IRQ → not pending. */
    ic.ime = true;
    ic.ie  = IRQ_HBLANK;
    ic.irf = IRQ_VBLANK;
    ASSERT_EQ(interrupt_pending(&ic), false);

    /* IME=1, IE matches IRF → pending. */
    ic.ie  = IRQ_VBLANK;
    ASSERT_EQ(interrupt_pending(&ic), true);
}

TEST(interrupt_request_if_enabled_respects_dispstat_bits) {
    /* PPU IRQs (VBlank, HBlank, VCount) are gated by DISPSTAT bits
     * 3-5. If the corresponding bit is clear in DISPSTAT, the IRQ
     * source must NOT mark IRF — even if the condition occurs. */
    InterruptController ic;
    interrupt_init(&ic);
    PPU ppu;
    memset(&ppu, 0, sizeof(ppu));

    /* VBlank IRQ disabled in DISPSTAT (bit 3 clear). */
    ppu.dispstat = 0;
    interrupt_request_if_enabled(&ic, &ppu, IRQ_VBLANK);
    ASSERT_EQ(ic.irf & IRQ_VBLANK, 0);

    /* Enable VBlank IRQ in DISPSTAT (bit 3 = 1 << 3 = 8). */
    ppu.dispstat = (1 << 3);
    interrupt_request_if_enabled(&ic, &ppu, IRQ_VBLANK);
    ASSERT_EQ(ic.irf & IRQ_VBLANK, IRQ_VBLANK);
}

void run_interrupt_tests(void) {
    TEST_SUITE("interrupt");
    RUN_TEST(interrupt_init_clears_state);
    RUN_TEST(interrupt_request_sets_irf_bit);
    RUN_TEST(interrupt_acknowledge_clears_only_set_bits);
    RUN_TEST(interrupt_pending_requires_ime_ie_irf_match);
    RUN_TEST(interrupt_request_if_enabled_respects_dispstat_bits);
}
