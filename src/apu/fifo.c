#include "apu.h"

// FIFO helper functions are implemented in apu.c (apu_fifo_write, apu_fifo_pop)
// This file can hold additional FIFO-specific logic if needed.

void fifo_reset(FIFO* fifo) {
    fifo->read_idx = 0;
    fifo->write_idx = 0;
    fifo->count = 0;
    /* Do NOT clear last_sample — hardware DAC holds its last output value
     * even after FIFO flush. Buffer contents are dead once count == 0. */
}
