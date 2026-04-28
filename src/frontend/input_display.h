#ifndef INPUT_DISPLAY_H
#define INPUT_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/* Pull in Frontend definition (which also forward-declares GBA). */
#include "frontend/frontend.h"

/* Pure: convert a KEYINPUT register value (active-low) to a "held" mask
 * with the same bit layout as KEYINPUT. */
uint16_t input_display_held_mask(uint16_t keyinput);

/* Compute the panel anchor (top-left corner) given screen size and
 * panel size. Clamps to (0, 0) for tiny screens. */
void input_display_anchor(int screen_w, int screen_h,
                          int panel_w, int panel_h,
                          int* out_x, int* out_y);

/* Draw the HUD into fe->overlay_buffer for the given GBA's KEYINPUT.
 * No-op if !fe->input_display_enabled. Sets fe->overlay_dirty when it
 * draws. Caller must have called frontend_overlay_clear() earlier in
 * the frame so the buffer starts at all-zeros. */
void input_display_render(Frontend* fe, GBA* gba);

#endif /* INPUT_DISPLAY_H */
