#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "common.h"

// Forward declaration
typedef struct PPU PPU;

struct InterruptController {
    bool ime;       // Interrupt Master Enable
    uint16_t ie;    // Interrupt Enable
    uint16_t irf;   // Interrupt Request Flags
};
typedef struct InterruptController InterruptController;

void interrupt_init(InterruptController* ic);
void interrupt_request(InterruptController* ic, uint16_t irq_bit);
void interrupt_request_if_enabled(InterruptController* ic, PPU* ppu, uint16_t irq_bit);
void interrupt_acknowledge(InterruptController* ic, uint16_t val);
bool interrupt_pending(InterruptController* ic);

#endif // INTERRUPT_H
