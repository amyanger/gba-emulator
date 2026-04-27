#include "sio.h"
#include "interrupt/interrupt.h"
#include "memory/io_regs.h"
#include <string.h>

void sio_init(SIO* sio, InterruptController* interrupts, LinkPeer* peer) {
    memset(sio, 0, sizeof(SIO));
    sio->interrupts = interrupts;
    sio->peer = peer;
    sio->mode = SIO_MODE_NORMAL_8;
}

uint8_t sio_read8(SIO* sio, uint32_t offset) {
    (void)sio; (void)offset;
    return 0;
}

void sio_write8(SIO* sio, uint32_t offset, uint8_t val) {
    (void)sio; (void)offset; (void)val;
}

void sio_tick(SIO* sio, int cycles) {
    (void)sio; (void)cycles;
}
