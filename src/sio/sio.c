#include "sio.h"
#include "interrupt/interrupt.h"
#include "memory/io_regs.h"
#include <string.h>

// Approximate Multiplayer transfer length in CPU cycles. Real hardware varies
// with the baud-rate select bits (~2-9 KHz packet rate); 1024 cycles is short
// enough to complete inside one scanline, which is all the game logic needs.
#define SIO_MP_TRANSFER_CYCLES 1024

static void sio_recompute_mode(SIO* sio) {
    // Per GBATEK: RCNT bits 14-15 == 00 means "serial mode" controlled by SIOCNT.
    // RCNT 14-15 = 01/10/11 selects GP/JOYBUS/etc which we don't implement.
    sio->serial_mode_enabled = ((sio->rcnt >> 14) == 0);

    if (!sio->serial_mode_enabled) {
        sio->mode = SIO_MODE_NORMAL_8;
        return;
    }
    uint16_t mode_bits = (sio->siocnt >> SIOCNT_MODE_SHIFT) & 0x3;
    sio->mode = (SioMode)mode_bits;
}

// Order-independent transfer arming. Real games write SIOCNT as a 16-bit
// halfword which the bus splits into two byte writes (low then high). We
// can't rely on either byte ordering, so after every relevant register write
// we re-check: if Multiplayer + START + not-already-active, arm the transfer.
// `transfer_active` guards against re-arming on subsequent writes; sio_tick
// clears START when the transfer completes, so a fresh write can arm again.
static void sio_maybe_arm_transfer(SIO* sio) {
    if (sio->transfer_active) return;
    if (sio->mode != SIO_MODE_MULTIPLAYER) return;
    if (!(sio->siocnt & SIOCNT_START)) return;
    sio->transfer_active = true;
    sio->transfer_cycles_remaining = SIO_MP_TRANSFER_CYCLES;
}

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
        sio_maybe_arm_transfer(sio);
        return;
    case 0x129:
        sio->siocnt = (sio->siocnt & 0x00FF) | ((uint16_t)val << 8);
        sio_recompute_mode(sio);
        sio_maybe_arm_transfer(sio);
        return;
    case 0x12A: sio->siomlt_send = (sio->siomlt_send & 0xFF00) | val; break;
    case 0x12B: sio->siomlt_send = (sio->siomlt_send & 0x00FF) | ((uint16_t)val << 8); break;
    case 0x134: sio->rcnt = (sio->rcnt & 0xFF00) | val; break;
    case 0x135:
        sio->rcnt = (sio->rcnt & 0x00FF) | ((uint16_t)val << 8);
        sio_recompute_mode(sio);
        sio_maybe_arm_transfer(sio);
        return;
    default: break;
    }
}

void sio_tick(SIO* sio, int cycles) {
    if (!sio->transfer_active) return;

    sio->transfer_cycles_remaining -= cycles;
    if (sio->transfer_cycles_remaining > 0) return;

    // Transfer complete. Slot 0 always reflects our own send. With no peer
    // (link cable disconnected), slots 1-3 read 0xFFFF per GBATEK.
    sio->siomulti[0] = sio->siomlt_send;
    sio->siomulti[1] = 0xFFFF;
    sio->siomulti[2] = 0xFFFF;
    sio->siomulti[3] = 0xFFFF;
    // Note: when a LinkPeer is present, link_peer_exchange will populate
    // siomulti[1..3] — wired up in Task 9.

    sio->siocnt &= ~SIOCNT_START;
    sio->transfer_active = false;

    if (sio->siocnt & SIOCNT_IRQ_ENABLE) {
        interrupt_request(sio->interrupts, IRQ_SERIAL);
    }
}
