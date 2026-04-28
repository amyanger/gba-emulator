#ifndef FRAME_ADVANCE_H
#define FRAME_ADVANCE_H

#include "frontend.h"

/* Result enum so the test can assert outcomes without poking Frontend
 * directly (keeps the test pure logic). */
typedef enum {
    FRAME_ADVANCE_NOOP,
    FRAME_ADVANCE_STEPPED,
} FrameAdvanceOutcome;

/* Called on `\` keydown. Returns FRAME_ADVANCE_STEPPED if a single-step
 * frame was queued; FRAME_ADVANCE_NOOP otherwise (e.g. fast-forward or
 * rewind active). */
FrameAdvanceOutcome frame_advance_request(Frontend* fe);

#endif /* FRAME_ADVANCE_H */
