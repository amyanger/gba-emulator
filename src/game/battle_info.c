#include "game/battle_info.h"

uint8_t battle_type_quarters(const uint8_t* table, uint32_t len, uint8_t move_type,
                             uint8_t def_type1, uint8_t def_type2, bool foresight) {
    uint32_t q = 4;
    for (uint32_t i = 0; i + 2 < len; i += 3) {
        uint8_t atk = table[i], def = table[i + 1], mult = table[i + 2];
        if (atk == 0xFF) break;
        if (atk == 0xFE) {
            if (foresight) break;
            continue;
        }
        if (atk != move_type) continue;
        if (def == def_type1) q = q * mult / 10;
        if (def == def_type2 && def_type2 != def_type1) q = q * mult / 10;
    }
    return (uint8_t)q;
}

uint8_t battle_apply_ability(uint8_t quarters, uint8_t move_type, uint8_t def_ability) {
    if (def_ability == ABILITY_LEVITATE && move_type == TYPE_GROUND) return 0;
    if (def_ability == ABILITY_WONDER_GUARD && quarters <= 4) return 0;
    return quarters;
}

uint8_t battle_ball_multiplier(const CatchInput* in) {
    switch (in->ball) {
    case BALL_ULTRA: return 20;
    case BALL_GREAT: return 15;
    case BALL_SAFARI: return 15;
    case BALL_NET:
        return (in->type1 == TYPE_WATER || in->type2 == TYPE_WATER ||
                in->type1 == TYPE_BUG || in->type2 == TYPE_BUG) ? 30 : 10;
    case BALL_DIVE:
        return in->underwater ? 35 : 10;
    case BALL_NEST:
        if (in->level < 40) {
            uint8_t m = (uint8_t)(40 - in->level);
            return m <= 9 ? 10 : m;
        }
        return 10;
    case BALL_REPEAT:
        return in->owned ? 30 : 10;
    case BALL_TIMER: {
        /* The game computes this in a u8, so very long battles wrap. */
        uint8_t m = (uint8_t)(in->turn_counter + 10);
        return m > 40 ? 40 : m;
    }
    default:
        return 10;
    }
}

uint32_t battle_isqrt(uint32_t n) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

uint16_t battle_catch_permille(const CatchInput* in) {
    if (in->ball == BALL_MASTER) return 1000;
    if (in->max_hp == 0) return 0;
    uint32_t max_hp = in->max_hp;
    uint32_t hp = in->hp > in->max_hp ? in->max_hp : in->hp;
    uint32_t odds = (uint32_t)in->catch_rate * battle_ball_multiplier(in) / 10;
    odds = odds * (3 * max_hp - 2 * hp) / (3 * max_hp);
    if (in->status1 & (STATUS1_SLEEP | STATUS1_FREEZE)) odds *= 2;
    if (in->status1 & (STATUS1_POISON | STATUS1_BURN | STATUS1_PARALYSIS | STATUS1_TOXIC))
        odds = odds * 15 / 10;
    if (odds > 254) return 1000;
    if (odds == 0) return 0;
    uint32_t b = 1048560u / battle_isqrt(battle_isqrt(16711680u / odds));
    double p = (b >= 65536u ? 65536.0 : (double)b) / 65536.0;
    return (uint16_t)(p * p * p * p * 1000.0 + 0.5);
}
