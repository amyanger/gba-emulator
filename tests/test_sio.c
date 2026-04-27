#include "test_harness.h"
#include "sio/sio.h"
#include "interrupt/interrupt.h"

TEST(sio_init_zeros_state) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    ASSERT_EQ(sio.siocnt, 0);
    ASSERT_EQ(sio.rcnt, 0);
    ASSERT_EQ(sio.siomlt_send, 0);
    ASSERT_EQ(sio.siomulti[0], 0);
    ASSERT_EQ(sio.siomulti[1], 0);
    ASSERT_EQ(sio.siomulti[2], 0);
    ASSERT_EQ(sio.siomulti[3], 0);
    ASSERT_EQ(sio.transfer_active, false);
    ASSERT_EQ(sio.mode, SIO_MODE_NORMAL_8);
    ASSERT_EQ(sio.siodata32, 0);
    ASSERT_EQ(sio.serial_mode_enabled, false);
    ASSERT_EQ(sio.transfer_cycles_remaining, 0);
    ASSERT_TRUE(sio.interrupts == &ic);
    ASSERT_TRUE(sio.peer == NULL);
}

TEST(sio_siocnt_writes_low_byte) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio_write8(&sio, 0x128, 0xAB);
    ASSERT_EQ_HEX(sio.siocnt & 0xFF, 0xAB);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x128), 0xAB);
}

TEST(sio_siocnt_writes_high_byte) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio_write8(&sio, 0x129, 0x40); // bit 14 = IRQ enable
    ASSERT_EQ_HEX(sio.siocnt & 0xFF00, 0x4000);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x129), 0x40);
}

TEST(sio_rcnt_writes_round_trip) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio_write8(&sio, 0x134, 0x12);
    sio_write8(&sio, 0x135, 0x80);
    ASSERT_EQ_HEX(sio.rcnt, 0x8012);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x134), 0x12);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x135), 0x80);
}

TEST(sio_siomlt_send_round_trip) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio_write8(&sio, 0x12A, 0xCD);
    sio_write8(&sio, 0x12B, 0xAB);
    ASSERT_EQ_HEX(sio.siomlt_send, 0xABCD);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x12A), 0xCD);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x12B), 0xAB);
}

TEST(sio_siomulti_reads_back) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio.siomulti[0] = 0x1234;
    sio.siomulti[1] = 0xABCD;

    ASSERT_EQ_HEX(sio_read8(&sio, 0x120), 0x34);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x121), 0x12);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x122), 0xCD);
    ASSERT_EQ_HEX(sio_read8(&sio, 0x123), 0xAB);
}

TEST(sio_multiplayer_transfer_with_no_peer_returns_ffff) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    // Configure: RCNT in serial mode (high bits 0), SIOCNT in MP mode
    // (bits 12-13 = 10), IRQ enable (bit 14 = 0x4000).
    sio_write8(&sio, 0x134, 0x00);
    sio_write8(&sio, 0x135, 0x00);                  // RCNT = 0x0000
    sio.siomlt_send = 0xCAFE;
    sio_write8(&sio, 0x128, 0x80);                  // SIOCNT low: START bit
    sio_write8(&sio, 0x129, 0x60);                  // SIOCNT high: MP mode + IRQ enable

    // Transfer should be marked active immediately
    ASSERT_TRUE(sio.transfer_active);

    // Tick enough cycles for transfer to complete (< 1 scanline is fine)
    sio_tick(&sio, 2000);

    // After transfer: SIOMULTI0 holds our send, SIOMULTI1..3 hold 0xFFFF
    ASSERT_EQ_HEX(sio.siomulti[0], 0xCAFE);
    ASSERT_EQ_HEX(sio.siomulti[1], 0xFFFF);
    ASSERT_EQ_HEX(sio.siomulti[2], 0xFFFF);
    ASSERT_EQ_HEX(sio.siomulti[3], 0xFFFF);

    // Start/Busy bit clears, IRQ is requested
    ASSERT_TRUE((sio.siocnt & SIOCNT_START) == 0);
    ASSERT_TRUE(!sio.transfer_active);
    ASSERT_TRUE((ic.irf & IRQ_SERIAL) != 0);
}

TEST(sio_multiplayer_no_irq_when_disabled) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio.siomlt_send = 0x1234;
    sio_write8(&sio, 0x128, 0x80);                  // START
    sio_write8(&sio, 0x129, 0x20);                  // MP mode, IRQ disabled

    sio_tick(&sio, 2000);

    ASSERT_EQ_HEX(sio.siomulti[0], 0x1234);
    ASSERT_TRUE((ic.irf & IRQ_SERIAL) == 0);
}

TEST(sio_multiplayer_transfer_via_halfword_write) {
    SIO sio;
    InterruptController ic;
    interrupt_init(&ic);
    sio_init(&sio, &ic, NULL);

    sio.siomlt_send = 0xDEAD;
    /* Simulate bus_write16 with start+mode in a single halfword: low byte then high byte. */
    sio_write8(&sio, 0x128, 0x80);
    sio_write8(&sio, 0x129, 0x60);
    ASSERT_TRUE(sio.transfer_active);

    sio_tick(&sio, 2000);
    ASSERT_EQ_HEX(sio.siomulti[0], 0xDEAD);
    ASSERT_EQ_HEX(sio.siomulti[1], 0xFFFF);
    ASSERT_TRUE((sio.siocnt & SIOCNT_START) == 0);
    ASSERT_TRUE((ic.irf & IRQ_SERIAL) != 0);
}

void run_sio_tests(void) {
    printf("\nSIO tests:\n");
    RUN_TEST(sio_init_zeros_state);
    RUN_TEST(sio_siocnt_writes_low_byte);
    RUN_TEST(sio_siocnt_writes_high_byte);
    RUN_TEST(sio_rcnt_writes_round_trip);
    RUN_TEST(sio_siomlt_send_round_trip);
    RUN_TEST(sio_siomulti_reads_back);
    RUN_TEST(sio_multiplayer_transfer_with_no_peer_returns_ffff);
    RUN_TEST(sio_multiplayer_no_irq_when_disabled);
    RUN_TEST(sio_multiplayer_transfer_via_halfword_write);
}
