#include "xray.h"
#include "xray_draw.h"
#include "timer/timer.h"
#include "memory/dma.h"
#include "interrupt/interrupt.h"

static const char* irq_names[16] = {
    "VBlank", "HBlank", "VCount", "Timer0",
    "Timer1", "Timer2", "Timer3", "Serial",
    "DMA0",   "DMA1",   "DMA2",   "DMA3",
    "Keypad", "GamePak", "---",   "---"
};

static const char* dma_timing_name(uint8_t timing) {
    switch (timing) {
    case 0:  return "Immed";
    case 1:  return "VBlnk";
    case 2:  return "HBlnk";
    case 3:  return "FIFO";
    default: return "???";
    }
}

/* Compute flash color: blend from FLASH to DIM based on remaining frames */
static uint32_t flash_color(uint8_t flash) {
    if (flash == 0) return XRAY_COL_DIM;
    /* Bright red intensity proportional to remaining flash frames */
    uint32_t intensity = (uint32_t)flash * 255 / XRAY_FLASH_FRAMES;
    return 0xFF000000 | (intensity << 16) | ((intensity / 4) << 8) |
           (intensity / 4);
}

void xray_render_activity(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                          int pw, int ph, Timer* timers,
                          DMAController* dma, InterruptController* ic,
                          XRayState* state) {
    (void)pw;
    (void)ph;

    int x0 = px + 8;
    int y = py + 18;

    /* === TIMERS === */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "TIMERS", XRAY_COL_HEADER);
    y += 12;

    /* Header row */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "#", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 24, y, "Counter", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 104, y, "Reload", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 176, y, "Pre", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 220, y, "Casc", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 268, y, "IRQ", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 308, y, "En", XRAY_COL_DIM);
    y += 12;

    for (int i = 0; i < 4; i++) {
        Timer* t = &timers[i];
        uint32_t col = t->enabled ? XRAY_COL_VALUE : XRAY_COL_DIM;
        uint32_t fc = flash_color(state->timer_flash[i]);

        /* Flash indicator dot */
        if (state->timer_flash[i] > 0) {
            xray_draw_rect(buf, buf_w, buf_h, x0 - 6, y + 1, 4, 6, fc);
        }

        xray_draw_textf(buf, buf_w, buf_h, x0, y, col, "%d", i);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 24, y, col, "%04X",
                        t->counter);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 104, y, col, "%04X",
                        t->reload);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 176, y, col, "%4d",
                        t->prescaler);
        xray_draw_text(buf, buf_w, buf_h, x0 + 220, y,
                       t->cascade ? "Yes" : " No", col);
        xray_draw_text(buf, buf_w, buf_h, x0 + 268, y,
                       t->irq_enable ? "Yes" : " No", col);
        xray_draw_text(buf, buf_w, buf_h, x0 + 308, y,
                       t->enabled ? "ON" : "--", col);
        y += 11;
    }

    y += 6;

    /* === DMA === */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "DMA", XRAY_COL_HEADER);
    y += 12;

    /* Header */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "#", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 24, y, "Source", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 112, y, "Dest", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 200, y, "Count", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 264, y, "Timing", XRAY_COL_DIM);
    xray_draw_text(buf, buf_w, buf_h, x0 + 320, y, "En", XRAY_COL_DIM);
    y += 12;

    for (int i = 0; i < 4; i++) {
        DMAChannel* dc = &dma->channels[i];
        uint32_t col = dc->enabled ? XRAY_COL_VALUE : XRAY_COL_DIM;
        uint32_t fc = flash_color(state->dma_flash[i]);

        if (state->dma_flash[i] > 0) {
            xray_draw_rect(buf, buf_w, buf_h, x0 - 6, y + 1, 4, 6, fc);
        }

        xray_draw_textf(buf, buf_w, buf_h, x0, y, col, "%d", i);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 24, y, col, "%08X",
                        dc->source);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 112, y, col, "%08X",
                        dc->dest);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 200, y, col, "%04X",
                        dc->count);
        xray_draw_text(buf, buf_w, buf_h, x0 + 264, y,
                       dma_timing_name(dc->timing), col);
        xray_draw_text(buf, buf_w, buf_h, x0 + 320, y,
                       dc->enabled ? "ON" : "--", col);
        y += 11;
    }

    y += 6;

    /* === IRQ === */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "INTERRUPTS", XRAY_COL_HEADER);
    y += 12;

    /* IME */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "IME:", XRAY_COL_LABEL);
    xray_draw_text(buf, buf_w, buf_h, x0 + 40, y,
                   ic->ime ? "ON" : "OFF",
                   ic->ime ? XRAY_COL_VALUE : XRAY_COL_DIM);
    y += 12;

    /* IE and IF as named bit flags */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "IE/IF", XRAY_COL_LABEL);
    y += 12;

    /* Two rows of 7 IRQ sources each */
    for (int row = 0; row < 2; row++) {
        int col_x = x0;
        int start = row * 7;
        int end = start + 7;
        if (end > 14) end = 14;

        for (int i = start; i < end; i++) {
            bool ie_set = (ic->ie >> i) & 1;
            bool if_set = (ic->irf >> i) & 1;

            uint32_t col;
            if (if_set && ie_set) {
                col = flash_color(state->irq_flash[i]);
                if (state->irq_flash[i] == 0)
                    col = XRAY_COL_FLASH;
            } else if (ie_set) {
                col = XRAY_COL_VALUE;
            } else {
                col = XRAY_COL_DIM;
            }

            /* IRQ name (truncated to 6 chars for space) */
            char label[8];
            snprintf(label, sizeof(label), "%.6s", irq_names[i]);
            xray_draw_text(buf, buf_w, buf_h, col_x, y, label, col);

            /* Pending indicator */
            if (if_set) {
                xray_draw_rect(buf, buf_w, buf_h, col_x + 48, y + 1, 4, 6,
                               XRAY_COL_FLASH);
            }

            col_x += 56;
        }
        y += 11;
    }
}
