#include "xray_draw.h"
#include "xray_font.h"
#include <stdio.h>
#include <string.h>

void xray_draw_pixel(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                     uint32_t color) {
    if (x >= 0 && x < buf_w && y >= 0 && y < buf_h) {
        buf[y * buf_w + x] = color;
    }
}

void xray_draw_rect(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                    int w, int h, uint32_t color) {
    /* Clip to buffer bounds */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w) > buf_w ? buf_w : (x + w);
    int y1 = (y + h) > buf_h ? buf_h : (y + h);

    for (int row = y0; row < y1; row++) {
        uint32_t* dst = &buf[row * buf_w + x0];
        for (int col = x0; col < x1; col++) {
            *dst++ = color;
        }
    }
}

void xray_draw_rect_outline(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                            int w, int h, uint32_t color) {
    xray_draw_hline(buf, buf_w, buf_h, x, y, w, color);
    xray_draw_hline(buf, buf_w, buf_h, x, y + h - 1, w, color);
    xray_draw_vline(buf, buf_w, buf_h, x, y, h, color);
    xray_draw_vline(buf, buf_w, buf_h, x + w - 1, y, h, color);
}

void xray_draw_hline(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                     int len, uint32_t color) {
    if (y < 0 || y >= buf_h) return;
    int x0 = x < 0 ? 0 : x;
    int x1 = (x + len) > buf_w ? buf_w : (x + len);
    uint32_t* dst = &buf[y * buf_w + x0];
    for (int i = x0; i < x1; i++) {
        *dst++ = color;
    }
}

void xray_draw_vline(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                     int len, uint32_t color) {
    if (x < 0 || x >= buf_w) return;
    int y0 = y < 0 ? 0 : y;
    int y1 = (y + len) > buf_h ? buf_h : (y + len);
    for (int row = y0; row < y1; row++) {
        buf[row * buf_w + x] = color;
    }
}

int xray_draw_char(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                   char ch, uint32_t color) {
    /* Map printable ASCII to font table index */
    int idx = (int)ch - 0x20;
    if (idx < 0 || idx >= 95) return x + 8;

    const uint8_t* glyph = xray_font_data[idx];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        int py = y + row;
        if (py < 0 || py >= buf_h) continue;
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                if (px >= 0 && px < buf_w) {
                    buf[py * buf_w + px] = color;
                }
            }
        }
    }
    return x + 8;
}

int xray_draw_text(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                   const char* str, uint32_t color) {
    int cx = x;
    while (*str) {
        cx = xray_draw_char(buf, buf_w, buf_h, cx, y, *str, color);
        str++;
    }
    return cx;
}

int xray_draw_textf(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                    uint32_t color, const char* fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    return xray_draw_text(buf, buf_w, buf_h, x, y, tmp, color);
}

void xray_draw_fill_bar(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                        int bar_w, int bar_h, float fill, uint32_t fg,
                        uint32_t bg) {
    /* Background */
    xray_draw_rect(buf, buf_w, buf_h, x, y, bar_w, bar_h, bg);
    /* Filled portion */
    if (fill < 0.0f) fill = 0.0f;
    if (fill > 1.0f) fill = 1.0f;
    int filled_w = (int)(fill * bar_w);
    if (filled_w > 0) {
        xray_draw_rect(buf, buf_w, buf_h, x, y, filled_w, bar_h, fg);
    }
    /* Outline */
    xray_draw_rect_outline(buf, buf_w, buf_h, x, y, bar_w, bar_h,
                           0xFF556677);
}

void xray_blit_gba(uint32_t* buf, int buf_w, int buf_h, int dst_x, int dst_y,
                   const uint16_t* src, int src_w, int src_h, int scale) {
    for (int sy = 0; sy < src_h; sy++) {
        for (int sx = 0; sx < src_w; sx++) {
            uint32_t argb = gba_to_argb(src[sy * src_w + sx]);
            for (int dy = 0; dy < scale; dy++) {
                int py = dst_y + sy * scale + dy;
                if (py < 0 || py >= buf_h) continue;
                for (int dx = 0; dx < scale; dx++) {
                    int px = dst_x + sx * scale + dx;
                    if (px >= 0 && px < buf_w) {
                        buf[py * buf_w + px] = argb;
                    }
                }
            }
        }
    }
}
