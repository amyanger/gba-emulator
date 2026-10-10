#ifndef XRAY_H
#define XRAY_H

#include "common.h"
#include "ui/ui_canvas.h"
#include "ui/ui_font.h"
#ifdef ENABLE_REWIND
#include "rewind/rewind.h"
#endif
#include <SDL2/SDL.h>

/* Forward declarations */
typedef struct GBA GBA;
typedef struct ARM7TDMI ARM7TDMI;
typedef struct PPU PPU;
typedef struct APU APU;
typedef struct Timer Timer;
typedef struct DMAController DMAController;
typedef struct InterruptController InterruptController;

/* Layout is designed in points and drawn at a density (pixels per point). */
#define XRAY_W_PT          1200
#define XRAY_H_PT          820
#define XRAY_TITLEBAR_PT   28
#define XRAY_MIN_TEXT_PT   10.0f   /* smallest text drawn: pills (UI_SIZE_PILL) */
#define XRAY_MAX_DENSITY   2.0f
#define XRAY_FB_MAX_W      2400
#define XRAY_FB_MAX_H      1640

/* GBA screen dimensions for layer buffers */
#define XRAY_LAYER_W SCREEN_WIDTH   /* 240 */
#define XRAY_LAYER_H SCREEN_HEIGHT  /* 160 */

/* Audio snapshot size */
#define XRAY_AUDIO_SNAP 512

/* Activity flash duration in frames */
#define XRAY_FLASH_FRAMES 8

/* Text sizes and spacing in points */
#define XRAY_SIZE_TITLE    14.0f
#define XRAY_SIZE_TEXT     11.5f
#define XRAY_SIZE_CAPTION  11.0f
#define XRAY_SIZE_HEAD     10.5f
#define XRAY_ROW           16.0f
#define XRAY_PAD           14.0f

/* Layer accent colors, matched between thumbnails and the layer table */
#define XRAY_COL_BG0       0xFFFF6B6Bu
#define XRAY_COL_BG1       0xFF3DDC97u
#define XRAY_COL_BG2       0xFF6FA8FFu
#define XRAY_COL_BG3       0xFFF2C94Cu
#define XRAY_COL_OBJ       0xFFFF7AA8u
#define XRAY_COL_BACKDROP  0xFF7D8494u

typedef enum {
    XRAY_CARD_SEE,
    XRAY_CARD_HEAR,
    XRAY_CARD_TILES,
    XRAY_CARD_CPU,
    XRAY_CARD_SCENES,
    XRAY_CARD_COUNT
} XRayCard;

struct XRayState {
    /* SDL2 resources */
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    int tex_w, tex_h;
    uint32_t window_id;

    /* State */
    bool active;        /* Currently visible (or feeding a headless export) */
    bool ever_opened;   /* Track first open for window creation */

    /* Canvas pixels, sized for the largest density (ARGB8888) */
    uint32_t framebuffer[XRAY_FB_MAX_W * XRAY_FB_MAX_H];
    /* Native-resolution staging for tile sheets, palettes and the layer map */
    uint32_t scratch[XRAY_LAYER_W * XRAY_LAYER_H];

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
};
typedef struct XRayState XRayState;

/* Global pointer — NULL when X-Ray is disabled.
 * Subsystem hooks check this before notifying. */
extern XRayState* g_xray;

/* Lifecycle (xray_init in xray_draw.c so headless code can use it; the rest in xray.c) */
void xray_init(XRayState* state);
void xray_destroy(XRayState* state);
void xray_toggle(XRayState* state);

/* Per-frame rendering (call after gba_run_frame) */
void xray_render(XRayState* state, GBA* gba);

/* Layout and drawing (xray_draw.c, no SDL) */
extern const UiRect xray_cards[XRAY_CARD_COUNT];
extern const char* const xray_titles[XRAY_CARD_COUNT];
extern const char* const xray_captions[XRAY_CARD_COUNT];

typedef struct { float scale; int win_w, win_h; } XRayFit;
/* Window size for a display's usable area in points (0 = unknown) and its pixel ratio. */
XRayFit xray_fit(int usable_w_pt, int usable_h_pt, float px_per_pt);
/* Canvas pixel size and density for a renderer output size, capped at XRAY_MAX_DENSITY. */
void xray_canvas_size(int out_w, int out_h, int* cw, int* ch, float* density);

void xray_draw(UiCanvas* c, GBA* gba, XRayState* s);
/* Fade activity flashes by one frame (call once per emulated frame). */
void xray_decay_flash(XRayState* s);
/* Card background, title, optional pill and caption. Returns the content top in points. */
float xray_card(UiCanvas* c, XRayCard id, const char* pill);
/* Nearest-neighbour blits of native-size images into a point rect, clipped to the canvas. */
void xray_blit_555(UiCanvas* c, UiRect dst, const uint16_t* src, int sw, int sh);
void xray_blit_argb(UiCanvas* c, UiRect dst, const uint32_t* src, int sw, int sh);
float xray_textf(UiCanvas* c, UiFont f, float size, float x, float y, float max_w, UiAlign a,
                 uint32_t argb, const char* fmt, ...);

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

/* Captures (no-ops unless state->active) */
void xray_capture_ppu_layers(PPU* ppu, XRayState* state);
void xray_capture_audio(APU* apu, XRayState* state);

/* Cards (implemented in separate files) */
void xray_render_ppu(UiCanvas* c, PPU* ppu, XRayState* s);
void xray_render_tiles(UiCanvas* c, PPU* ppu, XRayState* s);
void xray_render_audio(UiCanvas* c, APU* apu, XRayState* s);
void xray_render_cpu(UiCanvas* c, ARM7TDMI* cpu);
/* Address of the executing instruction: PC runs 8 bytes ahead in ARM, 4 in Thumb. */
uint32_t xray_cpu_exec_addr(const ARM7TDMI* cpu);
void xray_render_activity(UiCanvas* c, Timer* timers, DMAController* dma,
                          InterruptController* ic, XRayState* s);
#ifdef ENABLE_REWIND
/* One status line inside the Processor card */
void xray_render_rewind(UiCanvas* c, float x, float y, float w, const RewindBuffer* rb);
#endif

#endif /* XRAY_H */
