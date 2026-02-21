#include "apu.h"

void apu_init(APU* apu) {
    memset(apu, 0, sizeof(APU));
    apu->soundbias = 0x200; // Default bias
}

void apu_tick(APU* apu, int cycles) {
    // TODO: Advance frame sequencer
    // TODO: Tick legacy channels
    // TODO: Mix all channels and write to sample buffer
}

void apu_fifo_write(APU* apu, int fifo_id, uint32_t data) {
    FIFO* fifo = (fifo_id == 0) ? &apu->fifo_a : &apu->fifo_b;

    // Write 4 bytes (samples) to FIFO
    for (int i = 0; i < 4; i++) {
        if (fifo->count < FIFO_SIZE) {
            fifo->buffer[fifo->write_idx] = (int8_t)(data >> (i * 8));
            fifo->write_idx = (fifo->write_idx + 1) % FIFO_SIZE;
            fifo->count++;
        }
    }
}

int8_t apu_fifo_pop(APU* apu, int fifo_id) {
    FIFO* fifo = (fifo_id == 0) ? &apu->fifo_a : &apu->fifo_b;

    if (fifo->count == 0) return 0;

    int8_t sample = fifo->buffer[fifo->read_idx];
    fifo->read_idx = (fifo->read_idx + 1) % FIFO_SIZE;
    fifo->count--;
    return sample;
}

void apu_on_timer_overflow(APU* apu, int timer_id) {
    // Check which FIFOs are attached to this timer
    if (apu->fifo_a.timer_id == timer_id) {
        apu_fifo_pop(apu, 0);
        // TODO: If FIFO count drops below threshold, signal DMA refill
    }
    if (apu->fifo_b.timer_id == timer_id) {
        apu_fifo_pop(apu, 1);
    }
}
