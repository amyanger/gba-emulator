#ifndef PANEL_LAYOUT_H
#define PANEL_LAYOUT_H

#include <stdbool.h>

#define PANEL_W_PT 600
#define PANEL_H_PT 480

typedef struct { int x, y, w, h; } PxRect;

typedef struct {
    int game_scale;        /* --scale, 1..10 */
    bool panel_visible;
    float pt_to_px;        /* drawable pixels per window unit (2.0 on Retina, 1.0 otherwise) */
    float os_scale;        /* extra scale where the OS reports it as DPI only (X11), else 1.0 */
    int usable_w_pt, usable_h_pt;   /* display usable area in window units; 0 = unknown */
} LayoutInput;

typedef struct {
    float density;         /* pixels per UI point for the panel */
    int game_px_scale;     /* integer pixels per GBA pixel */
    int window_w, window_h;/* requested window size, window units */
} LayoutPlan;

/* Window size and densities for a windowed (non-fullscreen) window. */
LayoutPlan panel_layout_plan(const LayoutInput* in);

/* Rects inside an actual drawable of out_w x out_h pixels (windowed or fullscreen):
 * game integer-scaled (grown to fill in fullscreen), panel at plan density, content centered. */
void panel_layout_rects(const LayoutPlan* plan, bool panel_visible, int out_w, int out_h,
                        PxRect* game, PxRect* panel);

#endif // PANEL_LAYOUT_H
