#include "test_harness.h"
#include "sio/sio_link.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

TEST(link_peer_create_returns_unconnected) {
    LinkPeer* peer = link_peer_create();
    ASSERT_TRUE(peer != NULL);
    ASSERT_TRUE(!link_peer_is_connected(peer));
    link_peer_shutdown(peer);
}

// Hoisted to file scope so the worker thread and the test can share the
// same definition. (The plan sketched a local-scope struct duplicated in
// two places — this is the cleaner equivalent.)
typedef struct {
    LinkPeer* p;
    uint16_t out;
    uint16_t* in;
    bool ok;
} ExchangeArgs;

static void* exchange_worker(void* arg) {
    ExchangeArgs* a = (ExchangeArgs*)arg;
    a->ok = link_peer_exchange(a->p, a->out, a->in);
    return NULL;
}

TEST(link_peer_exchange_round_trip) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        // socketpair can fail on some constrained environments (e.g. some
        // WSL2 configs). Skip rather than spuriously fail.
        printf("SKIP (socketpair unavailable: %s)\n", strerror(errno));
        return;
    }

    LinkPeer* a = link_peer_create();
    LinkPeer* b = link_peer_create();
    ASSERT_TRUE(a != NULL);
    ASSERT_TRUE(b != NULL);

    link_peer_test_inject_fd(a, fds[0]);
    link_peer_test_inject_fd(b, fds[1]);

    uint16_t a_recv = 0, b_recv = 0;
    ExchangeArgs b_args = { b, 0xBEEF, &b_recv, false };

    pthread_t t;
    int rc = pthread_create(&t, NULL, exchange_worker, &b_args);
    ASSERT_EQ(rc, 0);

    bool ok_a = link_peer_exchange(a, 0xCAFE, &a_recv);
    pthread_join(t, NULL);

    ASSERT_TRUE(ok_a);
    ASSERT_TRUE(b_args.ok);
    ASSERT_EQ_HEX(a_recv, 0xBEEF);
    ASSERT_EQ_HEX(b_recv, 0xCAFE);

    link_peer_shutdown(a);
    link_peer_shutdown(b);
}

// Verifies the SO_RCVTIMEO/SO_SNDTIMEO safeguard added to sio_link.c. With a
// silent peer holding the other end of the socketpair, link_peer_exchange
// must report failure (rather than block forever) and mark the peer as
// disconnected so sio_tick can fall back to the 0xFFFF "slot empty" payload.
TEST(link_peer_exchange_timeout_when_peer_silent) {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        printf("SKIP (socketpair unavailable)\n");
        return;
    }
    LinkPeer* a = link_peer_create();
    link_peer_test_inject_fd(a, fds[0]);
    /* fds[1] never replies — held but unused. */

    uint16_t recv = 0xDEAD;
    bool ok = link_peer_exchange(a, 0xCAFE, &recv);
    ASSERT_TRUE(!ok);                          // exchange reports failure
    ASSERT_TRUE(!link_peer_is_connected(a));   // peer marked disconnected

    close(fds[1]);
    link_peer_shutdown(a);
}

void run_sio_link_tests(void) {
    printf("\nSIO Link tests:\n");
    RUN_TEST(link_peer_create_returns_unconnected);
    RUN_TEST(link_peer_exchange_round_trip);
    RUN_TEST(link_peer_exchange_timeout_when_peer_silent);
}
