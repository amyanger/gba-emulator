#include "gba.h"

void gba_init(GBA* gba) {
    memset(gba, 0, sizeof(GBA));

    // Wire up subsystem cross-references
    gba->cpu.bus = &gba->bus;
    gba->bus.cpu = &gba->cpu;
    gba->bus.ppu = &gba->ppu;
    gba->bus.apu = &gba->apu;
    gba->bus.dma = &gba->dma;
    gba->bus.timers = gba->timers;
    gba->bus.interrupts = &gba->interrupts;
    gba->bus.cart = &gba->cart;
    gba->bus.input = &gba->input;
    gba->bus.sio = &gba->sio;

    // Initialize all subsystems (must happen before wiring pointers that
    // init functions would otherwise zero via memset)
    cpu_init(&gba->cpu);
    bus_init(&gba->bus);
    ppu_init(&gba->ppu);
    apu_init(&gba->apu);
    timer_init(gba->timers);
    dma_init(&gba->dma);
    interrupt_init(&gba->interrupts);
    input_init(&gba->input);
    cheat_init(&gba->cheats);
    sio_init(&gba->sio, &gba->interrupts, NULL);

    // PPU gets pointers to VRAM/palette/OAM in bus — AFTER ppu_init()
    // so the memset inside ppu_init doesn't wipe them
    gba->ppu.palette_ram = gba->bus.palette_ram;
    gba->ppu.vram = gba->bus.vram;
    gba->ppu.oam = gba->bus.oam;

    // DMA needs bus (for memory transfers) and interrupts (for completion IRQs)
    gba->dma.bus = &gba->bus;
    gba->dma.interrupts = &gba->interrupts;

    // APU needs DMA controller for FIFO refill triggering
    gba->apu.dma = &gba->dma;

    gba->running = true;
    gba->frame_complete = false;

#ifdef ENABLE_REWIND
    /* 60-second window at 30 snapshots/sec = 1800 slots, capped by a byte
     * budget (the window shortens if snapshots run large). Failure is
     * non-fatal — rewind_init logs a warning and the APIs become no-ops. */
    rewind_init(&gba->rewind, 1800, REWIND_DEFAULT_MAX_BYTES);
#endif

    LOG_INFO("GBA system initialized");
}

bool gba_load_rom(GBA* gba, const char* path) {
    return cartridge_load(&gba->cart, path);
}

bool gba_load_bios(GBA* gba, const char* path) {
    return bus_load_bios(&gba->bus, path);
}

void gba_run_cycles(GBA* gba, int cycles) {
    cpu_run(&gba->cpu, cycles);
    timer_tick(gba->timers, cycles, &gba->interrupts, &gba->apu);
    apu_tick(&gba->apu, cycles);
    sio_tick(&gba->sio, cycles);
    // Timers are now synced; clear the CPU's chunk progress so timer
    // reads don't project these cycles a second time.
    gba->cpu.cycles_executed = 0;
}

void gba_run_scanline(GBA* gba) {
    /* Three-chunk scanline (per GBATEK):
     *   1. HDraw to cycle 1006     (1006 cycles)
     *   2. HBlank flag set + edge events  (instant)
     *   3. HBlank tail              (226 cycles)
     *   4. End-of-line edge events  (instant)
     */
    gba_run_cycles(gba, HBLANK_FLAG_SET_CYCLE);  // 1006

    ppu_set_hblank(&gba->ppu, true);
    if (gba->ppu.vcount < VDRAW_LINES) {
        ppu_render_scanline(&gba->ppu);
        /* DMA fires before IRQ — order matters; do not swap. */
        dma_on_hblank(&gba->dma);
    }
    interrupt_request_if_enabled(&gba->interrupts, &gba->ppu, IRQ_HBLANK);

    gba_run_cycles(gba, HBLANK_TAIL_CYCLES);  // 226

    ppu_set_hblank(&gba->ppu, false);
    ppu_increment_vcount(&gba->ppu);

    if (ppu_vcount_match(&gba->ppu)) {
        interrupt_request_if_enabled(&gba->interrupts, &gba->ppu, IRQ_VCOUNT);
    }

    if (gba->ppu.vcount == VDRAW_LINES) {
        ppu_set_vblank(&gba->ppu, true);
        interrupt_request_if_enabled(&gba->interrupts, &gba->ppu, IRQ_VBLANK);
        dma_on_vblank(&gba->dma);
        cheat_apply(&gba->cheats, &gba->bus);
        gba->ppu.bg_ref_x[0] = gba->ppu.bg_ref_x_latch[0];
        gba->ppu.bg_ref_y[0] = gba->ppu.bg_ref_y_latch[0];
        gba->ppu.bg_ref_x[1] = gba->ppu.bg_ref_x_latch[1];
        gba->ppu.bg_ref_y[1] = gba->ppu.bg_ref_y_latch[1];
        gba->frame_complete = true;
    }

    if (gba->ppu.vcount == 227) {
        ppu_set_vblank(&gba->ppu, false);
    }

    gba->total_cycles += SCANLINE_CYCLES;
}

void gba_run_frame(GBA* gba) {
    gba->frame_complete = false;

    for (int line = 0; line < TOTAL_LINES; line++) {
        gba_run_scanline(gba);
    }

#ifdef ENABLE_REWIND
    /* Record snapshot at end-of-frame, after all scanlines (including VBlank)
     * have completed and the GBA struct represents a consistent state. */
    rewind_record_frame(&gba->rewind, gba);
#endif
}

void gba_set_link_peer(GBA* gba, LinkPeer* peer) {
    gba->sio.peer = peer;
}

void gba_destroy(GBA* gba) {
    cartridge_destroy(&gba->cart);
#ifdef ENABLE_REWIND
    rewind_shutdown(&gba->rewind);
#endif
    LOG_INFO("GBA system destroyed");
}
