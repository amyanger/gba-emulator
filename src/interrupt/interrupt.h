#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "common.h"

// Forward declaration
typedef struct PPU PPU;

/* Cycles from an IRQ being raised to the CPU taking it (mGBA's
 * GBA_IRQ_DELAY). IF is visible at once; dispatch and HALT wake wait. */
#define IRQ_DISPATCH_DELAY 7

struct InterruptController {
    bool ime;       // Interrupt Master Enable
    uint16_t ie;    // Interrupt Enable
    uint16_t irf;   // Interrupt Request Flags

    // Delayed dispatch check (mGBA's irqEvent). check_delay counts down
    // from the start of the CPU instruction in flight (or from the
    // current boundary between instructions); cpu_run takes the check at
    // the first boundary where it is <= 0. Not saved: a loaded state
    // re-arms it with no delay.
    bool check_pending;
    int32_t check_delay;
};
typedef struct InterruptController InterruptController;

void interrupt_init(InterruptController* ic);
void interrupt_request(InterruptController* ic, uint16_t irq_bit);
void interrupt_request_late(InterruptController* ic, uint16_t irq_bit, int32_t late);
void interrupt_request_if_enabled(InterruptController* ic, PPU* ppu, uint16_t irq_bit,
                                  int32_t late);
void interrupt_test(InterruptController* ic, int32_t delay);
void interrupt_elapse(InterruptController* ic, int32_t cycles);
bool interrupt_check_due(InterruptController* ic);
void interrupt_acknowledge(InterruptController* ic, uint16_t val);
bool interrupt_pending(InterruptController* ic);
bool interrupt_pending_raw(InterruptController* ic);

#endif // INTERRUPT_H
