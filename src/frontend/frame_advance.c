#include "frame_advance.h"

FrameAdvanceOutcome frame_advance_request(Frontend* fe) {
    if (!fe) return FRAME_ADVANCE_NOOP;
    if (fe->ff_hold || fe->ff_toggle) return FRAME_ADVANCE_NOOP;
#ifdef ENABLE_REWIND
    if (fe->rewind_hold) return FRAME_ADVANCE_NOOP;
#endif
    fe->paused = true;
    fe->step_pending = true;
    return FRAME_ADVANCE_STEPPED;
}
