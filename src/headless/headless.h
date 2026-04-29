#ifndef HEADLESS_HEADLESS_H
#define HEADLESS_HEADLESS_H

#include "gba.h"
#include <stdio.h>

/* Run `frames` frames on `gba` via gba_run_frame, writing one line per frame to
   `hash_out` in the format "%d %08x\n": 0-based frame index, a space, and 8
   lowercase hex digits of the FNV-1a framebuffer hash.

   Returns 0 on success, non-zero on I/O failure (e.g. fprintf returns negative)
   or if `frames` is negative.

   The caller owns `hash_out` — it opens and closes the stream. This function
   does not flush; call fflush or close to force the writes to disk. */
int headless_run(GBA* gba, int frames, FILE* hash_out);

#endif // HEADLESS_HEADLESS_H
