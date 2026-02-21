#include "interrupt.h"
#include "ppu/ppu.h"

void interrupt_init(InterruptController* ic) {
    ic->ime = false;
    ic->ie = 0;
    ic->irf = 0;
}

void interrupt_request(InterruptController* ic, uint16_t irq_bit) {
    ic->irf |= irq_bit;
}

void interrupt_request_if_enabled(InterruptController* ic, PPU* ppu, uint16_t irq_bit) {
    // Check DISPSTAT enable bits for PPU-related IRQs
    if (irq_bit == IRQ_VBLANK && !(ppu->dispstat & (1 << 3))) return;
    if (irq_bit == IRQ_HBLANK && !(ppu->dispstat & (1 << 4))) return;
    if (irq_bit == IRQ_VCOUNT && !(ppu->dispstat & (1 << 5))) return;

    ic->irf |= irq_bit;
}

void interrupt_acknowledge(InterruptController* ic, uint16_t val) {
    // Writing 1 bits to IF *clears* those bits
    ic->irf &= ~val;
}

bool interrupt_pending(InterruptController* ic) {
    return ic->ime && (ic->ie & ic->irf);
}
