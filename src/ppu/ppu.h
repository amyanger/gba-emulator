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

    // Memory pointers (point into bus memory)
    uint8_t* palette_ram;
    uint8_t* vram;
    uint8_t* oam;

    // Cycle tracking
    uint32_t cycle_counter;
};
typedef struct PPU PPU;

void ppu_init(PPU* ppu);
void ppu_render_scanline(PPU* ppu);
void ppu_set_hblank(PPU* ppu, bool active);
void ppu_set_vblank(PPU* ppu, bool active);
void ppu_increment_vcount(PPU* ppu);
bool ppu_vcount_match(PPU* ppu);

#endif // PPU_H
