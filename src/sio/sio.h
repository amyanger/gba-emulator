#ifndef SIO_H
#define SIO_H

#include "common.h"

// Forward declarations
typedef struct InterruptController InterruptController;
typedef struct LinkPeer LinkPeer;

// IRQ bit position for serial communication
#define IRQ_SERIAL (1 << 7)

// SIOCNT bit positions (Multiplayer mode — per GBATEK)
#define SIOCNT_BAUD_MASK     0x0003   // bits 0-1
#define SIOCNT_SI            (1 << 2) // input from peer
#define SIOCNT_SD            (1 << 3) // output to peer
#define SIOCNT_ID_MASK       0x0030   // bits 4-5 (read-only multi-player ID)
#define SIOCNT_ID_SHIFT      4
#define SIOCNT_ERROR         (1 << 6)
#define SIOCNT_START         (1 << 7) // write to start; read for busy
#define SIOCNT_MODE_MASK     0x3000   // bits 12-13: 10 = multiplayer
#define SIOCNT_MODE_SHIFT    12
#define SIOCNT_IRQ_ENABLE    (1 << 14)
// Master/slave is decided by the LinkPeer connection topology.

typedef enum {
    SIO_MODE_NORMAL_8 = 0,
    SIO_MODE_NORMAL_32 = 1,
    SIO_MODE_MULTIPLAYER = 2,
    SIO_MODE_UART = 3,
} SioMode;

struct SIO {
    // Registers
    uint16_t siomulti[4];   // 0x120, 0x122, 0x124, 0x126
    uint16_t siocnt;         // 0x128
    uint16_t siomlt_send;    // 0x12A
    uint16_t rcnt;           // 0x134
    uint32_t siodata32;      // Normal mode 32-bit (unused in v1)

    // Decoded mode (refreshed on writes to SIOCNT or RCNT)
    SioMode mode;
    bool serial_mode_enabled; // RCNT[15]=0 AND RCNT[14]=0 => serial mode active

    // In-flight transfer state
    bool transfer_active;
    int32_t transfer_cycles_remaining;

    // Wired by sio_init
    InterruptController* interrupts;
    LinkPeer* peer; // may be NULL (no peer connected)
};
typedef struct SIO SIO;

void sio_init(SIO* sio, InterruptController* interrupts, LinkPeer* peer);

// Bus dispatch
uint8_t sio_read8(SIO* sio, uint32_t offset);   // offset is bus->io_regs offset (e.g. 0x120)
void sio_write8(SIO* sio, uint32_t offset, uint8_t val);

// Advance transfer state machine. Called from gba.c per scanline.
void sio_tick(SIO* sio, int cycles);

#endif // SIO_H
