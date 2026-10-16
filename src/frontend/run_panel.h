#ifndef RUN_PANEL_H
#define RUN_PANEL_H

#include <stdbool.h>
#include <stdint.h>
#include "frontend/run_session.h"
#include "game/badge_pic.h"
#include "ui/ui_canvas.h"

/* The Run page of the Pokemon Companion panel: the dashboard, or the Details sheet
 * when details is set. top is the y (points) below the tab bar. v and art may be NULL;
 * without art the badge strip falls back to a dot. */
void run_panel_draw(UiCanvas* c, const RunView* v, float top, uint8_t scroll, bool details,
                    const BadgeArt* art, GameRegion region);
/* Max scroll for the current view: feed rows (dashboard) or team rows (details) beyond what fits. */
uint8_t run_panel_max_scroll(const RunView* v, bool details);

#endif // RUN_PANEL_H
