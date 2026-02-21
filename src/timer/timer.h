#ifndef TIMER_H
#define TIMER_H

#include "common.h"

// Forward declaration
typedef struct InterruptController InterruptController;

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
    uint32_t prescaler_counter;
};
typedef struct Timer Timer;

void timer_init(Timer timers[4]);
void timer_tick(Timer timers[4], int cycles, InterruptController* interrupts);
void timer_write_reload(Timer* timer, uint16_t val);
void timer_write_control(Timer* timer, uint16_t val);
uint16_t timer_read_counter(Timer* timer);

#endif // TIMER_H
