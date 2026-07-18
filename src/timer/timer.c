#include "timer.h"
#include "interrupt/interrupt.h"
#include "apu/apu.h"

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
#endif

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

/* Read the counter as of 'elapsed_cycles' past the last timer_tick
 * sync.  Timers advance in scanline chunks after the CPU runs, so a
 * mid-chunk read projects forward WITHOUT mutating timer state (the
 * pending cycles will be applied by the upcoming timer_tick).
 * Cascade timers are not projected — their advance depends on the lower
 * timer's overflows, and they move too slowly for the lag to matter. */
uint16_t timer_read_counter(Timer* timer, uint32_t elapsed_cycles) {
    if (!timer->enabled || timer->cascade || elapsed_cycles == 0) {
        return timer->counter;
    }

    uint32_t ticks = (timer->prescaler_counter + elapsed_cycles) / timer->prescaler;
    uint32_t projected = (uint32_t)timer->counter + ticks;
    if (projected > 0xFFFF) {
        uint32_t period = 0x10000u - timer->reload;
        projected = timer->reload + (projected - 0x10000u) % period;
    }
    return (uint16_t)projected;
}

void timer_tick(Timer timers[4], int cycles, InterruptController* interrupts, APU* apu) {
    static const uint16_t timer_irq_bits[] = {IRQ_TIMER0, IRQ_TIMER1, IRQ_TIMER2, IRQ_TIMER3};

    for (int i = 0; i < 4; i++) {
        Timer* t = &timers[i];
        if (!t->enabled || t->cascade) continue;

        t->prescaler_counter += cycles;

        while (t->prescaler_counter >= t->prescaler) {
            t->prescaler_counter -= t->prescaler;
            t->counter++;

            if (t->counter == 0) {
                // Overflow: reload and fire IRQ/audio callbacks
                t->counter = t->reload;
#ifdef ENABLE_XRAY
                xray_notify_timer_overflow(g_xray, i);
#endif

                if (t->irq_enable) {
                    interrupt_request(interrupts, timer_irq_bits[i]);
                }

                if (apu) {
                    apu_on_timer_overflow(apu, i);
                }

                // Cascade forward through the chain: if timer N overflows
                // and timer N+1 is a cascade timer, increment it. If N+1
                // also overflows, continue to N+2, and so on up to timer 3.
                int next = i + 1;
                while (next < 4 && timers[next].enabled && timers[next].cascade) {
                    timers[next].counter++;
                    if (timers[next].counter == 0) {
                        // Cascaded timer overflowed
                        timers[next].counter = timers[next].reload;
#ifdef ENABLE_XRAY
                        xray_notify_timer_overflow(g_xray, next);
#endif
                        if (timers[next].irq_enable) {
                            interrupt_request(interrupts, timer_irq_bits[next]);
                        }
                        if (apu) {
                            apu_on_timer_overflow(apu, next);
                        }
                        next++;
                    } else {
                        break; // No overflow, cascade chain stops
                    }
                }
            }
        }
    }
}
