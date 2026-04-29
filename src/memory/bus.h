#ifndef BUS_H
#define BUS_H

#include "common.h"

// Forward declarations
typedef struct PPU PPU;
typedef struct APU APU;
typedef struct DMAController DMAController;
typedef struct Timer Timer;
typedef struct InterruptController InterruptController;
typedef struct Cartridge Cartridge;
typedef struct InputState InputState;
typedef struct ARM7TDMI ARM7TDMI;
typedef struct SIO SIO;

// Memory region sizes
#define BIOS_SIZE 0x4000    // 16KB
#define EWRAM_SIZE 0x40000  // 256KB
#define IWRAM_SIZE 0x8000   // 32KB
#define IO_SIZE 0x400       // 1KB
#define PALETTE_SIZE 0x400  // 1KB
#define VRAM_SIZE 0x18000   // 96KB
#define OAM_SIZE 0x400      // 1KB

/* WAITCNT-derived per-region cycle counts (in CPU clocks).
 * sram_n is the SRAM non-sequential count (SRAM has no S timing — it's an
 * 8-bit bus, every access is N).  ws*_n / ws*_s are the ROM wait-state pair
 * for each of the three Game Pak mirror regions (0x08-0x09, 0x0A-0x0B,
 * 0x0C-0x0D). 32-bit ROM access is two halfword bus cycles, charged as N+S
 * (or S+S if sequential). */
typedef struct WaitState {
    uint8_t sram_n;
    uint8_t ws0_n;
    uint8_t ws0_s;
    uint8_t ws1_n;
    uint8_t ws1_s;
    uint8_t ws2_n;
    uint8_t ws2_s;
    /* Game Pak Prefetch Buffer enabled (WAITCNT bit 14). When set, sequential
     * ROM accesses pull from a small FIFO that the cart fills during cycles
     * the CPU isn't using the bus, costing 1 cycle instead of the S timing.
     * Non-sequential ROM access flushes the FIFO and pays full N. We model
     * the steady state — S becomes 1 — without simulating the FIFO depth or
     * fill rate, which is sufficient for our scanline-based timing. */
    bool prefetch_enabled;
    uint16_t raw;
} WaitState;

struct Bus {
    uint8_t bios[BIOS_SIZE];
    uint8_t ewram[EWRAM_SIZE];
    uint8_t iwram[IWRAM_SIZE];
    uint8_t io_regs[IO_SIZE];
    uint8_t palette_ram[PALETTE_SIZE];
    uint8_t vram[VRAM_SIZE];
    uint8_t oam[OAM_SIZE];

    // Open bus (last value read)
    uint32_t open_bus;

    // BIOS protection
    bool bios_readable;
    uint32_t last_bios_read;

    // WAITCNT-driven memory access timing.
    // pending_cycles accumulates wait cycles incurred by bus accesses; the
    // CPU (and DMA) drain it after each unit of work via bus_drain_pending().
    WaitState wait_state;
    int pending_cycles;
    uint32_t last_access_addr;
    uint8_t last_access_size;

    // Subsystem pointers (wired during gba_init)
    ARM7TDMI* cpu;
    PPU* ppu;
    APU* apu;
    DMAController* dma;
    Timer* timers;
    InterruptController* interrupts;
    Cartridge* cart;
    InputState* input;
    SIO* sio;
};
typedef struct Bus Bus;

void bus_init(Bus* bus);
bool bus_load_bios(Bus* bus, const char* path);

uint8_t bus_read8(Bus* bus, uint32_t addr);
uint16_t bus_read16(Bus* bus, uint32_t addr);
uint32_t bus_read32(Bus* bus, uint32_t addr);

void bus_write8(Bus* bus, uint32_t addr, uint8_t val);
void bus_write16(Bus* bus, uint32_t addr, uint16_t val);
void bus_write32(Bus* bus, uint32_t addr, uint32_t val);

/* Returns the wait cycles accumulated since the last call, then resets. */
int bus_drain_pending(Bus* bus);

/* Re-derive transient bus state (parsed WAITCNT, access tracking) from
 * io_regs after a savestate load — io_regs is serialized but the cached
 * decoded fields aren't, so they need rebuilding. */
void bus_post_load(Bus* bus);

#endif // BUS_H
