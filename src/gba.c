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

    LOG_INFO("GBA system initialized");
}

bool gba_load_rom(GBA* gba, const char* path) {
    return cartridge_load(&gba->cart, path);
}

bool gba_load_bios(GBA* gba, const char* path) {
    return bus_load_bios(&gba->bus, path);
}

void gba_run_frame(GBA* gba) {
    gba->frame_complete = false;

    for (int line = 0; line < TOTAL_LINES; line++) {
        // --- HDraw period (visible pixel rendering time) ---
        int hdraw_cycles = HDRAW_PIXELS * CYCLES_PER_PIXEL; // 960
        cpu_run(&gba->cpu, hdraw_cycles);
        timer_tick(gba->timers, hdraw_cycles, &gba->interrupts, &gba->apu);
        apu_tick(&gba->apu, hdraw_cycles);

        // --- HBlank ---
        ppu_set_hblank(&gba->ppu, true);

        if (line < VDRAW_LINES) {
            // Render this scanline
            ppu_render_scanline(&gba->ppu);

            // Trigger HBlank DMA
            dma_on_hblank(&gba->dma);
        }

        // Fire HBlank IRQ if enabled
        interrupt_request_if_enabled(&gba->interrupts, &gba->ppu, IRQ_HBLANK);

        int hblank_cycles = HBLANK_PIXELS * CYCLES_PER_PIXEL; // 272
        cpu_run(&gba->cpu, hblank_cycles);
        timer_tick(gba->timers, hblank_cycles, &gba->interrupts, &gba->apu);
        apu_tick(&gba->apu, hblank_cycles);

        // --- End of scanline ---
        ppu_set_hblank(&gba->ppu, false);
        ppu_increment_vcount(&gba->ppu);

        // Check VCount match
        if (ppu_vcount_match(&gba->ppu)) {
            interrupt_request_if_enabled(&gba->interrupts, &gba->ppu, IRQ_VCOUNT);
        }

        // VBlank start
        if (gba->ppu.vcount == VDRAW_LINES) {
            ppu_set_vblank(&gba->ppu, true);
            interrupt_request_if_enabled(&gba->interrupts, &gba->ppu, IRQ_VBLANK);
            dma_on_vblank(&gba->dma);

            // Reload affine reference points from latches at VBlank start.
            // Per GBATEK: the internal reference point registers are reloaded
            // from the latch values at the beginning of each VBlank period.
            gba->ppu.bg_ref_x[0] = gba->ppu.bg_ref_x_latch[0];
            gba->ppu.bg_ref_y[0] = gba->ppu.bg_ref_y_latch[0];
            gba->ppu.bg_ref_x[1] = gba->ppu.bg_ref_x_latch[1];
            gba->ppu.bg_ref_y[1] = gba->ppu.bg_ref_y_latch[1];

            gba->frame_complete = true;
        }

        // VBlank end (wrap around)
        if (gba->ppu.vcount == 0) {
            ppu_set_vblank(&gba->ppu, false);
        }

        gba->total_cycles += SCANLINE_CYCLES;
    }
}

void gba_destroy(GBA* gba) {
    cartridge_destroy(&gba->cart);
    LOG_INFO("GBA system destroyed");
}
