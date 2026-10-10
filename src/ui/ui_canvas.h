#ifndef UI_CANVAS_H
#define UI_CANVAS_H

#include <stdint.h>

typedef struct { float x, y, w, h; } UiRect;                 /* points */
typedef struct {
    uint32_t* px; int w, h;   /* ARGB8888, opaque background assumed */
    float scale;              /* pixels per point */
    int cx0, cy0, cx1, cy1;   /* clip, pixels, half-open */
} UiCanvas;
void ui_canvas_init(UiCanvas* c, uint32_t* px, int w, int h, float scale);
void ui_canvas_clip(UiCanvas* c, UiRect r);    /* intersects with the buffer */
void ui_canvas_unclip(UiCanvas* c);
void ui_canvas_clear(UiCanvas* c, uint32_t argb);
void ui_fill_rect(UiCanvas* c, UiRect r, uint32_t argb);           /* edges snapped to pixels */
void ui_fill_round_rect(UiCanvas* c, UiRect r, float radius, uint32_t argb);
void ui_hline(UiCanvas* c, float x, float y, float w, uint32_t argb); /* 1 pixel thick */
void ui_ring(UiCanvas* c, float cx, float cy, float r, float thickness, uint32_t argb);
void ui_capsule(UiCanvas* c, float x0, float y0, float x1, float y1, float thickness, uint32_t argb);
void ui_blend_coverage(UiCanvas* c, int x, int y, uint32_t argb, uint32_t coverage /*0..255*/);
static inline int ui_px(const UiCanvas* c, float pts) { return (int)(pts * c->scale + 0.5f); }

#endif // UI_CANVAS_H
