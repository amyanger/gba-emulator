#include "xray.h"
#include "timer/timer.h"
#include "memory/dma.h"
#include "interrupt/interrupt.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <stdio.h>

static const char* irq_names[14] = {
    "VBlank", "HBlank", "VCount", "Timer 0", "Timer 1", "Timer 2", "Timer 3",
    "Serial", "DMA 0", "DMA 1", "DMA 2", "DMA 3", "Keypad", "Cartridge"
};

#define IRQ_LISTENING 0xFF3B5BB5u
#define IME_ON        0xFF1E3A2Eu

/* Fading warm dot for "this just fired". */
static void flash_dot(UiCanvas* c, float x, float y, uint8_t flash) {
    if (!flash) return;
    uint32_t a = (uint32_t)flash * 255 / XRAY_FLASH_FRAMES;
    ui_fill_round_rect(c, (UiRect){x - 3.0f, y + 4.0f, 6.0f, 6.0f}, 3.0f,
                       (a << 24) | (UI_WARN & 0x00FFFFFFu));
}

static void head(UiCanvas* c, float x, float y, UiAlign a, const char* t) {
    ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_HEAD, x, y, 0, a, UI_DIM, t);
}

static void cell(UiCanvas* c, float x, float y, UiAlign a, uint32_t col, const char* t) {
    ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x, y, 0, a, col, t);
}

static const char* dma_target(uint32_t dest) {
    if (dest == 0x040000A0u) return "Sound FIFO A";
    if (dest == 0x040000A4u) return "Sound FIFO B";
    return NULL;
}

/* Timing 3 means something different per channel (GBATEK DMAxCNT_H bits 12-13). */
static const char* dma_when(int ch, uint8_t timing) {
    switch (timing) {
    case 0:  return "Right away";
    case 1:  return "Each frame (VBlank)";
    case 2:  return "Each line (HBlank)";
    default: return ch == 3 ? "Video capture" : (ch == 0 ? "Special" : "Sound refill");
    }
}

static void timers_table(UiCanvas* c, float x0, float top, Timer* timers, const XRayState* s) {
    char buf[16];
    ui_section_label(c, x0, top, "TIMERS");
    float y = top + 16.0f;
    head(c, x0, y, UI_ALIGN_LEFT, "#");
    head(c, x0 + 80.0f, y, UI_ALIGN_RIGHT, "COUNT");
    head(c, x0 + 150.0f, y, UI_ALIGN_RIGHT, "RELOAD");
    head(c, x0 + 205.0f, y, UI_ALIGN_RIGHT, "TICK");
    head(c, x0 + 225.0f, y, UI_ALIGN_LEFT, "CHAINED");
    head(c, x0 + 290.0f, y, UI_ALIGN_LEFT, "IRQ");
    y += XRAY_ROW;
    for (int i = 0; i < 4; i++, y += XRAY_ROW) {
        Timer* t = &timers[i];
        uint32_t col = t->enabled ? UI_TEXT : UI_FAINT;
        flash_dot(c, x0 - 7.0f, y, s->timer_flash[i]);
        snprintf(buf, sizeof(buf), "%d", i);
        cell(c, x0, y, UI_ALIGN_LEFT, col, buf);
        snprintf(buf, sizeof(buf), "%04X", (unsigned)t->counter);
        cell(c, x0 + 80.0f, y, UI_ALIGN_RIGHT, col, buf);
        snprintf(buf, sizeof(buf), "%04X", (unsigned)t->reload);
        cell(c, x0 + 150.0f, y, UI_ALIGN_RIGHT, col, buf);
        snprintf(buf, sizeof(buf), "%u", (unsigned)t->prescaler);
        cell(c, x0 + 205.0f, y, UI_ALIGN_RIGHT, col, buf);
        cell(c, x0 + 225.0f, y, UI_ALIGN_LEFT, col, t->cascade ? "Yes" : "No");
        cell(c, x0 + 290.0f, y, UI_ALIGN_LEFT, col, t->irq_enable ? "Yes" : "No");
    }
}

static void dma_table(UiCanvas* c, float dx, float top, DMAController* dma, const XRayState* s) {
    char buf[16];
    ui_section_label(c, dx, top, "DMA (MEMORY COPIES)");
    float y = top + 16.0f;
    head(c, dx, y, UI_ALIGN_LEFT, "#");
    head(c, dx + 18.0f, y, UI_ALIGN_LEFT, "FROM");
    head(c, dx + 98.0f, y, UI_ALIGN_LEFT, "TO");
    head(c, dx + 290.0f, y, UI_ALIGN_RIGHT, "AMOUNT");
    head(c, dx + 302.0f, y, UI_ALIGN_LEFT, "WHEN");
    y += XRAY_ROW;
    for (int i = 0; i < 4; i++, y += XRAY_ROW) {
        DMAChannel* dc = &dma->channels[i];
        uint32_t col = dc->enabled ? UI_TEXT : UI_FAINT;
        flash_dot(c, dx - 7.0f, y, s->dma_flash[i]);
        snprintf(buf, sizeof(buf), "%d", i);
        cell(c, dx, y, UI_ALIGN_LEFT, col, buf);
        snprintf(buf, sizeof(buf), "%08X", (unsigned)dc->source);
        cell(c, dx + 18.0f, y, UI_ALIGN_LEFT, col, buf);
        const char* named = dma_target(dc->dest);
        if (!named) {
            snprintf(buf, sizeof(buf), "%08X", (unsigned)dc->dest);
            named = buf;
        }
        cell(c, dx + 98.0f, y, UI_ALIGN_LEFT, col, named);
        snprintf(buf, sizeof(buf), "%u", (unsigned)dc->count);
        cell(c, dx + 290.0f, y, UI_ALIGN_RIGHT, col, buf);
        ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, dx + 302.0f, y, 138.0f, UI_ALIGN_LEFT, col,
                dma_when(i, dc->timing));
    }
}

static void irq_pills(UiCanvas* c, float ix, float iw, float top, InterruptController* ic,
                      const XRayState* s) {
    ui_section_label(c, ix, top, "INTERRUPTS");
    float px = ix, py = top + 16.0f;
    px += ui_pill(c, px, py, ic->ime ? "Master switch on" : "Master switch off",
                  ic->ime ? IME_ON : UI_TRACK) + 5.0f;
    for (int i = 0; i < 14; i++) {
        bool ie = (ic->ie >> i) & 1, pending = ie && ((ic->irf >> i) & 1);
        uint32_t bg = (s->irq_flash[i] || pending) ? UI_WARN : ie ? IRQ_LISTENING : UI_TRACK;
        float w = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, irq_names[i]) + 14.0f;
        if (px + w > ix + iw) {
            px = ix;
            py += 20.0f;
        }
        px += ui_pill(c, px, py, irq_names[i], bg) + 5.0f;
    }
    ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_HEAD, ix, py + 24.0f, iw, UI_ALIGN_LEFT, UI_DIM,
            "Blue = listening, orange = just fired");
}

void xray_render_activity(UiCanvas* c, Timer* timers, DMAController* dma,
                          InterruptController* ic, XRayState* s) {
    float top = xray_card(c, XRAY_CARD_SCENES, NULL);
    float x0 = xray_cards[XRAY_CARD_SCENES].x + XRAY_PAD;
    timers_table(c, x0, top, timers, s);
    dma_table(c, x0 + 370.0f, top, dma, s);
    irq_pills(c, x0 + 840.0f, 308.0f, top, ic, s);
}
