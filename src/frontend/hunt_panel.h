#ifndef HUNT_PANEL_H
#define HUNT_PANEL_H

#include "frontend/hunt_session.h"
#include "ui/ui_canvas.h"

/* The Hunt page of the Emerald Companion panel. top is the y (points) below the tab bar.
 * v may be NULL. */
void hunt_panel_draw(UiCanvas* c, const HuntView* v, float top);
/* 18402 -> "18,402" (the page and the shiny toast). */
void hunt_panel_number(uint32_t n, char* out, size_t size);

#endif // HUNT_PANEL_H
