#ifndef OVERWORLD_DRAW_H
#define OVERWORLD_DRAW_H

#include <stddef.h>
#include "ui/ui_canvas.h"
#include "game/overworld.h"

/* Paints an OverworldSnapshot over the game image. Pure C, no SDL.
 * gba_pt = canvas points per GBA pixel. Does not clear: the frontend clears a
 * transparent canvas first; the headless PNG draws onto the game image. */
void overworld_draw(UiCanvas* c, const OverworldSnapshot* s, float gba_pt);
/* "Youngster · 4 tiles", "Trainer · 1 tile", "Beaten". */
void overworld_pill_text(const OwTrainer* t, char* out, size_t out_size);
/* The trainer's pill in points: above the head, under the feet when there is no
 * room, always inside the canvas. */
UiRect overworld_pill_rect(const UiCanvas* c, const OwTrainer* t, float gba_pt);

#endif // OVERWORLD_DRAW_H
