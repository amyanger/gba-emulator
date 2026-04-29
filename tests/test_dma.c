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
    RUN_TEST(dma_disabled_does_not_transfer);
}
