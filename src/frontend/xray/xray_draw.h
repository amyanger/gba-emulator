#ifndef XRAY_DRAW_H
#define XRAY_DRAW_H

#include <stdint.h>
#include <stdarg.h>

/* Drawing context: a uint32_t ARGB8888 pixel buffer with known dimensions. */

/* Draw a single pixel (bounds-checked). */
void xray_draw_pixel(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                     uint32_t color);

/* Draw a filled rectangle. */
void xray_draw_rect(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                    int w, int h, uint32_t color);

/* Draw a rectangle outline (1px border). */
void xray_draw_rect_outline(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                            int w, int h, uint32_t color);

/* Draw a horizontal line. */
void xray_draw_hline(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                     int len, uint32_t color);

/* Draw a vertical line. */
void xray_draw_vline(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                     int len, uint32_t color);

/* Draw a single 8x8 character. Returns x + 8. */
int xray_draw_char(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                   char ch, uint32_t color);

/* Draw a null-terminated string. Returns final x. */
int xray_draw_text(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                   const char* str, uint32_t color);

/* Draw formatted text (printf-style). Returns final x. */
int xray_draw_textf(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                    uint32_t color, const char* fmt, ...);

/* Draw a horizontal fill bar (e.g., FIFO level). */
void xray_draw_fill_bar(uint32_t* buf, int buf_w, int buf_h, int x, int y,
                        int bar_w, int bar_h, float fill, uint32_t fg,
                        uint32_t bg);

/* Blit a GBA 15-bit (BGR555) buffer to ARGB8888, scaling by integer factor.
 * src_w/src_h = source dimensions, dst_x/dst_y = position in buf,
 * scale = integer scale factor (1 = pixel-perfect). */
void xray_blit_gba(uint32_t* buf, int buf_w, int buf_h, int dst_x, int dst_y,
                   const uint16_t* src, int src_w, int src_h, int scale);

/* Convert a single GBA BGR555 color to ARGB8888. */
static inline uint32_t gba_to_argb(uint16_t gba_color) {
    uint32_t r = (gba_color & 0x1F) << 3;
    uint32_t g = ((gba_color >> 5) & 0x1F) << 3;
    uint32_t b = ((gba_color >> 10) & 0x1F) << 3;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

#endif /* XRAY_DRAW_H */
