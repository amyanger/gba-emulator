#include "gba.h"
#include "cpu/arm7tdmi.h"
#include "cheat/cheat_file.h"
#include "frontend/frontend.h"
#include "rewind/rewind.h"
#include "savestate/savestate.h"
#include <stdio.h>

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
static XRayState s_xray_state;
#endif

static void print_usage(const char* prog) {
    printf("Usage: %s <rom.gba> [options]\n", prog);
    printf("Options:\n");
    printf("  --bios <file>   Load GBA BIOS ROM\n");
    printf("  --scale <n>     Window scale factor (default: 3)\n");
    printf("  --cheats <file> Load cheat codes from file (.cht format)\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* rom_path = argv[1];
    const char* bios_path = NULL;
    const char* cheat_path = NULL;
    int scale = 3;

    // Parse arguments
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
            bios_path = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cheats") == 0 && i + 1 < argc) {
            cheat_path = argv[++i];
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

    // Load cheat codes if provided
    if (cheat_path) {
        int32_t loaded = cheat_file_load(&gba.cheats, cheat_path);
        if (loaded < 0) {
            LOG_WARN("Failed to load cheat file: %s", cheat_path);
        } else {
            LOG_INFO("Loaded %d cheats from %s", loaded, cheat_path);
        }
    }

    // Initialize frontend (SDL2)
    Frontend fe;
    if (!frontend_init(&fe, scale)) {
        LOG_ERROR("Failed to initialize frontend");
        gba_destroy(&gba);
        return 1;
    }

    snprintf(fe.rom_path, sizeof(fe.rom_path), "%s", rom_path);

    // Initialize audio
    frontend_audio_init(&fe);

#ifdef ENABLE_XRAY
    xray_init(&s_xray_state);
    gba.xray = &s_xray_state;
    g_xray = &s_xray_state;
#endif

    LOG_INFO("Starting emulation...");

    // Main loop
    while (fe.running && gba.running) {
        frontend_poll_input(&fe, &gba);

        // Save state handling (at frame boundary)
        if (fe.save_requested || fe.load_requested) {
            char ss_path[512];
            savestate_slot_path(fe.rom_path, fe.savestate_slot,
                                ss_path, sizeof(ss_path));
            if (fe.save_requested) {
                fe.save_requested = false;
                SaveStateResult res = savestate_save(&gba, ss_path);
                if (res == SS_OK) {
                    LOG_INFO("State saved to slot %d", fe.savestate_slot);
                } else {
                    LOG_ERROR("Failed to save state (error %d)", res);
                }
            }
            if (fe.load_requested) {
                fe.load_requested = false;
                SaveStateResult res = savestate_load(&gba, ss_path);
                if (res == SS_OK) {
                    LOG_INFO("State loaded from slot %d", fe.savestate_slot);
                    SDL_ClearQueuedAudio(fe.audio_device);
                    rewind_clear(&gba.rewind);   // past is no longer valid
                } else {
                    LOG_ERROR("Failed to load state (error %d)", res);
                }
            }
        }

        bool rewind_active_now = fe.rewind_hold && rewind_depth(&gba.rewind) > 0;

        // Detect transitions OUT of rewind (cleanup audio + APU state)
        {
            static bool prev_rewind = false;
            if (prev_rewind && !rewind_active_now) {
                rewind_end(&gba.rewind);
                SDL_ClearQueuedAudio(fe.audio_device);
                gba.apu.read_pos = gba.apu.write_pos;
            }
            // Detect transitions INTO rewind
            if (!prev_rewind && rewind_active_now) {
                rewind_begin(&gba.rewind);
                SDL_ClearQueuedAudio(fe.audio_device);
                gba.apu.read_pos = gba.apu.write_pos;
            }
            prev_rewind = rewind_active_now;
        }

        if (rewind_active_now) {
            // Reverse playback: pop one frame back, render, no audio.
            if (!rewind_step(&gba.rewind, &gba)) {
                // Hit oldest frame — freeze on screen until release.
            }
            frontend_present_frame(&fe, gba.ppu.framebuffer);
#ifdef ENABLE_XRAY
            xray_render(&s_xray_state, &gba);
#endif
            // Drain APU ring buffer (state was restored from snapshot)
            gba.apu.read_pos = gba.apu.write_pos;
            frontend_frame_sync(&fe);
            continue;
        }

        gba_run_frame(&gba);

        if (gba.frame_complete) {
            bool ff_active = fe.ff_hold || fe.ff_toggle;

            // Update window title on FF state change
            {
                static bool prev_ff = false;
                if (ff_active != prev_ff) {
                    frontend_set_ff_indicator(&fe, ff_active);
                    if (!ff_active) {
                        // Returning to normal speed: clean audio state
                        SDL_ClearQueuedAudio(fe.audio_device);
                        gba.apu.read_pos = gba.apu.write_pos;
                        fe.ff_frame_count = 0;
                    }
                    prev_ff = ff_active;
                }
            }

            if (ff_active) {
                // Fast-forward: render every Nth frame, skip audio and sync
                if (fe.ff_frame_count++ % fe.ff_frame_skip == 0) {
                    frontend_present_frame(&fe, gba.ppu.framebuffer);
#ifdef ENABLE_XRAY
                    xray_render(&s_xray_state, &gba);
#endif
                }
                // Drain APU ring buffer to prevent stall
                gba.apu.read_pos = gba.apu.write_pos;
            } else {
                frontend_present_frame(&fe, gba.ppu.framebuffer);
                frontend_push_audio(&fe, &gba.apu);
#ifdef ENABLE_XRAY
                xray_render(&s_xray_state, &gba);
#endif
                frontend_frame_sync(&fe);
            }
        }
    }

    // Cleanup
    cartridge_save_to_file(&gba.cart);
#ifdef ENABLE_XRAY
    xray_destroy(&s_xray_state);
    g_xray = NULL;
#endif
    frontend_destroy(&fe);
    gba_destroy(&gba);

    return 0;
}
