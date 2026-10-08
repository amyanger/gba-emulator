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

/* WAITCNT-derived per-region total access cycles (1 + waitstates, in CPU
 * clocks).
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
    /* Game Pak Prefetch Buffer enabled (WAITCNT bit 14). Fetch costs don't
     * change; instead bus_prefetch_stall refunds the ROM halfwords the
     * buffer could fill while an instruction is busy off the cart bus. */
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

    // Cycles of the current CPU chunk already applied to the timers by a
    // mid-chunk register write. gba_run_cycles ticks the rest and resets it,
    // so it is always 0 between chunks (and never needs saving).
    int timer_synced_cycles;

    // Prefetch bookkeeping for the instruction in flight: total cycles of
    // its CPU data accesses, whether any was a read (adds the load's
    // I cycle), and the last data address. Opcode fetches (fetching=true)
    // and DMA are excluded. last_prefetched_pc marks how far the buffer
    // has already been credited so one window isn't refunded twice; a
    // pipeline flush resets it to 0. None of this survives a chunk
    // boundary in a meaningful way, so it is not saved.
    bool fetching;
    bool data_read;
    int data_wait;
    uint32_t data_addr;
    uint32_t last_prefetched_pc;

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

/* Opcode fetches: charged like reads but not counted as data accesses. */
uint16_t bus_fetch16(Bus* bus, uint32_t addr);
uint32_t bus_fetch32(Bus* bus, uint32_t addr);

/* Apply the prefetch buffer's refund for the instruction just executed
 * (pc = the pipeline PC it ran with, thumb = its mode), then clear the per-instruction
 * data-access bookkeeping. */
void bus_prefetch_stall(Bus* bus, uint32_t pc, bool thumb);

/* Re-derive transient bus state (parsed WAITCNT, access tracking) from
 * io_regs after a savestate load — io_regs is serialized but the cached
 * decoded fields aren't, so they need rebuilding. */
void bus_post_load(Bus* bus);

// Raise IRQ_KEYPAD if KEYCNT's condition holds for the current KEYINPUT.
// Level-sampled: called on KEYCNT writes and once per frame from gba.c.
void bus_check_keypad_irq(Bus* bus);

#endif // BUS_H
