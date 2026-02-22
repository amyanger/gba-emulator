#ifndef GBA_H
#define GBA_H

#include "common.h"
#include "cpu/arm7tdmi.h"
#include "memory/bus.h"
#include "memory/dma.h"
#include "ppu/ppu.h"
#include "apu/apu.h"
#include "timer/timer.h"
#include "interrupt/interrupt.h"
#include "cartridge/cartridge.h"
#include "input/input.h"

#ifdef ENABLE_XRAY
typedef struct XRayState XRayState;
#endif

struct GBA {
    ARM7TDMI cpu;
    Bus bus;
    PPU ppu;
    APU apu;
    Timer timers[4];
    DMAController dma;
    InterruptController interrupts;
    Cartridge cart;
    InputState input;

    uint64_t total_cycles;
    bool frame_complete;
    bool running;

#ifdef ENABLE_XRAY
    XRayState* xray;
#endif
};
typedef struct GBA GBA;

// Initialize the GBA system
void gba_init(GBA* gba);

// Load a ROM file, returns false on failure
bool gba_load_rom(GBA* gba, const char* path);

// Load BIOS file (optional), returns false on failure
bool gba_load_bios(GBA* gba, const char* path);

// Run one full frame (280,896 cycles)
void gba_run_frame(GBA* gba);

// Clean up resources
void gba_destroy(GBA* gba);

#endif // GBA_H
