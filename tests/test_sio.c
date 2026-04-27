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

void run_sio_tests(void) {
    printf("\nSIO tests:\n");
    RUN_TEST(sio_init_zeros_state);
}
