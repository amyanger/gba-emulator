#ifndef TIMER_H
#define TIMER_H

#include "common.h"

// Forward declarations
typedef struct InterruptController InterruptController;
typedef struct APU APU;

// IRQ bit positions for timers
#define IRQ_TIMER0 (1 << 3)
#define IRQ_TIMER1 (1 << 4)
#define IRQ_TIMER2 (1 << 5)
#define IRQ_TIMER3 (1 << 6)

struct Timer {
    uint16_t counter;
    uint16_t reload;
    uint16_t control;

    // Decoded control fields
    uint16_t prescaler;   // 1, 64, 256, 1024
    bool cascade;
    bool irq_enable;
    bool enabled;

    // Internal
    uint32_t prescaler_counter;  // cycles since the last prescaler tick

    // Global cycle count (wraps), the same in all four timers and advanced
    // by timer_tick. Prescaled timers tick when it reaches a multiple of
    // the prescaler. Not saved: after a load, running timers keep their
    // phase via prescaler_counter and only new starts may drift slightly.
    uint32_t clock;
};
typedef struct Timer Timer;

void timer_init(Timer timers[4]);
void timer_tick(Timer timers[4], int cycles, InterruptController* interrupts, APU* apu);
void timer_write_reload(Timer* timer, uint16_t val);
void timer_write_control(Timer* timer, uint16_t val);
uint16_t timer_read_counter(const Timer* timer, int32_t elapsed_cycles);
int32_t timer_cycles_until_irq(const Timer timers[4]);

#endif // TIMER_H
