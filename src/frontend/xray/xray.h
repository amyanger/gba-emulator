#ifndef XRAY_H
#define XRAY_H

#include "common.h"
#include <SDL2/SDL.h>

/* Forward declarations */
typedef struct GBA GBA;
typedef struct ARM7TDMI ARM7TDMI;
typedef struct PPU PPU;
typedef struct APU APU;
typedef struct Timer Timer;
typedef struct DMAController DMAController;
typedef struct InterruptController InterruptController;

/* X-Ray window dimensions */
#define XRAY_WIDTH  1280
#define XRAY_HEIGHT 960

/* GBA screen dimensions for layer buffers */
#define XRAY_LAYER_W SCREEN_WIDTH   /* 240 */
#define XRAY_LAYER_H SCREEN_HEIGHT  /* 160 */

/* Audio snapshot size */
#define XRAY_AUDIO_SNAP 512

/* Activity flash duration in frames */
#define XRAY_FLASH_FRAMES 8

/* Color scheme */
#define XRAY_COL_BG        0xFF0A0A2E  /* Dark navy background */
#define XRAY_COL_PANEL_BG  0xFF0D0D36  /* Slightly lighter panel bg */
#define XRAY_COL_BORDER    0xFF334466  /* Panel border */
#define XRAY_COL_HEADER    0xFF00FFFF  /* Cyan panel headers */
#define XRAY_COL_LABEL     0xFF88AACC  /* Muted blue-gray labels */
#define XRAY_COL_VALUE     0xFF00FF88  /* Green values */
#define XRAY_COL_DIM       0xFF445566  /* Dimmed/inactive text */
#define XRAY_COL_FLASH     0xFFFF2222  /* Bright red activity flash */
#define XRAY_COL_WHITE     0xFFFFFFFF
#define XRAY_COL_BLACK     0xFF000000

/* Layer overlay colors */
#define XRAY_COL_BG0       0xFFFF4444  /* Red */
#define XRAY_COL_BG1       0xFF44FF44  /* Green */
#define XRAY_COL_BG2       0xFF4444FF  /* Blue */
#define XRAY_COL_BG3       0xFFFFFF44  /* Yellow */
#define XRAY_COL_OBJ       0xFFFF44FF  /* Magenta */
#define XRAY_COL_BACKDROP  0xFF888888  /* Gray */

struct XRayState {
    /* SDL2 resources */
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    uint32_t window_id;

    /* State */
    bool active;        /* Currently visible */
    bool ever_opened;   /* Track first open for window creation */

    /* Main framebuffer (1280x960, ARGB8888) */
    uint32_t framebuffer[XRAY_WIDTH * XRAY_HEIGHT];

    /* PPU layer isolation buffers (240x160 each, GBA 15-bit) */
    uint16_t layer_bg[4][XRAY_LAYER_W * XRAY_LAYER_H];
    uint16_t layer_obj[XRAY_LAYER_W * XRAY_LAYER_H];

    /* Layer map: which layer produced each pixel (accumulated per scanline) */
    uint8_t layer_map[XRAY_LAYER_H][XRAY_LAYER_W];

    /* Audio snapshot (stereo interleaved) */
    int16_t audio_snapshot[XRAY_AUDIO_SNAP * 2];
    uint32_t audio_snapshot_count;

    /* Activity flash counters (count down from XRAY_FLASH_FRAMES to 0) */
    uint8_t timer_flash[4];
    uint8_t dma_flash[4];
    uint8_t irq_flash[16];

    /* IPS (instructions per second) tracking */
    uint64_t ips_count;
    uint64_t ips_display;
    uint32_t ips_frame_counter;
    uint64_t ips_last_total_cycles;

    /* Frame skip counter */
    uint8_t frame_counter;
};
typedef struct XRayState XRayState;

/* Global pointer — NULL when X-Ray is disabled.
 * Subsystem hooks check this before notifying. */
extern XRayState* g_xray;

/* Lifecycle */
void xray_init(XRayState* state);
void xray_destroy(XRayState* state);
void xray_toggle(XRayState* state);

/* Per-frame rendering (call after gba_run_frame) */
void xray_render(XRayState* state, GBA* gba);

/* Activity notification hooks (called from subsystems) */
static inline void xray_notify_timer_overflow(XRayState* state, int timer_id) {
    if (state && timer_id >= 0 && timer_id < 4)
        state->timer_flash[timer_id] = XRAY_FLASH_FRAMES;
}

static inline void xray_notify_dma_trigger(XRayState* state, int channel) {
    if (state && channel >= 0 && channel < 4)
        state->dma_flash[channel] = XRAY_FLASH_FRAMES;
}

static inline void xray_notify_irq(XRayState* state, uint16_t irq_bit) {
    if (!state) return;
    for (int i = 0; i < 16; i++) {
        if (irq_bit & (1 << i))
            state->irq_flash[i] = XRAY_FLASH_FRAMES;
    }
}

/* Panel render functions (implemented in separate files) */
void xray_render_cpu(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                     int pw, int ph, ARM7TDMI* cpu, XRayState* state);

void xray_render_activity(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                          int pw, int ph, Timer* timers,
                          DMAController* dma, InterruptController* ic,
                          XRayState* state);

void xray_render_ppu(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                     int pw, int ph, PPU* ppu, XRayState* state);

void xray_capture_ppu_layers(PPU* ppu, XRayState* state);

void xray_render_tiles(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                       int pw, int ph, PPU* ppu);

void xray_render_audio(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                       int pw, int ph, APU* apu, XRayState* state);

void xray_capture_audio(APU* apu, XRayState* state);

#endif /* XRAY_H */
