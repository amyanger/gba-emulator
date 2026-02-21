#include "gba.h"
#include "cpu/arm7tdmi.h"
#include "frontend/frontend.h"
#include <stdio.h>

static void print_usage(const char* prog) {
    printf("Usage: %s <rom.gba> [options]\n", prog);
    printf("Options:\n");
    printf("  --bios <file>   Load GBA BIOS ROM\n");
    printf("  --scale <n>     Window scale factor (default: 3)\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* rom_path = argv[1];
    const char* bios_path = NULL;
    int scale = 3;

    // Parse arguments
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
            bios_path = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        }
    }

    // Initialize GBA
    GBA gba;
    gba_init(&gba);

    if (bios_path) {
        if (!gba_load_bios(&gba, bios_path)) {
            LOG_WARN("Failed to load BIOS, continuing without it");
            cpu_skip_bios(&gba.cpu);
        }
    } else {
        // No BIOS provided — set CPU to post-BIOS state so execution
        // starts at the ROM entry point with correct stack pointers.
        cpu_skip_bios(&gba.cpu);
    }

    if (!gba_load_rom(&gba, rom_path)) {
        LOG_ERROR("Failed to load ROM: %s", rom_path);
        return 1;
    }

    // Initialize frontend (SDL2)
    Frontend fe;
    if (!frontend_init(&fe, scale)) {
        LOG_ERROR("Failed to initialize frontend");
        gba_destroy(&gba);
        return 1;
    }

    // Initialize audio
    frontend_audio_init(&fe);

    LOG_INFO("Starting emulation...");

    // Main loop
    while (fe.running && gba.running) {
        frontend_poll_input(&fe, &gba);
        gba_run_frame(&gba);

        if (gba.frame_complete) {
            frontend_present_frame(&fe, gba.ppu.framebuffer);
            frontend_push_audio(&fe, &gba.apu);
            frontend_frame_sync(&fe);
        }
    }

    // Cleanup
    cartridge_save_to_file(&gba.cart);
    frontend_destroy(&fe);
    gba_destroy(&gba);

    return 0;
}
