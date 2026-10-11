#include "frontend/frame_pacer.h"

uint64_t frame_pacer_period(uint64_t ticks_per_sec, uint32_t speed) {
    if (speed == 0) speed = 1;
    return (uint64_t)((double)ticks_per_sec / (FRAME_PACER_FPS * (double)speed) + 0.5);
}

void frame_pacer_reset(FramePacer* p) { p->next = 0; }

uint64_t frame_pacer_wait(FramePacer* p, uint64_t now, uint64_t period) {
    if (p->next == 0 || now > p->next + 3 * period) p->next = now;
    p->next += period;
    return p->next > now ? p->next - now : 0;
}
