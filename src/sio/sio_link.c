#include "sio_link.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

// Maximum time to block on a single read/write before treating the peer as
// unresponsive. 200 ms is comfortably larger than a multiplayer slot transfer
// (~1-2 ms at the GBA's serial baud rates) yet small enough that a crashed
// peer surfaces as "disconnected" within a frame or two rather than wedging
// the emulator main thread forever.
#define LINK_PEER_IO_TIMEOUT_USEC 200000

struct LinkPeer {
    int listen_fd;
    int conn_fd;
    bool connected;
    // Path used by link_peer_listen so shutdown can unlink the socket file
    // it created. Empty string when this peer didn't create the file
    // (clients via link_peer_connect, or test fd injection).
    // sockaddr_un::sun_path is 108 bytes on Linux; mirror that so any path
    // we successfully bound to also fits here.
    char socket_path[108];
};

// Apply a bounded send/receive timeout to `fd`. Without this, a crashed or
// hung peer would block read/write indefinitely and freeze the emulator main
// thread. setsockopt is best-effort: a failure (e.g. unsupported on the
// platform's socket type) just leaves the fd in default blocking mode, which
// is the prior behavior — never worse than before.
static void set_socket_timeouts(int fd) {
    struct timeval tv = { .tv_sec = 0, .tv_usec = LINK_PEER_IO_TIMEOUT_USEC };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

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

void link_peer_test_inject_fd(LinkPeer* peer, int fd) {
    if (!peer) return;
    // Close any prior conn fd (e.g. from a previous listen/connect/inject)
    // to avoid leaking descriptors when tests reuse a peer.
    if (peer->conn_fd >= 0) close(peer->conn_fd);
    peer->conn_fd = fd;
    peer->connected = true;
    // Mirror the production listen/connect paths so timeout-based tests
    // exercise the same I/O behavior the emulator sees in practice.
    set_socket_timeouts(fd);
}

bool link_peer_listen(LinkPeer* peer, const char* path) {
    if (!peer || !path) return false;

    // Best-effort cleanup of stale socket files from prior crashes. The
    // listening process always owns its socket path, so an existing entry
    // here is unrecoverable junk by definition.
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0
        || listen(fd, 1) != 0) {
        close(fd);
        return false;
    }
    peer->listen_fd = fd;
    // Remember the bound path so shutdown can unlink it. We are the
    // listener, therefore we own this filesystem entry.
    snprintf(peer->socket_path, sizeof(peer->socket_path), "%s", path);

    // Blocking accept: v1 link cable is a 2-player point-to-point setup.
    int conn = accept(fd, NULL, NULL);
    if (conn < 0) {
        close(fd);
        peer->listen_fd = -1;
        return false;
    }
    peer->conn_fd = conn;
    peer->connected = true;
    set_socket_timeouts(peer->conn_fd);
    return true;
}

bool link_peer_connect(LinkPeer* peer, const char* path) {
    if (!peer || !path) return false;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false;
    }
    peer->conn_fd = fd;
    peer->connected = true;
    // Client side: don't touch socket_path — the listener owns the file,
    // and this peer must not unlink it on shutdown.
    set_socket_timeouts(peer->conn_fd);
    return true;
}

// Loop until the full payload is written, retrying on EINTR. EAGAIN/
// EWOULDBLOCK (i.e. the SO_SNDTIMEO timeout fired) and any other error
// return false — the caller will surface this as "peer unresponsive" by
// marking the peer disconnected. We deliberately do NOT retry on timeout:
// a timeout is exactly the signal we want to propagate.
static bool write_full(int fd, const void* buf, size_t n) {
    const uint8_t* p = (const uint8_t*)buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false; // EAGAIN/EWOULDBLOCK on timeout; treat as failure.
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return true;
}

// Loop until the full payload is read, retrying on EINTR. A read of 0 means
// the peer cleanly closed the connection — treat as failure. EAGAIN/
// EWOULDBLOCK from the SO_RCVTIMEO timeout also fails the read so a hung
// peer doesn't deadlock the emulator main thread.
static bool read_full(int fd, void* buf, size_t n) {
    uint8_t* p = (uint8_t*)buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false; // EAGAIN/EWOULDBLOCK on timeout; treat as failure.
        }
        if (r == 0) return false; // peer closed
        p += (size_t)r;
        n -= (size_t)r;
    }
    return true;
}

bool link_peer_exchange(LinkPeer* peer, uint16_t our_payload, uint16_t* peer_out) {
    if (!peer || !peer->connected || !peer_out) return false;

    // Serialize our 16-bit payload little-endian on the wire so both ends
    // agree regardless of host byte order.
    uint8_t out_buf[2] = { (uint8_t)our_payload, (uint8_t)(our_payload >> 8) };
    if (!write_full(peer->conn_fd, out_buf, sizeof(out_buf))) {
        peer->connected = false;
        return false;
    }

    uint8_t in_buf[2];
    if (!read_full(peer->conn_fd, in_buf, sizeof(in_buf))) {
        peer->connected = false;
        return false;
    }

    *peer_out = (uint16_t)in_buf[0] | ((uint16_t)in_buf[1] << 8);
    return true;
}

void link_peer_shutdown(LinkPeer* peer) {
    if (!peer) return;
    if (peer->conn_fd >= 0) close(peer->conn_fd);
    if (peer->listen_fd >= 0) close(peer->listen_fd);
    // Remove the listening socket file we created. Best-effort: a removed or
    // renamed entry isn't worth surfacing, the close() above already freed
    // the kernel-side resource.
    if (peer->socket_path[0] != '\0') {
        unlink(peer->socket_path);
    }
    free(peer);
}
