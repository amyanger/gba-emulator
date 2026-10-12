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
/* "Rare Candy", "Gives TM36", "Gives Mach Bike / Acro Bike", "Feebas", "Feebas · bridge",
 * "Feebas · can't fish". */
void overworld_marker_text(const OwMarker* mk, char* out, size_t out_size);

typedef enum { OW_LABEL_TRAINER, OW_LABEL_MARKER, OW_LABEL_HINT } OwLabelSource;
typedef struct {
    uint8_t source;    /* OwLabelSource */
    uint8_t index;     /* into trainers[] or markers[]; 0 for the hint */
    UiRect rect;       /* points */
} OwLabel;
#define OW_MAX_LABELS (OW_MAX_TRAINERS + OW_MAX_MARKERS + 1)

/* Places every label (the Feebas hint, trainers, Feebas tiles, gifts, then items), each
 * above, left, right or below its object, avoiding placed labels, cones and Feebas tiles,
 * inside the canvas. The hint sits on the screen edge toward its target.
 * Returns how many were written to out (OW_MAX_LABELS at most). */
uint32_t overworld_label_rects(const UiCanvas* c, const OverworldSnapshot* s, float gba_pt, OwLabel* out);

#endif // OVERWORLD_DRAW_H
