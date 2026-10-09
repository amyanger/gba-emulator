#include "interrupt.h"
#include "ppu/ppu.h"

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
#endif

void interrupt_init(InterruptController* ic) {
    ic->ime = false;
    ic->ie = 0;
    ic->irf = 0;
    ic->check_pending = false;
    ic->check_delay = 0;
}

/* Raise an IRQ that happened 'late' cycles before the current point
 * (mGBA's GBARaiseIRQ with cyclesLate). */
void interrupt_request_late(InterruptController* ic, uint16_t irq_bit, int32_t late) {
    ic->irf |= irq_bit;
#ifdef ENABLE_XRAY
    xray_notify_irq(g_xray, irq_bit);
#endif
    interrupt_test(ic, IRQ_DISPATCH_DELAY - late);
}

void interrupt_request(InterruptController* ic, uint16_t irq_bit) {
    interrupt_request_late(ic, irq_bit, 0);
}

void interrupt_request_if_enabled(InterruptController* ic, PPU* ppu, uint16_t irq_bit,
                                  int32_t late) {
    // Check DISPSTAT enable bits for PPU-related IRQs
    if (irq_bit == IRQ_VBLANK && !(ppu->dispstat & (1 << 3))) return;
    if (irq_bit == IRQ_HBLANK && !(ppu->dispstat & (1 << 4))) return;
    if (irq_bit == IRQ_VCOUNT && !(ppu->dispstat & (1 << 5))) return;

    interrupt_request_late(ic, irq_bit, late);
}

/* mGBA's GBATestIRQ: if IE & IF, schedule a dispatch check 'delay'
 * cycles out unless one is already pending (the earliest one wins). */
void interrupt_test(InterruptController* ic, int32_t delay) {
    if ((ic->ie & ic->irf) && !ic->check_pending) {
        ic->check_pending = true;
        ic->check_delay = delay;
    }
}

void interrupt_elapse(InterruptController* ic, int32_t cycles) {
    if (ic->check_pending) {
        ic->check_delay -= cycles;
    }
}

/* Consume the pending check if its time has come. */
bool interrupt_check_due(InterruptController* ic) {
    if (!ic->check_pending || ic->check_delay > 0) return false;
    ic->check_pending = false;
    return true;
}

void interrupt_acknowledge(InterruptController* ic, uint16_t val) {
    // Writing 1 bits to IF *clears* those bits
    ic->irf &= ~val;
}

bool interrupt_pending(InterruptController* ic) {
    return ic->ime && (ic->ie & ic->irf);
}

/* HALT's condition per GBATEK: IE & IF, ignoring IME (and CPSR.I). */
bool interrupt_pending_raw(InterruptController* ic) {
    return (ic->ie & ic->irf) != 0;
}
