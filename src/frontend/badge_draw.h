#ifndef BADGE_DRAW_H
#define BADGE_DRAW_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_profile.h"
#include "ui/ui_canvas.h"

/* Presentation colour of gym badge i (0..7, FLAG_BADGE01..08 order) as 0xAARRGGBB. */
uint32_t badge_color(GameRegion r, int i);

/* Draws one badge from its 16x16 palette-index pixels, centred on the box pt square at (x, y).
 * Earned badges are tinted in the badge's colour, missing ones are a flat dark silhouette.
 * Each ROM pixel is a whole number of device pixels so it stays crisp at any density. */
void badge_draw(UiCanvas* c, float x, float y, float box, const uint8_t px[256], GameRegion r, int i,
                bool have);

#endif // BADGE_DRAW_H
