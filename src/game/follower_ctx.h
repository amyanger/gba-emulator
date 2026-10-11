#ifndef FOLLOWER_CTX_H
#define FOLLOWER_CTX_H

#include "game/follower.h"
#include "hgss/hgss_talk.h"

typedef struct {
    uint8_t type1, type2;      /* Emerald type ids */
    uint8_t gender_ratio;
    uint8_t item_pocket;       /* Emerald pocket of held item, 0 none: 1 items, 2 balls, 3 TM/HM, 4 berries, 5 key */
    uint8_t species_class;     /* HGSS a/2/3/1 byte for the national dex number */
    uint8_t adjacent_objects;  /* objects within 1 tile of the player, player excluded */
    uint8_t hidden_items;      /* unfound hidden items on the map */
    bool encounter_tile;       /* follower tile can produce wild encounters */
    uint8_t hour;              /* 0..23, RTC minus save offset */
    int8_t mood;
    uint8_t follower_facing;   /* game 1..4 */
} FollowerCtxExtra;

void follower_ctx_build(const FollowerInput* in, const FollowerCtxExtra* x, HgssTalkCtx* out);
uint8_t follower_ctx_time(uint8_t hour);          /* 1 morn 4-9, 2 day 10-16, 3 eve 17-19, 4 night 20-23, 5 late 0-3 */
uint8_t follower_ctx_weather(uint8_t emerald_weather);   /* 1 if 0/1/2, 3 if 3/5/13, else 0 */
uint8_t follower_ctx_type(uint8_t emerald_type);  /* HGSS 1..17; 0 for ??? (9) */
uint8_t follower_ctx_behavior(uint8_t emerald_behavior);  /* HGSS tile behavior or 0 */

#endif // FOLLOWER_CTX_H
