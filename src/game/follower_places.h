#ifndef FOLLOWER_PLACES_H
#define FOLLOWER_PLACES_H

#include <stdbool.h>
#include <stdint.h>

/* HGSS map section whose local rule table (a/2/2/0 member mapsec+1) fits this
 * Emerald place, or 0 for "no local table". */
uint16_t follower_place_mapsec(uint8_t map_type, uint8_t mapsec, uint8_t behavior);
/* True if HGSS follower message id (1-based, msg bank 265) names a Johto place or event. */
bool follower_place_msg_denied(uint16_t msg);
/* Emerald metatile behavior -> HGSS tile behavior used by the rules, 0 when none. */
uint8_t follower_place_behavior(uint8_t emerald_behavior);

#endif // FOLLOWER_PLACES_H
