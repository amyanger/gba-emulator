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

// Memory region sizes
#define BIOS_SIZE 0x4000    // 16KB
#define EWRAM_SIZE 0x40000  // 256KB
#define IWRAM_SIZE 0x8000   // 32KB
#define IO_SIZE 0x400       // 1KB
#define PALETTE_SIZE 0x400  // 1KB
#define VRAM_SIZE 0x18000   // 96KB
#define OAM_SIZE 0x400      // 1KB

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

    // Subsystem pointers (wired during gba_init)
    ARM7TDMI* cpu;
    PPU* ppu;
    APU* apu;
    DMAController* dma;
    Timer* timers;
    InterruptController* interrupts;
    Cartridge* cart;
    InputState* input;
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

#endif // BUS_H
