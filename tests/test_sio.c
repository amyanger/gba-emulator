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

void run_sio_tests(void) {
    printf("\nSIO tests:\n");
    RUN_TEST(sio_init_zeros_state);
    RUN_TEST(sio_siocnt_writes_low_byte);
    RUN_TEST(sio_siocnt_writes_high_byte);
    RUN_TEST(sio_rcnt_writes_round_trip);
    RUN_TEST(sio_siomlt_send_round_trip);
    RUN_TEST(sio_siomulti_reads_back);
}
