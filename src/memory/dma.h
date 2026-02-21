#ifndef DMA_H
#define DMA_H

#include "common.h"

// Forward declarations
typedef struct Bus Bus;
typedef struct InterruptController InterruptController;

typedef struct {
    uint32_t source;
    uint32_t dest;
    uint32_t source_latch;
    uint32_t dest_latch;
    uint16_t count;
    uint16_t control;

    // Decoded control fields
    int8_t dest_adjust;   // 0=inc, 1=dec, 2=fixed, 3=inc+reload
    int8_t src_adjust;    // 0=inc, 1=dec, 2=fixed
    bool repeat;
    bool transfer_32;     // false=16-bit, true=32-bit
    uint8_t timing;       // 0=immediate, 1=VBlank, 2=HBlank, 3=special
    bool irq_on_done;
    bool enabled;
} DMAChannel;

struct DMAController {
    DMAChannel channels[4];
    int8_t active_channel; // -1 if none active
    Bus* bus;
    InterruptController* interrupts;
};
typedef struct DMAController DMAController;

void dma_init(DMAController* dma);
void dma_write_control(DMAController* dma, int ch, uint16_t val);
void dma_on_vblank(DMAController* dma);
void dma_on_hblank(DMAController* dma);
void dma_on_fifo(DMAController* dma, int fifo_id);
int dma_execute(DMAController* dma, int ch);

#endif // DMA_H
