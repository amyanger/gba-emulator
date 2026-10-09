#include "test_harness.h"
#include "timer/timer.h"
#include "interrupt/interrupt.h"

/* Timer tests run without an APU; timer_tick tolerates apu==NULL. */

TEST(timer_init_zeros_state_and_sets_prescaler_to_one) {
    Timer ts[4];
    /* Pre-poison so init must clear. */
    memset(ts, 0xFF, sizeof(ts));

    timer_init(ts);
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(ts[i].counter, 0);
        ASSERT_EQ(ts[i].reload, 0);
        ASSERT_EQ(ts[i].control, 0);
        ASSERT_EQ(ts[i].prescaler, 1);
        ASSERT_EQ(ts[i].cascade, false);
        ASSERT_EQ(ts[i].irq_enable, false);
        ASSERT_EQ(ts[i].enabled, false);
    }
}

TEST(timer_enable_reloads_counter_from_reload_value) {
    /* On rising edge of enable bit, the counter latches from reload. */
    Timer ts[4];
    timer_init(ts);

    timer_write_reload(&ts[0], 0xFF00);
    /* Counter should still be 0 — reload only loads on enable. */
    ASSERT_EQ(ts[0].counter, 0);

    /* Enable bit is bit 7. */
    timer_write_control(&ts[0], 0x80);
    ASSERT_EQ(ts[0].counter, 0xFF00);
    ASSERT_EQ(ts[0].enabled, true);
}

TEST(timer_basic_tick_increments_counter) {
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    timer_write_reload(&ts[0], 0);
    timer_write_control(&ts[0], 0x80); /* prescaler=1, enabled */
    /* Tick a few cycles. With prescaler=1, the counter advances by
     * exactly that many. */
    timer_tick(ts, 100, &ic, NULL);
    ASSERT_EQ(ts[0].counter, 100);
}

TEST(timer_overflow_fires_irq_when_enabled) {
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    /* Reload near the top so a small tick triggers overflow. */
    timer_write_reload(&ts[1], 0xFFFE);
    /* Enabled, IRQ enabled (bit 6), prescaler=1 (bits 0-1=00). */
    timer_write_control(&ts[1], 0xC0);
    /* Sanity: rising-edge enable latched 0xFFFE into counter. */
    ASSERT_EQ(ts[1].counter, 0xFFFE);

    /* Tick 3 cycles: 0xFFFE -> 0xFFFF -> overflow → reload to 0xFFFE,
     * one more increment to 0xFFFF. */
    timer_tick(ts, 3, &ic, NULL);
    ASSERT_EQ(ic.irf & IRQ_TIMER1, IRQ_TIMER1);
}

TEST(timer_overflow_does_not_fire_irq_when_disabled) {
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    timer_write_reload(&ts[2], 0xFFFE);
    /* Enabled but IRQ-disabled (bit 6 clear). */
    timer_write_control(&ts[2], 0x80);
    timer_tick(ts, 3, &ic, NULL);
    ASSERT_EQ(ic.irf & IRQ_TIMER2, 0);
}

TEST(timer_cascade_increments_only_on_lower_overflow) {
    /* Timer N overflowing increments cascade-mode timer N+1 by 1
     * regardless of N+1's prescaler. Verify the basic behavior. */
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    /* Timer 0: reload=0, prescaler=1. Takes 0x10000 ticks per overflow. */
    timer_write_reload(&ts[0], 0);
    timer_write_control(&ts[0], 0x80);          /* prescaler=1, enabled */

    /* Timer 1: cascade mode (bit 2), enabled. Prescaler doesn't apply. */
    timer_write_reload(&ts[1], 0);
    timer_write_control(&ts[1], 0x84);          /* enabled + cascade */

    /* Tick 100 cycles. Timer 0 only reaches 100 — well below overflow.
     * Cascade timer 1 must NOT advance from raw cycles. */
    timer_tick(ts, 100, &ic, NULL);
    ASSERT_EQ(ts[0].counter, 100);
    ASSERT_EQ(ts[1].counter, 0);

    /* Tick enough cycles to overflow timer 0 exactly once. */
    timer_tick(ts, 0x10000 - 100, &ic, NULL);
    ASSERT_EQ(ts[1].counter, 1);
}

TEST(timer_read_projects_unsynced_cycles) {
    /* Timers only sync at scanline-chunk boundaries; a mid-chunk read
     * must project the counter forward by the cycles the CPU has run
     * since the last sync, or games see values stale by up to ~960
     * cycles (RNG divergence). */
    Timer ts[4];
    timer_init(ts);

    timer_write_reload(&ts[0], 0x1000);
    timer_write_control(&ts[0], 0x80); /* enable, prescaler 1 */
    ASSERT_EQ_HEX(timer_read_counter(&ts[0], 0), 0x1000);

    /* 100 unsynced cycles at prescaler 1 → counter reads +100, without
     * mutating the timer itself. */
    ASSERT_EQ_HEX(timer_read_counter(&ts[0], 100), 0x1064);
    ASSERT_EQ_HEX(ts[0].counter, 0x1000);
}

TEST(timer_read_projection_respects_prescaler_and_wrap) {
    Timer ts[4];
    timer_init(ts);

    /* Prescaler 64: 130 cycles = 2 ticks. */
    timer_write_reload(&ts[1], 0);
    timer_write_control(&ts[1], 0x81); /* enable, prescaler 64 */
    ASSERT_EQ_HEX(timer_read_counter(&ts[1], 130), 2);

    /* Wrap: counter near overflow reloads through the reload value. */
    timer_write_reload(&ts[0], 0xFFF0);
    timer_write_control(&ts[0], 0x80); /* enable, prescaler 1 */
    /* Period is 0x10000 - 0xFFF0 = 0x10 ticks.  0x18 ticks overflow
     * once and leave 8 more: reload + 8 = 0xFFF8. */
    ASSERT_EQ_HEX(timer_read_counter(&ts[0], 0x18), 0xFFF8);
}

TEST(timer_cycles_until_irq_finds_nearest_irq_timer) {
    Timer ts[4];
    timer_init(ts);
    ASSERT_EQ(timer_cycles_until_irq(ts), INT32_MAX);

    /* No IRQ enabled: not a slice boundary. */
    timer_write_reload(&ts[0], 0xFFF0);
    timer_write_control(&ts[0], 0x80);
    ASSERT_EQ(timer_cycles_until_irq(ts), INT32_MAX);

    /* Prescaler 64, 2 ticks left, 10 cycles into the current tick. */
    timer_write_reload(&ts[1], 0xFFFE);
    timer_write_control(&ts[1], 0xC1);
    ts[1].prescaler_counter = 10;
    ASSERT_EQ(timer_cycles_until_irq(ts), 2 * 64 - 10);
}

TEST(timer_enable_aligns_prescaler_to_global_clock) {
    /* A prescaled timer ticks at global times that are multiples of the
     * prescaler, not a full prescaler period after it was enabled. */
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    timer_tick(ts, 100, &ic, NULL);    /* global clock = 100 */
    timer_write_reload(&ts[0], 0);
    timer_write_control(&ts[0], 0x81); /* enable, prescaler 64 */

    timer_tick(ts, 27, &ic, NULL);     /* clock 127 */
    ASSERT_EQ(ts[0].counter, 0);
    timer_tick(ts, 1, &ic, NULL);      /* clock 128: first tick */
    ASSERT_EQ(ts[0].counter, 1);
    timer_tick(ts, 64, &ic, NULL);     /* clock 192 */
    ASSERT_EQ(ts[0].counter, 2);
}

TEST(timer_prescaler_change_realigns_and_drops_partial_tick) {
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    timer_write_reload(&ts[0], 0);
    timer_write_control(&ts[0], 0x81); /* prescaler 64 at clock 0 */
    timer_tick(ts, 100, &ic, NULL);    /* one tick at 64, 36 cycles spare */
    ASSERT_EQ(ts[0].counter, 1);

    /* Switch to 256 at clock 100: the next tick is at 256, not 256
     * cycles after the switch or 220 cycles counting the spare 36. */
    timer_write_control(&ts[0], 0x82);
    ASSERT_EQ(ts[0].counter, 1);
    timer_tick(ts, 155, &ic, NULL);    /* clock 255 */
    ASSERT_EQ(ts[0].counter, 1);
    timer_tick(ts, 1, &ic, NULL);      /* clock 256 */
    ASSERT_EQ(ts[0].counter, 2);

    /* Rewriting the same settings changes nothing. */
    timer_tick(ts, 10, &ic, NULL);
    timer_write_control(&ts[0], 0x82);
    timer_tick(ts, 246, &ic, NULL);    /* clock 512 */
    ASSERT_EQ(ts[0].counter, 3);
}

TEST(timer_read_before_last_sync_steps_counter_back) {
    Timer ts[4];
    InterruptController ic;
    timer_init(ts);
    interrupt_init(&ic);

    /* Prescaler 1: each cycle back is one tick back. */
    timer_write_reload(&ts[0], 0x1000);
    timer_write_control(&ts[0], 0x80);
    timer_tick(ts, 50, &ic, NULL);
    ASSERT_EQ_HEX(timer_read_counter(&ts[0], -2), 0x1030);
    ASSERT_EQ_HEX(timer_read_counter(&ts[0], -1), 0x1031);

    /* Prescaler 64: only a tick that landed inside the window counts. */
    timer_write_reload(&ts[1], 0);
    timer_write_control(&ts[1], 0x81); /* clock 50 */
    timer_tick(ts, 15, &ic, NULL);     /* clock 65: ticked at 64 */
    ASSERT_EQ(ts[1].counter, 1);
    ASSERT_EQ(timer_read_counter(&ts[1], -1), 1); /* time 64 */
    ASSERT_EQ(timer_read_counter(&ts[1], -2), 0); /* time 63 */
    timer_tick(ts, 2, &ic, NULL);      /* clock 67 */
    ASSERT_EQ(timer_read_counter(&ts[1], -2), 1);
}

TEST(timer_cycles_until_irq_predicts_cascade_overflow) {
    Timer ts[4];
    timer_init(ts);

    /* Timer 0 overflows every 0x100 cycles, first at 0x100. */
    timer_write_reload(&ts[0], 0xFF00);
    timer_write_control(&ts[0], 0x80);
    /* Count-up timer 1 with IRQ at 0xFFFE overflows on the 2nd one. */
    timer_write_reload(&ts[1], 0xFFFE);
    timer_write_control(&ts[1], 0xC4);
    ASSERT_EQ(timer_cycles_until_irq(ts), 0x200);

    /* Timer 2 counts timer 1, which overflows at 0x200 and then every
     * 0x200; at 0xFFFE timer 2 raises its IRQ at 0x400. */
    timer_write_control(&ts[1], 0x84); /* drop timer 1's IRQ */
    timer_write_reload(&ts[2], 0xFFFE);
    timer_write_control(&ts[2], 0xC4);
    ASSERT_EQ(timer_cycles_until_irq(ts), 0x400);

    /* A stopped lower timer never feeds the chain. */
    timer_write_control(&ts[0], 0x00);
    ASSERT_EQ(timer_cycles_until_irq(ts), INT32_MAX);
}

void run_timer_tests(void) {
    TEST_SUITE("timer");
    RUN_TEST(timer_init_zeros_state_and_sets_prescaler_to_one);
    RUN_TEST(timer_enable_reloads_counter_from_reload_value);
    RUN_TEST(timer_basic_tick_increments_counter);
    RUN_TEST(timer_overflow_fires_irq_when_enabled);
    RUN_TEST(timer_overflow_does_not_fire_irq_when_disabled);
    RUN_TEST(timer_cascade_increments_only_on_lower_overflow);
    RUN_TEST(timer_read_projects_unsynced_cycles);
    RUN_TEST(timer_read_projection_respects_prescaler_and_wrap);
    RUN_TEST(timer_cycles_until_irq_finds_nearest_irq_timer);
    RUN_TEST(timer_enable_aligns_prescaler_to_global_clock);
    RUN_TEST(timer_prescaler_change_realigns_and_drops_partial_tick);
    RUN_TEST(timer_read_before_last_sync_steps_counter_back);
    RUN_TEST(timer_cycles_until_irq_predicts_cascade_overflow);
}
