#include "xray.h"
#include "xray_draw.h"
#include "gba.h"

/* Global pointer — NULL when X-Ray is not wired up */
XRayState* g_xray = NULL;

/* Panel layout constants */
#define PANEL_GAP 2

/* Left column: x=0..639 (640px), Right column: x=640..1279 (640px) */
/* Top row height: 480px, Bottom row height: 480px */
#define COL_L_X     0
#define COL_R_X     640
#define COL_W       640

/* Panel 1: PPU Layers (top-left) */
#define P1_X COL_L_X
#define P1_Y 0
#define P1_W COL_W
#define P1_H 480

/* Panel 2: Tile/Palette Inspector (bottom-left) */
#define P2_X COL_L_X
#define P2_Y 480
#define P2_W COL_W
#define P2_H 480

/* Panel 3: CPU State (top-right, upper) */
#define P3_X COL_R_X
#define P3_Y 0
#define P3_W COL_W
#define P3_H 280

/* Panel 4: Audio Monitor (top-right, lower) */
#define P4_X COL_R_X
#define P4_Y 280
#define P4_W COL_W
#define P4_H 280

/* Panel 5: DMA/Timer/IRQ Activity (bottom-right) */
#define P5_X COL_R_X
#define P5_Y 560
#define P5_W COL_W
#define P5_H 400

static void xray_draw_panel_frame(uint32_t* buf, int buf_w, int buf_h,
                                  int px, int py, int pw, int ph,
                                  const char* title) {
    /* Panel background */
    xray_draw_rect(buf, buf_w, buf_h, px + 1, py + 1, pw - 2, ph - 2,
                   XRAY_COL_PANEL_BG);
    /* Border */
    xray_draw_rect_outline(buf, buf_w, buf_h, px, py, pw, ph,
                           XRAY_COL_BORDER);
    /* Title bar */
    xray_draw_rect(buf, buf_w, buf_h, px + 1, py + 1, pw - 2, 12,
                   0xFF182040);
    xray_draw_text(buf, buf_w, buf_h, px + 4, py + 2, title,
                   XRAY_COL_HEADER);
}

static bool xray_create_window(XRayState* state) {
    state->window = SDL_CreateWindow("GBA X-Ray", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, XRAY_WIDTH,
                                     XRAY_HEIGHT, SDL_WINDOW_SHOWN);
    if (!state->window) {
        LOG_ERROR("X-Ray: SDL window creation failed: %s", SDL_GetError());
        return false;
    }

    state->renderer = SDL_CreateRenderer(state->window, -1,
                                         SDL_RENDERER_ACCELERATED);
    if (!state->renderer) {
        LOG_ERROR("X-Ray: SDL renderer creation failed: %s", SDL_GetError());
        SDL_DestroyWindow(state->window);
        state->window = NULL;
        return false;
    }

    state->texture = SDL_CreateTexture(state->renderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       XRAY_WIDTH, XRAY_HEIGHT);
    if (!state->texture) {
        LOG_ERROR("X-Ray: SDL texture creation failed: %s", SDL_GetError());
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        state->renderer = NULL;
        state->window = NULL;
        return false;
    }

    state->window_id = SDL_GetWindowID(state->window);
    state->ever_opened = true;
    LOG_INFO("X-Ray window created (%dx%d)", XRAY_WIDTH, XRAY_HEIGHT);
    return true;
}

static void xray_destroy_window(XRayState* state) {
    if (state->texture) {
        SDL_DestroyTexture(state->texture);
        state->texture = NULL;
    }
    if (state->renderer) {
        SDL_DestroyRenderer(state->renderer);
        state->renderer = NULL;
    }
    if (state->window) {
        SDL_DestroyWindow(state->window);
        state->window = NULL;
    }
    state->window_id = 0;
}

void xray_init(XRayState* state) {
    memset(state, 0, sizeof(XRayState));
    state->active = false;
    state->ever_opened = false;
}

void xray_destroy(XRayState* state) {
    xray_destroy_window(state);
}

void xray_toggle(XRayState* state) {
    if (state->active) {
        /* Hide the window */
        state->active = false;
        if (state->window) {
            SDL_HideWindow(state->window);
        }
        LOG_INFO("X-Ray: disabled");
    } else {
        /* Show (or create) the window */
        if (!state->window) {
            if (!xray_create_window(state)) return;
        } else {
            SDL_ShowWindow(state->window);
            SDL_RaiseWindow(state->window);
        }
        state->active = true;
        LOG_INFO("X-Ray: enabled");
    }
}

static void xray_decay_flash(XRayState* state) {
    for (int i = 0; i < 4; i++) {
        if (state->timer_flash[i] > 0) state->timer_flash[i]--;
        if (state->dma_flash[i] > 0) state->dma_flash[i]--;
    }
    for (int i = 0; i < 16; i++) {
        if (state->irq_flash[i] > 0) state->irq_flash[i]--;
    }
}

static void xray_update_ips(XRayState* state, GBA* gba) {
    state->ips_frame_counter++;

    /* Use total_cycles delta (cycles_executed is reset per cpu_run call) */
    uint64_t delta = gba->total_cycles - state->ips_last_total_cycles;
    state->ips_last_total_cycles = gba->total_cycles;
    state->ips_count += delta;

    /* Update display every 60 frames (~1 second) */
    if (state->ips_frame_counter >= 60) {
        state->ips_display = state->ips_count;
        state->ips_count = 0;
        state->ips_frame_counter = 0;
    }
}

void xray_render(XRayState* state, GBA* gba) {
    if (!state->active || !state->window) return;

    /* Decay activity flash counters */
    xray_decay_flash(state);

    /* Update IPS counter */
    xray_update_ips(state, gba);

    /* Capture PPU layers (re-render in isolation) */
    xray_capture_ppu_layers(&gba->ppu, state);

    /* Capture audio snapshot */
    xray_capture_audio(&gba->apu, state);

    /* Clear framebuffer */
    uint32_t* fb = state->framebuffer;
    for (int i = 0; i < XRAY_WIDTH * XRAY_HEIGHT; i++) {
        fb[i] = XRAY_COL_BG;
    }

    /* Draw panel frames */
    xray_draw_panel_frame(fb, XRAY_WIDTH, XRAY_HEIGHT, P1_X, P1_Y, P1_W, P1_H,
                          "PPU LAYERS");
    xray_draw_panel_frame(fb, XRAY_WIDTH, XRAY_HEIGHT, P2_X, P2_Y, P2_W, P2_H,
                          "TILES / PALETTE");
    xray_draw_panel_frame(fb, XRAY_WIDTH, XRAY_HEIGHT, P3_X, P3_Y, P3_W, P3_H,
                          "CPU STATE");
    xray_draw_panel_frame(fb, XRAY_WIDTH, XRAY_HEIGHT, P4_X, P4_Y, P4_W, P4_H,
                          "AUDIO MONITOR");
    xray_draw_panel_frame(fb, XRAY_WIDTH, XRAY_HEIGHT, P5_X, P5_Y, P5_W, P5_H,
                          "DMA / TIMER / IRQ");

    /* Render each panel */
    xray_render_ppu(fb, XRAY_WIDTH, XRAY_HEIGHT, P1_X, P1_Y, P1_W, P1_H,
                    &gba->ppu, state);
    xray_render_tiles(fb, XRAY_WIDTH, XRAY_HEIGHT, P2_X, P2_Y, P2_W, P2_H,
                      &gba->ppu);
    xray_render_cpu(fb, XRAY_WIDTH, XRAY_HEIGHT, P3_X, P3_Y, P3_W, P3_H,
                    &gba->cpu, state);
    xray_render_audio(fb, XRAY_WIDTH, XRAY_HEIGHT, P4_X, P4_Y, P4_W, P4_H,
                      &gba->apu, state);
    xray_render_activity(fb, XRAY_WIDTH, XRAY_HEIGHT, P5_X, P5_Y, P5_W, P5_H,
                         gba->timers, &gba->dma, &gba->interrupts, state);

    /* Present to SDL */
    SDL_UpdateTexture(state->texture, NULL, fb,
                      XRAY_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(state->renderer);
    SDL_RenderCopy(state->renderer, state->texture, NULL, NULL);
    SDL_RenderPresent(state->renderer);

    state->frame_counter++;
}
