#ifndef SIO_LINK_H
#define SIO_LINK_H

#include "common.h"

// LinkPeer is the transport for one SIO multiplayer link. v1 only handles
// 2-player point-to-point over a UNIX domain socket. The protocol exchanges
// fixed 2-byte SIOMLT_SEND payloads. Both sides send their packet and read
// the peer's; no master/slave distinction at the transport level.

typedef struct LinkPeer LinkPeer;

// Allocate and zero a LinkPeer. Caller owns the pointer (free with shutdown).
LinkPeer* link_peer_create(void);

// Listen for one incoming connection on `path` (AF_UNIX SOCK_STREAM).
// Blocks until a peer connects. Returns true on success.
bool link_peer_listen(LinkPeer* peer, const char* path);

// Connect to a listener at `path`. Returns true on success.
bool link_peer_connect(LinkPeer* peer, const char* path);

bool link_peer_is_connected(const LinkPeer* peer);

// Send our 16-bit payload, block (with timeout) for peer's reply.
// On success, *peer_out is set to the peer's payload and returns true.
// On timeout or peer disconnect, returns false and the SIO module should
// treat slot as 0xFFFF (disconnected).
bool link_peer_exchange(LinkPeer* peer, uint16_t our_payload, uint16_t* peer_out);

// Close socket and free.
void link_peer_shutdown(LinkPeer* peer);

#endif // SIO_LINK_H
