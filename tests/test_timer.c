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

void run_timer_tests(void) {
    TEST_SUITE("timer");
    RUN_TEST(timer_init_zeros_state_and_sets_prescaler_to_one);
    RUN_TEST(timer_enable_reloads_counter_from_reload_value);
    RUN_TEST(timer_basic_tick_increments_counter);
    RUN_TEST(timer_overflow_fires_irq_when_enabled);
    RUN_TEST(timer_overflow_does_not_fire_irq_when_disabled);
    RUN_TEST(timer_cascade_increments_only_on_lower_overflow);
}
