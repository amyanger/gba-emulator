#ifndef HUD_DRAW_H
#define HUD_DRAW_H

#include <stdbool.h>
#include <stdint.h>
#include "ui/ui_canvas.h"
#include "frontend/toast.h"

/* Lays out and paints toasts (top center) and status badges (top left) onto a
 * transparent canvas covering the top strip of the game area. Pure C, no SDL. */

typedef struct { float x, y, w, h; } HudBox;   /* points */
typedef struct {
    HudBox badges[BADGE_COUNT];   /* w = 0 when the badge is off */
    HudBox toasts[TOAST_CAP];     /* y includes the slide offset; w = 0 when hidden */
    float title_w[TOAST_CAP];     /* width allotted to the title */
    float detail_w[TOAST_CAP];    /* 0 = detail hidden */
    bool below_badges;            /* stack moved down to clear the badge row */
} HudLayout;

float hud_strip_height_pt(void);                     /* tallest the HUD can be */
bool  hud_visible(const ToastQueue* q);              /* any toast or badge */
void  hud_layout(const UiCanvas* c, const ToastQueue* q, uint32_t now_ms, float area_w_pt,
                 HudLayout* out);
/* Sets c->keep_alpha, clears to transparent, draws badges and toasts. */
void  hud_draw(UiCanvas* c, const ToastQueue* q, uint32_t now_ms);

#endif // HUD_DRAW_H
