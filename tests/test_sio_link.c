#include "test_harness.h"
#include "sio/sio_link.h"

TEST(link_peer_create_returns_unconnected) {
    LinkPeer* peer = link_peer_create();
    ASSERT_TRUE(peer != NULL);
    ASSERT_TRUE(!link_peer_is_connected(peer));
    link_peer_shutdown(peer);
}

void run_sio_link_tests(void) {
    printf("\nSIO Link tests:\n");
    RUN_TEST(link_peer_create_returns_unconnected);
}
