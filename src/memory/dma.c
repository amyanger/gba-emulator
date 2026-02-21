#include "dma.h"
#include "bus.h"

void dma_init(DMAController* dma) {
    memset(dma->channels, 0, sizeof(dma->channels));
    dma->active_channel = -1;
}

void dma_write_control(DMAController* dma, int ch, uint16_t val) {
    DMAChannel* dc = &dma->channels[ch];
    bool was_enabled = dc->enabled;

    dc->control = val;
    dc->dest_adjust = (val >> 5) & 3;
    dc->src_adjust = (val >> 7) & 3;
    dc->repeat = BIT(val, 9);
    dc->transfer_32 = BIT(val, 10);
    dc->timing = (val >> 12) & 3;
    dc->irq_on_done = BIT(val, 14);
    dc->enabled = BIT(val, 15);

    // On rising edge of enable, latch source/dest
    if (!was_enabled && dc->enabled) {
        dc->source = dc->source_latch;
        dc->dest = dc->dest_latch;

        if (dc->timing == 0) {
            // Immediate — execute now
            dma_execute(dma, ch);
        }
    }
}

void dma_on_vblank(DMAController* dma) {
    for (int i = 0; i < 4; i++) {
        if (dma->channels[i].enabled && dma->channels[i].timing == 1) {
            dma_execute(dma, i);
        }
    }
}

void dma_on_hblank(DMAController* dma) {
    for (int i = 0; i < 4; i++) {
        if (dma->channels[i].enabled && dma->channels[i].timing == 2) {
            dma_execute(dma, i);
        }
    }
}

void dma_on_fifo(DMAController* dma, int fifo_id) {
    // Channels 1 and 2 handle FIFO refill (special timing = 3)
    int ch = fifo_id + 1; // FIFO A -> DMA1, FIFO B -> DMA2
    if (ch < 4 && dma->channels[ch].enabled && dma->channels[ch].timing == 3) {
        dma_execute(dma, ch);
    }
}

int dma_execute(DMAController* dma, int ch) {
    // TODO: Implement actual DMA transfer via bus
    // Transfer count words from source to dest
    // Apply address adjustments
    // Fire IRQ if enabled
    return 0;
}
