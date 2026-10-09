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
    /* Cycles owed to completed transfers: DMA halts the CPU, so cpu_run
     * consumes this stall before executing instructions.  Transient
     * (not serialized in savestates). */
    int32_t pending_stall;
    /* Per channel: stall cycles left until its completion IRQ is raised
     * (0 = none owed). The IRQ fires when the CPU unblocks, like mGBA's
     * end-of-transfer event. Transient like pending_stall. */
    int32_t irq_stall[4];
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
int32_t dma_consume_stall(DMAController* dma, int32_t max);
uint16_t dma_owed_irqs(const DMAController* dma);

#endif // DMA_H
