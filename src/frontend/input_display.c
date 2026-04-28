#include "input_display.h"
#include "frontend.h"
#include "gba.h"
#include "memory/io_regs.h"
#include "memory/bus.h"
#include "common.h"
#include "frontend/overlay_draw.h"
#include <string.h>

/* GBA KEYINPUT bit layout (active-low; we invert for the held mask).
 * 0=A 1=B 2=Sel 3=Start 4=Right 5=Left 6=Up 7=Down 8=R 9=L */
#define BTN_A      0
#define BTN_B      1
#define BTN_SELECT 2
#define BTN_START  3
#define BTN_RIGHT  4
#define BTN_LEFT   5
#define BTN_UP     6
#define BTN_DOWN   7
#define BTN_R      8
#define BTN_L      9

#define PANEL_W 64
#define PANEL_H 40
#define MARGIN  4

#define COL_BG   0xA0000000u  /* ~62% alpha black background */
#define COL_DIM  0xFF445566u  /* unlit button */
#define COL_LIT  0xFF00FFCCu  /* lit button */

uint16_t input_display_held_mask(uint16_t keyinput) {
    /* KEYINPUT only uses the low 10 bits. */
    return (uint16_t)(~keyinput & 0x03FF);
}

void input_display_anchor(int screen_w, int screen_h,
                          int panel_w, int panel_h,
                          int* out_x, int* out_y) {
    int x = screen_w - panel_w - MARGIN;
    int y = screen_h - panel_h - MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *out_x = x;
    *out_y = y;
}

static void draw_btn(uint32_t* buf, int bw, int bh, int x, int y,
                     int w, int h, bool held) {
    overlay_draw_rect(buf, bw, bh, x, y, w, h,
                   held ? COL_LIT : COL_DIM);
}

void input_display_render(Frontend* fe, GBA* gba) {
    if (!fe || !gba || !fe->input_display_enabled) return;

    uint16_t key = bus_read16(&gba->bus, REG_KEYINPUT);
    uint16_t held = input_display_held_mask(key);

    int ox, oy;
    input_display_anchor(SCREEN_WIDTH, SCREEN_HEIGHT,
                         PANEL_W, PANEL_H, &ox, &oy);

    uint32_t* b = fe->overlay_buffer;
    int bw = SCREEN_WIDTH, bh = SCREEN_HEIGHT;

    /* Background. */
    overlay_draw_rect(b, bw, bh, ox, oy, PANEL_W, PANEL_H, COL_BG);

    /* D-pad cross at (ox+4, oy+12), 4x4 px cells. */
    int dx = ox + 4, dy = oy + 12;
    draw_btn(b, bw, bh, dx + 4, dy,     4, 4, (bool)(held & (1 << BTN_UP)));
    draw_btn(b, bw, bh, dx + 4, dy + 8, 4, 4, (bool)(held & (1 << BTN_DOWN)));
    draw_btn(b, bw, bh, dx,     dy + 4, 4, 4, (bool)(held & (1 << BTN_LEFT)));
    draw_btn(b, bw, bh, dx + 8, dy + 4, 4, 4, (bool)(held & (1 << BTN_RIGHT)));

    /* A and B buttons on the right. */
    draw_btn(b, bw, bh, ox + PANEL_W - 14, oy + 14, 6, 6,
             (bool)(held & (1 << BTN_A)));
    draw_btn(b, bw, bh, ox + PANEL_W - 22, oy + 22, 6, 6,
             (bool)(held & (1 << BTN_B)));

    /* L / R as small pips at top corners. */
    draw_btn(b, bw, bh, ox + 2,            oy + 2, 8, 4,
             (bool)(held & (1 << BTN_L)));
    draw_btn(b, bw, bh, ox + PANEL_W - 10, oy + 2, 8, 4,
             (bool)(held & (1 << BTN_R)));

    /* Start / Select pills along the bottom. */
    draw_btn(b, bw, bh, ox + 22, oy + PANEL_H - 6, 8, 3,
             (bool)(held & (1 << BTN_START)));
    draw_btn(b, bw, bh, ox + 34, oy + PANEL_H - 6, 8, 3,
             (bool)(held & (1 << BTN_SELECT)));

    fe->overlay_dirty = true;
}
