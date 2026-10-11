#include "game/follower_places.h"

uint16_t follower_place_mapsec(uint8_t map_type, uint8_t mapsec, uint8_t behavior) {
    (void)map_type;
    (void)mapsec;
    (void)behavior;
    return 0;
}

bool follower_place_msg_denied(uint16_t msg) {
    (void)msg;
    return false;
}

uint8_t follower_place_behavior(uint8_t b) {
    if (b == 0x02) return 2;   /* tall grass */
    if (b == 0x03) return 3;   /* long grass */
    return 0;
}
