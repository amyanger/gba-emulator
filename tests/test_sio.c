#include "test_harness.h"
#include "sio/sio.h"
#include "sio/sio_link.h"
#include "interrupt/interrupt.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>

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

// Hoisted to file scope so the worker thread function and the test body share
// the same definition. The worker just drives sio_tick on one SIO instance —
// link_peer_exchange is blocking, so both ends must run in parallel or the
// first sio_tick deadlocks waiting for its peer.
typedef struct {
    SIO* sio;
    int total_cycles;
} SioTickArgs;

static void* sio_tick_worker(void* arg) {
    SioTickArgs* a = (SioTickArgs*)arg;
    sio_tick(a->sio, a->total_cycles);
    return NULL;
}

TEST(sio_multiplayer_transfer_via_peer_pair) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        // socketpair can fail on some constrained environments (e.g. some
        // WSL2 configs). Skip rather than spuriously fail.
        printf("SKIP (socketpair unavailable: %s)\n", strerror(errno));
        return;
    }

    LinkPeer* peer_a = link_peer_create();
    LinkPeer* peer_b = link_peer_create();
    ASSERT_TRUE(peer_a != NULL);
    ASSERT_TRUE(peer_b != NULL);
    // After inject, the LinkPeer owns the fd — do NOT close fds[0]/fds[1].
    link_peer_test_inject_fd(peer_a, fds[0]);
    link_peer_test_inject_fd(peer_b, fds[1]);

    SIO sio_a, sio_b;
    InterruptController ic_a, ic_b;
    interrupt_init(&ic_a);
    interrupt_init(&ic_b);
    sio_init(&sio_a, &ic_a, peer_a);
    sio_init(&sio_b, &ic_b, peer_b);

    // Both sides arm an MP transfer with their own SIOMLT_SEND payload and
    // IRQ enable.
    sio_a.siomlt_send = 0xAAAA;
    sio_write8(&sio_a, 0x129, 0x60); // MP mode + IRQ enable
    sio_write8(&sio_a, 0x128, 0x80); // START
    ASSERT_TRUE(sio_a.transfer_active);

    sio_b.siomlt_send = 0xBBBB;
    sio_write8(&sio_b, 0x129, 0x60);
    sio_write8(&sio_b, 0x128, 0x80);
    ASSERT_TRUE(sio_b.transfer_active);

    // Drive both ticks in parallel. link_peer_exchange writes both bytes then
    // reads two — running them sequentially on one thread would deadlock.
    SioTickArgs args_b = { &sio_b, 2000 };
    pthread_t t;
    int rc = pthread_create(&t, NULL, sio_tick_worker, &args_b);
    ASSERT_EQ(rc, 0);

    sio_tick(&sio_a, 2000);
    pthread_join(t, NULL);

    ASSERT_EQ_HEX(sio_a.siomulti[0], 0xAAAA);
    ASSERT_EQ_HEX(sio_a.siomulti[1], 0xBBBB);
    ASSERT_EQ_HEX(sio_a.siomulti[2], 0xFFFF);
    ASSERT_EQ_HEX(sio_a.siomulti[3], 0xFFFF);

    ASSERT_EQ_HEX(sio_b.siomulti[0], 0xBBBB);
    ASSERT_EQ_HEX(sio_b.siomulti[1], 0xAAAA);
    ASSERT_EQ_HEX(sio_b.siomulti[2], 0xFFFF);
    ASSERT_EQ_HEX(sio_b.siomulti[3], 0xFFFF);

    ASSERT_TRUE((sio_a.siocnt & SIOCNT_START) == 0);
    ASSERT_TRUE((sio_b.siocnt & SIOCNT_START) == 0);
    ASSERT_TRUE(!sio_a.transfer_active);
    ASSERT_TRUE(!sio_b.transfer_active);

    ASSERT_TRUE((ic_a.irf & IRQ_SERIAL) != 0);
    ASSERT_TRUE((ic_b.irf & IRQ_SERIAL) != 0);

    link_peer_shutdown(peer_a);
    link_peer_shutdown(peer_b);
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
    RUN_TEST(sio_multiplayer_transfer_via_peer_pair);
}
