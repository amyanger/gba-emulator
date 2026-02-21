#include "dma.h"
#include "bus.h"
#include "interrupt/interrupt.h"

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
    DMAChannel* dc = &dma->channels[ch];
    Bus* bus = dma->bus;

    if (!bus || !dc->enabled) return 0;

    dma->active_channel = ch;

    // Determine transfer count.
    // When count is 0, use the maximum: 0x4000 for channels 0-2, 0x10000 for ch 3.
    uint32_t count = dc->count;
    if (count == 0) {
        count = (ch == 3) ? 0x10000 : 0x4000;
    }

    // FIFO special case (timing == 3): force 4 words, 32-bit, dest fixed
    bool is_fifo = (dc->timing == 3) && (ch == 1 || ch == 2);
    if (is_fifo) {
        count = 4;
    }

    // Transfer width: 16-bit or 32-bit
    bool use_32 = is_fifo ? true : dc->transfer_32;
    uint32_t step = use_32 ? 4 : 2;

    // Address masking per GBATEK:
    // DMA0 SAD: 27 bits. DMA1-3 SAD: 28 bits.
    // DMA0-2 DAD: 27 bits. DMA3 DAD: 28 bits.
    uint32_t src_mask = (ch == 0) ? 0x07FFFFFF : 0x0FFFFFFF;
    uint32_t dst_mask = (ch == 3) ? 0x0FFFFFFF : 0x07FFFFFF;
    dc->source &= src_mask;
    dc->dest &= dst_mask;

    // Execute the transfer
    for (uint32_t i = 0; i < count; i++) {
        if (use_32) {
            uint32_t val = bus_read32(bus, dc->source);
            bus_write32(bus, dc->dest, val);
        } else {
            uint16_t val = bus_read16(bus, dc->source);
            bus_write16(bus, dc->dest, val);
        }

        // Adjust source address
        // 0=increment, 1=decrement, 2=fixed, 3=prohibited (treat as fixed)
        switch (dc->src_adjust) {
        case 0: dc->source += step; break;
        case 1: dc->source -= step; break;
        case 2: break; // fixed
        case 3: break; // prohibited, treat as fixed
        }

        // Adjust destination address
        // 0=increment, 1=decrement, 2=fixed, 3=increment+reload
        // For FIFO, destination is always fixed (the FIFO register)
        if (is_fifo) {
            // FIFO: dest stays fixed, source increments
        } else {
            switch (dc->dest_adjust) {
            case 0: dc->dest += step; break;
            case 1: dc->dest -= step; break;
            case 2: break; // fixed
            case 3: dc->dest += step; break; // increment now, reload after
            }
        }
    }

    // Post-transfer: destination reload for mode 3 (increment+reload)
    if (dc->dest_adjust == 3 && !is_fifo) {
        dc->dest = dc->dest_latch;
    }

    // Fire IRQ if requested
    if (dc->irq_on_done && dma->interrupts) {
        uint16_t irq_bit = (uint16_t)(1 << (8 + ch)); // DMA0=bit8, DMA1=bit9, etc.
        interrupt_request(dma->interrupts, irq_bit);
    }

    // Repeat or disable
    if (dc->repeat && dc->timing != 0) {
        // Repeating DMA: stay enabled, wait for next trigger
        // Reload count from latch (the count register value)
    } else {
        // One-shot or immediate: disable after completion
        dc->enabled = false;
        dc->control &= ~(1u << 15);

        // Also update the io_regs backing store so reads reflect the cleared bit.
        // CNT_H high byte offsets: DMA0=0xBB, DMA1=0xC7, DMA2=0xD3, DMA3=0xDF
        static const uint32_t cnt_h_hi[] = { 0xBB, 0xC7, 0xD3, 0xDF };
        bus->io_regs[cnt_h_hi[ch]] &= ~0x80;
    }

    dma->active_channel = -1;

    // Return approximate cycle cost (2 cycles per unit is a simplification)
    return (int)(count * 2);
}
