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
#include "sio/sio.h"
#include "cheat/cheat.h"

#ifdef ENABLE_REWIND
#include "rewind/rewind.h"
#endif

#ifdef ENABLE_XRAY
typedef struct XRayState XRayState;
#endif

typedef struct LinkPeer LinkPeer;

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
    SIO sio;
    CheatEngine cheats;
#ifdef ENABLE_REWIND
    RewindBuffer rewind;
#endif

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

// Advance the system by an arbitrary number of CPU cycles. Ticks CPU,
// timers, APU, and SIO uniformly. Does NOT fire scanline edge events
// (HBlank/VBlank flag changes, IRQs, DMAs, vcount changes) — those
// remain the responsibility of gba_run_frame's per-line state machine.
//
// Public so test code can drive sub-scanline state directly.
void gba_run_cycles(GBA* gba, int cycles);

// Advance the system by one full scanline (1232 cycles), running
// the orchestrator's per-line state machine: HDraw cycles, HBlank
// edge events (flag set, render-if-visible, HBlank DMA, HBlank
// IRQ), HBlank-tail cycles, end-of-line edges (flag clear,
// vcount++, VCount match, VBlank set/clear, frame_complete).
//
// Equivalent to one iteration of the loop inside gba_run_frame.
// Public so tests can drive the orchestrator at scanline
// granularity instead of full-frame granularity.
void gba_run_scanline(GBA* gba);

// Attach (or detach with NULL) a LinkPeer transport for SIO multiplayer.
// Used by the CLI to wire up --link-master / --link-client at startup.
void gba_set_link_peer(GBA* gba, LinkPeer* peer);

// Clean up resources
void gba_destroy(GBA* gba);

#endif // GBA_H
