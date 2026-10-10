#ifndef UI_STB_H
#define UI_STB_H

#include <stddef.h>
#include "ui/stb_truetype.h"

/* stb_truetype allocates scratch memory while rasterizing. It is routed to a
 * fixed static arena so the UI never touches the heap. Reset it after each
 * glyph; requests that do not fit return NULL, which stb_truetype handles. */
void* ui_stb_arena_alloc(size_t n);
void ui_stb_arena_reset(void);

#endif // UI_STB_H
