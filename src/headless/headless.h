#ifndef HEADLESS_HEADLESS_H
#define HEADLESS_HEADLESS_H

#include "gba.h"
#include "headless/input_script.h"
#include <stdio.h>

/* Run `frames` frames on `gba` via gba_run_frame, writing one line per frame to
   `hash_out` in the format "%d %08x\n": 0-based frame index, a space, and 8
   lowercase hex digits of the FNV-1a framebuffer hash.

   If `script` is non-NULL, its events for frame i are applied to gba->input
   before frame i runs. If `before_frame` is non-NULL it is called after that,
   just before the frame runs; if `on_frame` is non-NULL it is called after each
   frame. Both get `ctx`.

   Returns 0 on success, non-zero on I/O failure (e.g. fprintf returns negative)
   or if `frames` is negative.

   The caller owns `hash_out` — it opens and closes the stream. This function
   does not flush; call fflush or close to force the writes to disk. */
int headless_run(GBA* gba, int frames, FILE* hash_out, InputScript* script,
                 void (*before_frame)(GBA* gba, void* ctx), void (*on_frame)(GBA* gba, void* ctx),
                 void* ctx);

#endif // HEADLESS_HEADLESS_H
