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
    switch (offset) {
    case 0x120: return (uint8_t)sio->siomulti[0];
    case 0x121: return (uint8_t)(sio->siomulti[0] >> 8);
    case 0x122: return (uint8_t)sio->siomulti[1];
    case 0x123: return (uint8_t)(sio->siomulti[1] >> 8);
    case 0x124: return (uint8_t)sio->siomulti[2];
    case 0x125: return (uint8_t)(sio->siomulti[2] >> 8);
    case 0x126: return (uint8_t)sio->siomulti[3];
    case 0x127: return (uint8_t)(sio->siomulti[3] >> 8);
    case 0x128: return (uint8_t)sio->siocnt;
    case 0x129: return (uint8_t)(sio->siocnt >> 8);
    case 0x12A: return (uint8_t)sio->siomlt_send;
    case 0x12B: return (uint8_t)(sio->siomlt_send >> 8);
    case 0x134: return (uint8_t)sio->rcnt;
    case 0x135: return (uint8_t)(sio->rcnt >> 8);
    default:    return 0;
    }
}

void sio_write8(SIO* sio, uint32_t offset, uint8_t val) {
    switch (offset) {
    case 0x120: sio->siomulti[0] = (sio->siomulti[0] & 0xFF00) | val; break;
    case 0x121: sio->siomulti[0] = (sio->siomulti[0] & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x122: sio->siomulti[1] = (sio->siomulti[1] & 0xFF00) | val; break;
    case 0x123: sio->siomulti[1] = (sio->siomulti[1] & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x124: sio->siomulti[2] = (sio->siomulti[2] & 0xFF00) | val; break;
    case 0x125: sio->siomulti[2] = (sio->siomulti[2] & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x126: sio->siomulti[3] = (sio->siomulti[3] & 0xFF00) | val; break;
    case 0x127: sio->siomulti[3] = (sio->siomulti[3] & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x128:
        sio->siocnt = (sio->siocnt & 0xFF00) | val;
        break;
    case 0x129:
        sio->siocnt = (sio->siocnt & 0x00FF) | ((uint16_t)val << 8);
        break;
    case 0x12A: sio->siomlt_send = (sio->siomlt_send & 0xFF00) | val; break;
    case 0x12B: sio->siomlt_send = (sio->siomlt_send & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x134: sio->rcnt = (sio->rcnt & 0xFF00) | val; break;
    case 0x135: sio->rcnt = (sio->rcnt & 0x00FF) | ((uint16_t)val << 8); break;
    default: break;
    }
}

void sio_tick(SIO* sio, int cycles) {
    (void)sio; (void)cycles;
}
