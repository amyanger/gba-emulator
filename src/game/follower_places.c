#include "game/follower_places.h"

/* Emerald map types (gMapHeader.mapType). */
#define MAP_TYPE_TOWN 1
#define MAP_TYPE_CITY 2
#define MAP_TYPE_ROUTE 3
#define MAP_TYPE_UNDERGROUND 4
#define MAP_TYPE_OCEAN_ROUTE 6

/* Emerald region map sections. */
#define MAPSEC_FIERY_PATH_A 74
#define MAPSEC_FIERY_PATH_B 75
#define MAPSEC_JAGGED_PASS_A 76
#define MAPSEC_JAGGED_PASS_B 77
#define MAPSEC_MT_CHIMNEY 56
#define MAPSEC_MT_PYRE 65
#define MAPSEC_PETALBURG_WOODS 59

/* Emerald metatile behaviors. */
#define MB_TALL_GRASS 0x02
#define MB_LONG_GRASS 0x03
#define MB_DEEP_SAND 0x06
#define MB_ICE 0x20
#define MB_SAND 0x21
#define MB_PUDDLE 0x16
#define MB_SHALLOW_WATER 0x17

/* HGSS map sections whose local rule tables (a/2/2/0 member sec+1) were chosen from
 * the lines each table selects; see the research appendix in the design spec. */
#define HGSS_SEC_SEA_ROUTE 167  /* shore and sea lines: wet and sandy tiles */
#define HGSS_SEC_CAVE 200       /* rocks and digging */
#define HGSS_SEC_ICE_CAVE 217   /* ice sliding and cold */
#define HGSS_SEC_FOREST 224     /* leaves, branches, sunlight through trees */
#define HGSS_SEC_MOUNTAIN 137   /* tense, alert, unusual presence */
#define HGSS_SEC_TOWN 126
#define HGSS_SEC_CITY 127

uint16_t follower_place_mapsec(uint8_t map_type, uint8_t mapsec, uint8_t behavior) {
    if (behavior == MB_ICE) return HGSS_SEC_ICE_CAVE;
    switch (mapsec) {
    case MAPSEC_PETALBURG_WOODS:
        return HGSS_SEC_FOREST;
    case MAPSEC_MT_CHIMNEY:
    case MAPSEC_MT_PYRE:
    case MAPSEC_JAGGED_PASS_A:
    case MAPSEC_JAGGED_PASS_B:
    case MAPSEC_FIERY_PATH_A:
    case MAPSEC_FIERY_PATH_B:
        return HGSS_SEC_MOUNTAIN;
    default:
        break;
    }
    switch (map_type) {
    case MAP_TYPE_TOWN:
        return HGSS_SEC_TOWN;
    case MAP_TYPE_CITY:
        return HGSS_SEC_CITY;
    case MAP_TYPE_UNDERGROUND:
        return HGSS_SEC_CAVE;
    case MAP_TYPE_OCEAN_ROUTE:
        return HGSS_SEC_SEA_ROUTE;
    case MAP_TYPE_ROUTE:
        /* A route counts as a beach when the follower stands on sand or in the shallows. */
        if (behavior == MB_SAND || behavior == MB_DEEP_SAND || behavior == MB_SHALLOW_WATER)
            return HGSS_SEC_SEA_ROUTE;
        return 0;
    default:
        return 0;
    }
}

/* Sorted. Lines in the rule tables that name a Johto/Kanto place, person, Pokemon
 * belonging to an NPC, or event (mom, labs, towers, trains, statues, dance shows). */
static const uint16_t k_denied_msgs[] = {
    43, 77, 78, 79, 80, 122, 144, 191, 230, 247, 298, 299, 302, 367, 384, 410, 415, 464,
    480, 498, 514, 517, 583, 605, 609, 689, 690, 692, 727, 730, 738, 739, 742, 753,
};

bool follower_place_msg_denied(uint16_t msg) {
    uint32_t lo = 0, hi = (uint32_t)(sizeof k_denied_msgs / sizeof k_denied_msgs[0]);
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (k_denied_msgs[mid] == msg) return true;
        if (k_denied_msgs[mid] < msg) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

/* HGSS values used by the rules: 2 grass, 3 long grass, 22 puddle, 23 shallow water,
 * 32 ice, 33 sand. Deep, pond and ocean water map to 0 because the follower is hidden
 * while the player surfs. 46, 48, 49, 113 and 114 are never tested by a reachable rule. */
uint8_t follower_place_behavior(uint8_t b) {
    switch (b) {
    case MB_TALL_GRASS: return 2;
    case MB_LONG_GRASS: return 3;
    case MB_PUDDLE: return 22;
    case MB_SHALLOW_WATER: return 23;
    case MB_ICE: return 32;
    case MB_SAND:
    case MB_DEEP_SAND: return 33;
    default: return 0;
    }
}
