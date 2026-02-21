#include "timer.h"
#include "interrupt/interrupt.h"

static const uint16_t prescaler_values[] = {1, 64, 256, 1024};

void timer_init(Timer timers[4]) {
    memset(timers, 0, sizeof(Timer) * 4);
    for (int i = 0; i < 4; i++) {
        timers[i].prescaler = 1;
    }
}

void timer_write_reload(Timer* timer, uint16_t val) {
    timer->reload = val;
}

void timer_write_control(Timer* timer, uint16_t val) {
    bool was_enabled = timer->enabled;

    timer->control = val;
    timer->prescaler = prescaler_values[val & 3];
    timer->cascade = BIT(val, 2);
    timer->irq_enable = BIT(val, 6);
    timer->enabled = BIT(val, 7);

    // On rising edge of enable, reload counter
    if (!was_enabled && timer->enabled) {
        timer->counter = timer->reload;
        timer->prescaler_counter = 0;
    }
}

uint16_t timer_read_counter(Timer* timer) {
    return timer->counter;
}

void timer_tick(Timer timers[4], int cycles, InterruptController* interrupts) {
    static const uint16_t timer_irq_bits[] = {IRQ_TIMER0, IRQ_TIMER1, IRQ_TIMER2, IRQ_TIMER3};

    for (int i = 0; i < 4; i++) {
        Timer* t = &timers[i];
        if (!t->enabled || t->cascade) continue;

        t->prescaler_counter += cycles;

        while (t->prescaler_counter >= t->prescaler) {
            t->prescaler_counter -= t->prescaler;
            t->counter++;

            if (t->counter == 0) {
                // Overflow
                t->counter = t->reload;

                if (t->irq_enable) {
                    interrupt_request(interrupts, timer_irq_bits[i]);
                }

                // TODO: Trigger FIFO playback if timer 0 or 1
                // TODO: Cascade to next timer if enabled
                if (i < 3 && timers[i + 1].enabled && timers[i + 1].cascade) {
                    timers[i + 1].counter++;
                    if (timers[i + 1].counter == 0) {
                        timers[i + 1].counter = timers[i + 1].reload;
                        if (timers[i + 1].irq_enable) {
                            interrupt_request(interrupts, timer_irq_bits[i + 1]);
                        }
                    }
                }
            }
        }
    }
}
