#ifndef RUN_PANEL_H
#define RUN_PANEL_H

#include <stdbool.h>
#include <stdint.h>
#include "frontend/run_session.h"
#include "ui/ui_canvas.h"

/* The Run page of the Emerald Companion panel: the dashboard, or the Details sheet
 * when details is set. top is the y (points) below the tab bar. v may be NULL. */
void run_panel_draw(UiCanvas* c, const RunView* v, float top, uint8_t scroll, bool details);
/* Max scroll for the current view: feed rows (dashboard) or team rows (details) beyond what fits. */
uint8_t run_panel_max_scroll(const RunView* v, bool details);

#endif // RUN_PANEL_H
