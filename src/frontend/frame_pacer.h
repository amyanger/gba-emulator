#ifndef FRAME_PACER_H
#define FRAME_PACER_H

#include <stdint.h>

#define FRAME_PACER_FPS 59.7275

typedef struct {
    uint64_t next; /* next = 0: not started */
} FramePacer;

/* Ticks per frame at speed x real time (speed >= 1). */
uint64_t frame_pacer_period(uint64_t ticks_per_sec, uint32_t speed);
/* Advances the deadline by one period and returns how many ticks to wait from now
 * (0 when late). More than 3 periods behind resyncs to now: no catch-up burst. */
uint64_t frame_pacer_wait(FramePacer* p, uint64_t now, uint64_t period);
void frame_pacer_reset(FramePacer* p);

#endif // FRAME_PACER_H
