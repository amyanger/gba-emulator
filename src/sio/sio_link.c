#include "sio_link.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct LinkPeer {
    int32_t listen_fd;
    int32_t conn_fd;
    bool connected;
};

LinkPeer* link_peer_create(void) {
    LinkPeer* peer = calloc(1, sizeof(LinkPeer));
    if (!peer) return NULL;
    peer->listen_fd = -1;
    peer->conn_fd = -1;
    peer->connected = false;
    return peer;
}

bool link_peer_is_connected(const LinkPeer* peer) {
    return peer && peer->connected;
}

bool link_peer_listen(LinkPeer* peer, const char* path) {
    (void)peer;
    (void)path;
    return false; // Real implementation lands in Task 8
}

bool link_peer_connect(LinkPeer* peer, const char* path) {
    (void)peer;
    (void)path;
    return false; // Real implementation lands in Task 8
}

bool link_peer_exchange(LinkPeer* peer, uint16_t our_payload, uint16_t* peer_out) {
    (void)peer;
    (void)our_payload;
    (void)peer_out;
    return false; // Real implementation lands in Task 8
}

void link_peer_shutdown(LinkPeer* peer) {
    if (!peer) return;
    if (peer->conn_fd >= 0) close(peer->conn_fd);
    if (peer->listen_fd >= 0) close(peer->listen_fd);
    free(peer);
}
