#include "test_harness.h"
#include "gba.h"

/* DMA tests use a fully wired GBA so the controller can talk to the
 * real bus. The cartridge isn't loaded, so we keep all transfers
 * inside RAM regions. */
static GBA* make_gba(void) {
    GBA* gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    return gba;
}

/* Enable DMA channel `ch` with control bits assembled as:
 *   bit 15 = enable (1)
 *   bit 10 = transfer_32 (1=32-bit, 0=16-bit)
 *   bits 12-13 = timing (0=immediate, 1=VBlank, 2=HBlank, 3=special)
 * Other bits left at 0 (increment-on-both, no IRQ, no repeat). */
static uint16_t dma_control(bool transfer_32, uint8_t timing) {
    uint16_t v = 0x8000;                  /* enable */
    if (transfer_32) v |= (1u << 10);
    v |= (uint16_t)(timing & 3) << 12;
    return v;
}

TEST(dma3_immediate_word_copy) {
    /* Use channel 3 — the general-purpose DMA. Set up an immediate
     * 4-word copy from one IWRAM range to another, and verify the
     * destination matches after the rising-edge enable. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;

    /* Seed source data in IWRAM at 0x03000000. */
    for (int i = 0; i < 16; i++) {
        bus->iwram[i] = (uint8_t)(0x10 + i);
    }
    /* Clear the destination range. */
    memset(&bus->iwram[0x100], 0, 16);

    /* DMA3 SAD = 0x03000000, DAD = 0x03000100, CNT_L = 4 words. */
    bus_write32(bus, 0x040000D4, 0x03000000); /* SAD */
    bus_write32(bus, 0x040000D8, 0x03000100); /* DAD */
    bus_write16(bus, 0x040000DC, 4);          /* count */
    /* Writing CNT_H (0xDE) with enable=1 + immediate timing kicks the
     * transfer. dma_write_control runs synchronously when timing=0. */
    bus_write16(bus, 0x040000DE, dma_control(true, 0));

    /* Verify the destination matches the source byte-for-byte. */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(bus->iwram[0x100 + i], bus->iwram[i]);
    }
    free(gba);
}

TEST(dma_transfer_stalls_cpu) {
    /* DMA halts the CPU: the cycles a transfer costs must be consumed
     * from the CPU's budget before it executes further instructions. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;

    /* Immediate 64-word IWRAM copy on DMA3. */
    bus_write32(bus, 0x040000D4, 0x03000000);
    bus_write32(bus, 0x040000D8, 0x03000400);
    bus_write16(bus, 0x040000DC, 64);
    bus_write16(bus, 0x040000DE, dma_control(true, 0));

    /* The transfer cost is now pending as a CPU stall. */
    ASSERT_TRUE(gba->dma.pending_stall >= 128); /* >= 2 cycles per word */
    int stall = gba->dma.pending_stall;

    /* Running the CPU for fewer cycles than the stall must consume the
     * budget WITHOUT executing any instruction (PC unchanged). */
    uint32_t pc_before = gba->cpu.regs[REG_PC];
    cpu_run(&gba->cpu, 10);
    ASSERT_EQ_HEX(gba->cpu.regs[REG_PC], pc_before);
    ASSERT_EQ(gba->dma.pending_stall, stall - 10);

    free(gba);
}

TEST(dma0_count_masked_to_14_bits) {
    /* DMA0-2 word counts are 14-bit (GBATEK); bits 14-15 of CNT_L are
     * ignored.  0x4001 must transfer exactly 1 unit, not 0x4001. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;

    bus->iwram[0] = 0xAA;
    bus->iwram[1] = 0xBB;
    bus->iwram[2] = 0xCC;
    bus->iwram[3] = 0xDD;
    memset(&bus->iwram[0x200], 0, 8);

    bus_write32(bus, 0x040000B0, 0x03000000); /* DMA0 SAD */
    bus_write32(bus, 0x040000B4, 0x03000200); /* DMA0 DAD */
    bus_write16(bus, 0x040000B8, 0x4001);     /* count: bit 14 ignored */
    bus_write16(bus, 0x040000BA, dma_control(false, 0)); /* 16-bit, immediate */

    /* Exactly one halfword copied. */
    ASSERT_EQ_HEX(bus->iwram[0x200], 0xAA);
    ASSERT_EQ_HEX(bus->iwram[0x201], 0xBB);
    ASSERT_EQ_HEX(bus->iwram[0x202], 0x00);
    ASSERT_EQ_HEX(bus->iwram[0x203], 0x00);

    free(gba);
}

TEST(dma_disabled_does_not_transfer) {
    /* If the enable bit is clear, no transfer happens even with a
     * valid SAD/DAD/count. */
    GBA* gba = make_gba();
    Bus* bus = &gba->bus;

    bus->iwram[0] = 0xAB;
    bus->iwram[0x100] = 0;

    bus_write32(bus, 0x040000D4, 0x03000000);
    bus_write32(bus, 0x040000D8, 0x03000100);
    bus_write16(bus, 0x040000DC, 1);
    /* Control with enable=0. */
    bus_write16(bus, 0x040000DE, 0);

    ASSERT_EQ(bus->iwram[0x100], 0);
    free(gba);
}

void run_dma_tests(void) {
    TEST_SUITE("dma");
    RUN_TEST(dma3_immediate_word_copy);
    RUN_TEST(dma_transfer_stalls_cpu);
    RUN_TEST(dma0_count_masked_to_14_bits);
    RUN_TEST(dma_disabled_does_not_transfer);
}
