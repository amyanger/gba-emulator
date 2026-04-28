#include "gba.h"
#include "cpu/arm7tdmi.h"
#include "cheat/cheat_file.h"
#include "frontend/frontend.h"
#include "frontend/overlay_draw.h"
#include "frontend/input_display.h"
#include "frontend/slot_picker.h"
#ifdef ENABLE_REWIND
#include "rewind/rewind.h"
#endif
#include "input/keymap.h"
#include "savestate/savestate.h"
#include "screenshot/screenshot.h"
#include "sio/sio_link.h"
#include "trace/trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef ENABLE_XRAY
#include "frontend/xray/xray.h"
static XRayState s_xray_state;
#endif

/* Present one frame: clear overlay, render HUD layers, then blit to screen. */
static void render_with_overlay(Frontend* fe, GBA* gba) {
    frontend_overlay_clear(fe);
    input_display_render(fe, gba);
    slot_picker_render(fe);
    frontend_present_frame(fe, gba->ppu.framebuffer);
}

static void print_usage(const char* prog) {
    printf("Usage: %s <rom.gba> [options]\n", prog);
    printf("Options:\n");
    printf("  --bios <file>          Load GBA BIOS ROM\n");
    printf("  --scale <n>            Window scale factor (default: 3)\n");
    printf("  --cheats <file>        Load cheat codes from file (.cht format)\n");
    printf("  --keymap <file>        Load custom keyboard bindings\n");
    printf("  --mute                 Start with audio muted (toggle in-game with M)\n");
    printf("  --link-master <path>   Listen for SIO peer at AF_UNIX path\n");
    printf("  --link-client <path>   Connect to SIO peer at AF_UNIX path\n");
    printf("  --trace <file>         Write per-instruction trace to file\n");
    printf("  --trace-from <hex>     Only trace instructions at PC >= hex\n");
    printf("  --trace-to <hex>       Only trace instructions at PC <= hex\n");
    printf("  --trace-frames <n>     Stop tracing after n frames\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* rom_path = argv[1];
    const char* bios_path = NULL;
    const char* cheat_path = NULL;
    const char* keymap_path = NULL;
    const char* link_master_path = NULL;
    const char* link_client_path = NULL;
    const char* trace_path = NULL;
    uint32_t trace_from = 0;
    uint32_t trace_to = 0;
    uint32_t trace_frames = 0;
    int scale = 3;
    bool start_muted = false;

    // Parse arguments
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc) {
            bios_path = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--cheats") == 0 && i + 1 < argc) {
            cheat_path = argv[++i];
        } else if (strcmp(argv[i], "--keymap") == 0 && i + 1 < argc) {
            keymap_path = argv[++i];
        } else if (strcmp(argv[i], "--link-master") == 0 && i + 1 < argc) {
            link_master_path = argv[++i];
        } else if (strcmp(argv[i], "--link-client") == 0 && i + 1 < argc) {
            link_client_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (strcmp(argv[i], "--trace-from") == 0 && i + 1 < argc) {
            trace_from = (uint32_t)strtoul(argv[++i], NULL, 16);
        } else if (strcmp(argv[i], "--trace-to") == 0 && i + 1 < argc) {
            trace_to = (uint32_t)strtoul(argv[++i], NULL, 16);
        } else if (strcmp(argv[i], "--trace-frames") == 0 && i + 1 < argc) {
            trace_frames = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--mute") == 0) {
            start_muted = true;
        }
    }

    // Initialize GBA
    GBA gba;
    gba_init(&gba);

    // Set up local link cable if requested. link_peer_listen blocks until a
    // peer connects, so we do this before the main loop starts.
    LinkPeer* link_peer = NULL;
    if (link_master_path) {
        link_peer = link_peer_create();
        LOG_INFO("Listening for SIO peer at %s ...", link_master_path);
        if (!link_peer_listen(link_peer, link_master_path)) {
            LOG_ERROR("Failed to listen on %s", link_master_path);
            link_peer_shutdown(link_peer);
            link_peer = NULL;
        } else {
            LOG_INFO("SIO peer connected");
            gba_set_link_peer(&gba, link_peer);
        }
    } else if (link_client_path) {
        link_peer = link_peer_create();
        LOG_INFO("Connecting to SIO peer at %s ...", link_client_path);
        if (!link_peer_connect(link_peer, link_client_path)) {
            LOG_ERROR("Failed to connect to %s", link_client_path);
            link_peer_shutdown(link_peer);
            link_peer = NULL;
        } else {
            LOG_INFO("SIO peer connected");
            gba_set_link_peer(&gba, link_peer);
        }
    }

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
        // Mirror the normal-cleanup path so a failed ROM load doesn't leak
        // the link socket fd or leave a stray socket file behind.
        if (link_peer) link_peer_shutdown(link_peer);
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

    if (trace_path) {
        trace_init(trace_path, trace_from, trace_to, trace_frames);
    }

    // Initialize frontend (SDL2)
    Frontend fe;
    if (!frontend_init(&fe, scale)) {
        LOG_ERROR("Failed to initialize frontend");
        if (link_peer) link_peer_shutdown(link_peer);
        gba_destroy(&gba);
        return 1;
    }

    snprintf(fe.rom_path, sizeof(fe.rom_path), "%s", rom_path);
    fe.muted = start_muted;
    if (start_muted) frontend_set_mute_indicator(&fe, true);

    // Keyboard bindings (must come after frontend_init so SDL is up)
    keymap_reset_defaults();
    if (keymap_path) {
        keymap_load(keymap_path);
    }

    // Initialize audio
    frontend_audio_init(&fe);

#ifdef ENABLE_XRAY
    xray_init(&s_xray_state);
    gba.xray = &s_xray_state;
    g_xray = &s_xray_state;
#endif

    LOG_INFO("Starting emulation...");

    // Main loop
    bool prev_paused = false;
    while (fe.running && gba.running) {
        frontend_poll_input(&fe, &gba);

        // Pause: skip emulation, keep window responsive, mute audio.
        if (fe.paused != prev_paused) {
            frontend_set_pause_indicator(&fe, fe.paused);
            if (fe.paused) {
                SDL_ClearQueuedAudio(fe.audio_device);
                gba.apu.read_pos = gba.apu.write_pos;
            }
            prev_paused = fe.paused;
        }
        if (fe.paused) {
            if (fe.step_pending) {
                // Single-step: run exactly one frame then re-pause.
                fe.step_pending = false;
                gba_run_frame(&gba);
                if (gba.frame_complete) {
                    cartridge_save_tick(&gba.cart, time(NULL));
                }
            }
            render_with_overlay(&fe, &gba);
#ifdef ENABLE_XRAY
            xray_render(&s_xray_state, &gba);
#endif
            gba.apu.read_pos = gba.apu.write_pos;
            frontend_frame_sync(&fe);
            continue;
        }

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
#ifdef ENABLE_REWIND
                    rewind_clear(&gba.rewind);   // past is no longer valid
#endif
                } else {
                    LOG_ERROR("Failed to load state (error %d)", res);
                }
            }
        }

#ifdef ENABLE_REWIND
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
            render_with_overlay(&fe, &gba);
#ifdef ENABLE_XRAY
            xray_render(&s_xray_state, &gba);
#endif
            // Drain APU ring buffer (state was restored from snapshot)
            gba.apu.read_pos = gba.apu.write_pos;
            frontend_frame_sync(&fe);
            continue;
        }
#endif /* ENABLE_REWIND */

        gba_run_frame(&gba);

        if (gba.frame_complete) {
            trace_frame_tick();
            cartridge_save_tick(&gba.cart, time(NULL));
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
                    render_with_overlay(&fe, &gba);
#ifdef ENABLE_XRAY
                    xray_render(&s_xray_state, &gba);
#endif
                }
                // Drain APU ring buffer to prevent stall
                gba.apu.read_pos = gba.apu.write_pos;
            } else {
                render_with_overlay(&fe, &gba);
                frontend_push_audio(&fe, &gba.apu);
#ifdef ENABLE_XRAY
                xray_render(&s_xray_state, &gba);
#endif
                frontend_frame_sync(&fe);
            }

            if (fe.screenshot_requested) {
                fe.screenshot_requested = false;
                char shot_path[512];
                screenshot_path(fe.rom_path, time(NULL), shot_path, sizeof(shot_path));
                if (screenshot_save(gba.ppu.framebuffer, shot_path)) {
                    LOG_INFO("Screenshot saved: %s", shot_path);
                }
            }
        }
    }

    // Cleanup
    trace_shutdown();
    cartridge_save_to_file(&gba.cart);
#ifdef ENABLE_XRAY
    xray_destroy(&s_xray_state);
    g_xray = NULL;
#endif
    frontend_destroy(&fe);
    if (link_peer) link_peer_shutdown(link_peer);
    gba_destroy(&gba);

    return 0;
}
