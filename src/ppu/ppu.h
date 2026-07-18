#ifndef PPU_H
#define PPU_H

#include "common.h"

// Interrupt bit flags (shared with interrupt.h)
#define IRQ_VBLANK  (1 << 0)
#define IRQ_HBLANK  (1 << 1)
#define IRQ_VCOUNT  (1 << 2)

struct PPU {
    // Control registers
    uint16_t dispcnt;
    uint16_t dispstat;
    uint16_t vcount;

    // Background control
    uint16_t bg_cnt[4];
    uint16_t bg_hofs[4];
    uint16_t bg_vofs[4];

    // Affine BG parameters (BG2, BG3)
    int16_t bg_pa[2], bg_pb[2], bg_pc[2], bg_pd[2];
    int32_t bg_ref_x[2], bg_ref_y[2];           // Internal (latched)
    int32_t bg_ref_x_latch[2], bg_ref_y_latch[2]; // Written values

    // Window
    uint16_t win_h[2], win_v[2];
    uint16_t winin, winout;

    // Blending
    uint16_t bldcnt;
    uint16_t bldalpha;
    uint16_t bldy;

    // Mosaic
    uint16_t mosaic;

    // Rendering buffers
    uint16_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    uint16_t scanline_buffer[SCREEN_WIDTH];

    // Layer tracking for blending (filled during compositing)
    uint8_t top_layer[SCREEN_WIDTH];     // Which layer produced the top pixel (0-3=BG, 4=OBJ, 5=backdrop)
    uint16_t second_pixel[SCREEN_WIDTH]; // Color of second-highest-priority pixel
    uint8_t second_layer[SCREEN_WIDTH];  // Layer ID of second pixel

    // OBJ window: true if an OBJ-window sprite covers this pixel on the current scanline
    bool obj_window[SCREEN_WIDTH];

    // Per-pixel flag: true if the top OBJ pixel was from a mosaic-enabled sprite
    bool obj_mosaic[SCREEN_WIDTH];

    // Per-pixel flag: true if the top OBJ pixel is semi-transparent
    // (GFX mode 1) — forces alpha blending in the effects pass
    bool obj_semitransparent[SCREEN_WIDTH];

    // Resolved per-pixel window mask for the current scanline.
    // Bits 0-3 = BG0-BG3 enable, bit 4 = OBJ enable, bit 5 = color effects.
    // All bits set (0x3F) when no window is enabled in DISPCNT.
    uint8_t win_mask[SCREEN_WIDTH];

    // Memory pointers (point into bus memory)
    uint8_t* palette_ram;
    uint8_t* vram;
    uint8_t* oam;

    // Cycle tracking
    uint32_t cycle_counter;
};
typedef struct PPU PPU;

// Push a rendered pixel onto the scanline, demoting the current top pixel to
// the second slot so the blend pass can reach it. Layer IDs: 0-3 = BG0-BG3,
// 4 = OBJ. The backdrop (5) is the initial fill and never pushed here.
// Writes are dropped when the layer is disabled for this pixel's window
// region, so a masked layer never displaces the pixel beneath it.
// Returns true if the pixel was written.
static inline bool ppu_push_pixel(PPU* ppu, uint32_t x, uint16_t color,
                                  uint8_t layer) {
    if (!BIT(ppu->win_mask[x], layer)) return false;
    ppu->second_pixel[x] = ppu->scanline_buffer[x];
    ppu->second_layer[x] = ppu->top_layer[x];
    ppu->scanline_buffer[x] = color;
    ppu->top_layer[x] = layer;
    return true;
}

void ppu_init(PPU* ppu);
void ppu_render_scanline(PPU* ppu);
void ppu_set_hblank(PPU* ppu, bool active);
void ppu_set_vblank(PPU* ppu, bool active);
void ppu_increment_vcount(PPU* ppu);
bool ppu_vcount_match(PPU* ppu);

// Background renderers (background.c)
void ppu_render_bg_regular(PPU* ppu, int bg_index);
void ppu_render_bg_affine(PPU* ppu, int bg_index);

// Bitmap mode renderers (bitmap.c)
void ppu_render_mode3(PPU* ppu);
void ppu_render_mode4(PPU* ppu);
void ppu_render_mode5(PPU* ppu);

// Sprite renderer (sprites.c)
void ppu_render_sprites_at_priority(PPU* ppu, int priority);
void ppu_render_sprites(PPU* ppu);
void ppu_build_obj_window(PPU* ppu);

// Effects (effects.c)
void ppu_apply_mosaic_scanline(PPU* ppu);
void ppu_build_window_mask(PPU* ppu);
void ppu_apply_blend_scanline(PPU* ppu);

#endif // PPU_H
