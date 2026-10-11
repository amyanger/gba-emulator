#include "game/follower_ctx.h"
#include "game/follower_places.h"
#include "game/pokemon.h"
#include <string.h>

static const uint8_t NATURE_CLASS[25] = {4, 5, 4, 4, 1, 4, 3, 2, 1, 2, 5, 6, 3,
                                         1, 1, 3, 6, 3, 5, 6, 2, 2, 1, 3, 6};

uint8_t follower_ctx_time(uint8_t hour) {
    if (hour >= 4 && hour <= 9) return 1;
    if (hour >= 10 && hour <= 16) return 2;
    if (hour >= 17 && hour <= 19) return 3;
    if (hour >= 20) return 4;
    return 5;
}

uint8_t follower_ctx_weather(uint8_t w) {
    if (w <= 2) return 1;
    if (w == 3 || w == 5 || w == 13) return 3;
    return 0;
}

uint8_t follower_ctx_type(uint8_t t) {
    if (t == 9) return 0;
    return t < 9 ? (uint8_t)(t + 1) : t;
}

uint8_t follower_ctx_behavior(uint8_t b) {
    return follower_place_behavior(b);
}

static uint8_t clamp8(uint32_t v) {
    return v > 255 ? 255 : (uint8_t)v;
}

static uint8_t hp_class(uint16_t hp, uint16_t max) {
    if (hp == max) return 1;
    if ((uint32_t)hp * 4 >= (uint32_t)max * 3) return 2;
    if ((uint32_t)hp * 2 >= max) return 3;
    if ((uint32_t)hp * 4 >= max) return 4;
    return 5;
}

static uint8_t status_class(uint32_t s) {
    if (s & 7) return 8;
    if ((s & (1u << 3)) || (s & (1u << 7))) return 5;
    if (s & (1u << 4)) return 2;
    if (s & (1u << 5)) return 3;
    if (s & (1u << 6)) return 4;
    return 1;
}

static uint8_t item_class(uint16_t item, uint8_t pocket) {
    if (item == 0) return 8;
    switch (pocket) {
    case 1: return 4;
    case 2: return 2;
    case 3: return 1;
    case 4: return 7;
    case 5: return 6;
    default: return 8;
    }
}

static uint8_t facing_class(uint8_t f) {
    switch (f) {
    case 2: return 3;
    case 1: return 4;
    case 3: return 2;
    case 4: return 1;
    default: return 0;
    }
}

void follower_ctx_build(const FollowerInput* in, const FollowerCtxExtra* x, HgssTalkCtx* out) {
    memset(out, 0, sizeof(*out));
    const MonData* m = &in->lead;
    uint8_t* c = out->c;
    c[HC_HAS_ITEM] = m->held_item != 0;
    c[HC_ITEM_CLASS] = item_class(m->held_item, x->item_pocket);
    c[HC_HP] = hp_class(m->hp, m->max_hp);
    c[HC_STATUS] = status_class(m->status);
    c[HC_LEVEL] = m->level >= 48 ? 4 : 6;
    c[HC_TYPE1] = follower_ctx_type(x->type1);
    c[HC_TYPE2] = follower_ctx_type(x->type2);
    c[HC_FRIEND] = m->friendship;
    uint8_t nature = pokemon_nature(m->personality);
    c[HC_NATURE] = nature < 25 ? NATURE_CLASS[nature] : 0;
    c[HC_GENDER] = pokemon_gender(x->gender_ratio, m->personality) == MON_GENDER_MALE ? 1 : 2;
    c[HC_SPECIES_CLASS] = x->species_class;
    c[HC_LEAVES] = 0;
    c[HC_NPCS] = clamp8(x->adjacent_objects);
    c[HC_HIDDEN] = clamp8(x->hidden_items);
    c[HC_WEATHER] = follower_ctx_weather(in->weather);
    c[HC_TILE] = follower_ctx_behavior(in->fol_behavior);
    c[HC_ENCOUNTER] = x->encounter_tile ? 1 : 2;
    c[HC_TIME] = follower_ctx_time(x->hour);
    c[HC_MOOD] = (uint8_t)x->mood;
    c[HC_ATHLON] = 0;
    c[HC_FACING] = facing_class(x->follower_facing);
    out->map_id = 0xFFFF;
}
