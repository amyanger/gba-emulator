#include "game/damage.h"

#include <string.h>

/* Game-code constants from pokeemerald (values listed in
 * docs/superpowers/specs/emerald-game-layer/damage-data.md unless noted). */

/* include/constants/pokemon.h */
#define TYPE_NORMAL   0
#define TYPE_GROUND   4
#define TYPE_ROCK     5
#define TYPE_BUG      6
#define TYPE_MYSTERY  9
#define TYPE_FIRE     10
#define TYPE_WATER    11
#define TYPE_GRASS    12
#define TYPE_ELECTRIC 13
#define TYPE_ICE      15
#define TYPE_FORESIGHT 0xFE
#define TYPE_ENDTABLE  0xFF

#define STAT_ATK   1
#define STAT_DEF   2
#define STAT_SPATK 4
#define STAT_SPDEF 5
#define MAX_STAT_STAGE 12

/* include/constants/abilities.h (STURDY, DAMP, SOUNDPROOF from the pret header) */
#define ABILITY_STURDY       5
#define ABILITY_VOLT_ABSORB  10
#define ABILITY_WATER_ABSORB 11
#define ABILITY_FLASH_FIRE   18
#define ABILITY_WONDER_GUARD 25
#define ABILITY_LEVITATE     26
#define ABILITY_HUGE_POWER   37
#define ABILITY_SOUNDPROOF   43
#define ABILITY_THICK_FAT    47
#define ABILITY_HUSTLE       55
#define ABILITY_PLUS         57
#define ABILITY_MINUS        58
#define ABILITY_GUTS         62
#define ABILITY_MARVEL_SCALE 63
#define ABILITY_OVERGROW     65
#define ABILITY_BLAZE        66
#define ABILITY_TORRENT      67
#define ABILITY_SWARM        68
#define ABILITY_PURE_POWER   74

/* include/constants/hold_effects.h */
#define HOLD_EFFECT_CHOICE_BAND     29
#define HOLD_EFFECT_SOUL_DEW        34
#define HOLD_EFFECT_DEEP_SEA_TOOTH  35
#define HOLD_EFFECT_DEEP_SEA_SCALE  36
#define HOLD_EFFECT_LIGHT_BALL      45
#define HOLD_EFFECT_METAL_POWDER    64
#define HOLD_EFFECT_THICK_CLUB      65

/* include/constants/species.h (internal ids) */
#define SPECIES_PIKACHU  25
#define SPECIES_CUBONE   104
#define SPECIES_MAROWAK  105
#define SPECIES_DITTO    132
#define SPECIES_CLAMPERL 373
#define SPECIES_LATIAS   407
#define SPECIES_LATIOS   408

#define ITEM_ENIGMA_BERRY 175

/* include/constants/moves.h (SURF, SNORE, UPROAR, HYPER_VOICE, WHIRLPOOL from the pret header) */
#define MOVE_SURF        57
#define MOVE_SOLAR_BEAM  76
#define MOVE_STRUGGLE    165
#define MOVE_SNORE       173
#define MOVE_WHIRLPOOL   250
#define MOVE_UPROAR      253
#define MOVE_HYPER_VOICE 304

/* include/constants/battle_move_effects.h */
#define EFFECT_HIT                    0
#define EFFECT_POISON_HIT             2
#define EFFECT_ABSORB                 3
#define EFFECT_BURN_HIT               4
#define EFFECT_FREEZE_HIT             5
#define EFFECT_PARALYZE_HIT           6
#define EFFECT_EXPLOSION              7
#define EFFECT_DREAM_EATER            8
#define EFFECT_ALWAYS_HIT             17
#define EFFECT_RAMPAGE                27
#define EFFECT_FLINCH_HIT             31
#define EFFECT_PAY_DAY                34
#define EFFECT_TRI_ATTACK             36
#define EFFECT_OHKO                   38
#define EFFECT_RAZOR_WIND             39
#define EFFECT_SUPER_FANG             40
#define EFFECT_DRAGON_RAGE            41
#define EFFECT_TRAP                   42
#define EFFECT_HIGH_CRITICAL          43
#define EFFECT_RECOIL_IF_MISS         45
#define EFFECT_RECOIL                 48
#define EFFECT_ATTACK_DOWN_HIT        68
#define EFFECT_DEFENSE_DOWN_HIT       69
#define EFFECT_SPEED_DOWN_HIT         70
#define EFFECT_SPECIAL_ATTACK_DOWN_HIT  71
#define EFFECT_SPECIAL_DEFENSE_DOWN_HIT 72
#define EFFECT_ACCURACY_DOWN_HIT      73
#define EFFECT_EVASION_DOWN_HIT       74
#define EFFECT_SKY_ATTACK             75
#define EFFECT_CONFUSE_HIT            76
#define EFFECT_VITAL_THROW            78
#define EFFECT_RECHARGE               80
#define EFFECT_RAGE                   81
#define EFFECT_LEVEL_DAMAGE           87
#define EFFECT_SNORE                  92
#define EFFECT_FLAIL                  99
#define EFFECT_FALSE_SWIPE            101
#define EFFECT_QUICK_ATTACK           103
#define EFFECT_THIEF                  105
#define EFFECT_RETURN                 121
#define EFFECT_FRUSTRATION            123
#define EFFECT_THAW_HIT               125
#define EFFECT_PURSUIT                128
#define EFFECT_RAPID_SPIN             129
#define EFFECT_SONICBOOM              130
#define EFFECT_HIDDEN_POWER           135
#define EFFECT_DEFENSE_UP_HIT         138
#define EFFECT_ATTACK_UP_HIT          139
#define EFFECT_ALL_STATS_UP_HIT       140
#define EFFECT_SKULL_BASH             145
#define EFFECT_TWISTER                146
#define EFFECT_EARTHQUAKE             147
#define EFFECT_GUST                   149
#define EFFECT_FLINCH_MINIMIZE_HIT    150
#define EFFECT_SOLAR_BEAM             151
#define EFFECT_THUNDER                152
#define EFFECT_SEMI_INVULNERABLE      155
#define EFFECT_FAKE_OUT               158
#define EFFECT_UPROAR                 159
#define EFFECT_SPIT_UP                161
#define EFFECT_FACADE                 169
#define EFFECT_FOCUS_PUNCH            170
#define EFFECT_SMELLINGSALT           171
#define EFFECT_SUPERPOWER             182
#define EFFECT_BRICK_BREAK            186
#define EFFECT_KNOCK_OFF              188
#define EFFECT_ENDEAVOR               189
#define EFFECT_ERUPTION               190
#define EFFECT_LOW_KICK               196
#define EFFECT_SECRET_POWER           197
#define EFFECT_DOUBLE_EDGE            198
#define EFFECT_BLAZE_KICK             200
#define EFFECT_POISON_FANG            202
#define EFFECT_WEATHER_BALL           203
#define EFFECT_OVERHEAT               204
#define EFFECT_SKY_UPPERCUT           207
#define EFFECT_POISON_TAIL            209
#define NUM_BATTLE_MOVE_EFFECTS       214

/* include/constants/battle.h */
#define BATTLE_TYPE_DOUBLE   (1u << 0)
#define BATTLE_TYPE_SAFARI   (1u << 7)
#define BATTLE_TYPE_FRONTIER 0x003F0100u

#define B_WEATHER_RAIN_TEMPORARY 0x01
#define B_WEATHER_RAIN           0x07
#define B_WEATHER_SANDSTORM      0x18
#define B_WEATHER_SUN            0x60
#define B_WEATHER_HAIL           0x80
#define B_WEATHER_ANY            0xFF

#define SIDE_STATUS_REFLECT     0x01
#define SIDE_STATUS_LIGHTSCREEN 0x02

#define STATUS1_SLEEP      0x07
#define STATUS1_POISON     0x08
#define STATUS1_BURN       0x10
#define STATUS1_FREEZE     0x20
#define STATUS1_PARALYSIS  0x40
#define STATUS1_TOXIC_POISON 0x80
#define STATUS2_MULTIPLETURNS (1u << 12)
#define STATUS2_SUBSTITUTE (1u << 24)
#define STATUS2_FORESIGHT  (1u << 29)
#define STATUS3_ON_AIR      (1u << 6)
#define STATUS3_UNDERGROUND (1u << 7)
#define STATUS3_MINIMIZED   (1u << 8)
#define STATUS3_CHARGED_UP  (1u << 9)
#define STATUS3_UNDERWATER  (1u << 18)
#define STATUS3_SEMI_INVULNERABLE (STATUS3_ON_AIR | STATUS3_UNDERGROUND | STATUS3_UNDERWATER)

/* include/battle.h */
#define MOVE_TARGET_BOTH 0x08
#define IS_TYPE_PHYSICAL(t) ((t) < TYPE_MYSTERY)
#define IS_TYPE_SPECIAL(t)  ((t) > TYPE_MYSTERY)

/* Badge bits in DamageMon.badges: FLAG_BADGE01/05/07_GET. */
#define BADGE_STONE   (1u << 0)
#define BADGE_BALANCE (1u << 4)
#define BADGE_MIND    (1u << 6)

/* Move result flags tracked through the type chart (include/constants/battle.h). */
#define RESULT_SUPER_EFFECTIVE (1u << 1)
#define RESULT_NOT_EFFECTIVE   (1u << 2)
#define RESULT_DOESNT_AFFECT   (1u << 3)

/* How each EFFECT_* is predicted. Anything not listed falls back to DMG_NONE.
 * PLAIN: damage-formula.md section 5 "Exact min-max" effects that run the normal pipeline with
 * table power (Dream Eater, Snore, Fake Out and Explosion after their failure checks).
 * VARIABLE: power, type or dmgMultiplier set by the move's script (section 3a).
 * FIXED: set damage, no roll (section 3b). OHKO: shown as OHKO or fails. */
enum { CLASS_FALLBACK = 0, CLASS_PLAIN, CLASS_VARIABLE, CLASS_FIXED, CLASS_OHKO };

static const uint8_t k_effect_class[NUM_BATTLE_MOVE_EFFECTS] = {
    [EFFECT_HIT] = CLASS_PLAIN,
    [EFFECT_POISON_HIT] = CLASS_PLAIN,
    [EFFECT_ABSORB] = CLASS_PLAIN,
    [EFFECT_BURN_HIT] = CLASS_PLAIN,
    [EFFECT_FREEZE_HIT] = CLASS_PLAIN,
    [EFFECT_PARALYZE_HIT] = CLASS_PLAIN,
    [EFFECT_EXPLOSION] = CLASS_PLAIN,
    [EFFECT_DREAM_EATER] = CLASS_PLAIN,
    [EFFECT_ALWAYS_HIT] = CLASS_PLAIN,
    [EFFECT_RAMPAGE] = CLASS_PLAIN,
    [EFFECT_FLINCH_HIT] = CLASS_PLAIN,
    [EFFECT_PAY_DAY] = CLASS_PLAIN,
    [EFFECT_TRI_ATTACK] = CLASS_PLAIN,
    [EFFECT_RAZOR_WIND] = CLASS_PLAIN,
    [EFFECT_TRAP] = CLASS_PLAIN,
    [EFFECT_HIGH_CRITICAL] = CLASS_PLAIN,
    [EFFECT_RECOIL_IF_MISS] = CLASS_PLAIN,
    [EFFECT_RECOIL] = CLASS_PLAIN,
    [EFFECT_ATTACK_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_DEFENSE_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_SPEED_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_SPECIAL_ATTACK_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_SPECIAL_DEFENSE_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_ACCURACY_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_EVASION_DOWN_HIT] = CLASS_PLAIN,
    [EFFECT_SKY_ATTACK] = CLASS_PLAIN,
    [EFFECT_CONFUSE_HIT] = CLASS_PLAIN,
    [EFFECT_VITAL_THROW] = CLASS_PLAIN,
    [EFFECT_RECHARGE] = CLASS_PLAIN,
    [EFFECT_RAGE] = CLASS_PLAIN,
    [EFFECT_FALSE_SWIPE] = CLASS_PLAIN,
    [EFFECT_QUICK_ATTACK] = CLASS_PLAIN,
    [EFFECT_THIEF] = CLASS_PLAIN,
    [EFFECT_THAW_HIT] = CLASS_PLAIN,
    [EFFECT_PURSUIT] = CLASS_PLAIN,
    [EFFECT_RAPID_SPIN] = CLASS_PLAIN,
    [EFFECT_DEFENSE_UP_HIT] = CLASS_PLAIN,
    [EFFECT_ATTACK_UP_HIT] = CLASS_PLAIN,
    [EFFECT_ALL_STATS_UP_HIT] = CLASS_PLAIN,
    [EFFECT_SKULL_BASH] = CLASS_PLAIN,
    [EFFECT_SOLAR_BEAM] = CLASS_PLAIN,
    [EFFECT_THUNDER] = CLASS_PLAIN,
    [EFFECT_SEMI_INVULNERABLE] = CLASS_PLAIN,
    [EFFECT_UPROAR] = CLASS_PLAIN,
    [EFFECT_FOCUS_PUNCH] = CLASS_PLAIN,
    [EFFECT_SUPERPOWER] = CLASS_PLAIN,
    [EFFECT_BRICK_BREAK] = CLASS_PLAIN,
    [EFFECT_KNOCK_OFF] = CLASS_PLAIN,
    [EFFECT_SECRET_POWER] = CLASS_PLAIN,
    [EFFECT_DOUBLE_EDGE] = CLASS_PLAIN,
    [EFFECT_BLAZE_KICK] = CLASS_PLAIN,
    [EFFECT_POISON_FANG] = CLASS_PLAIN,
    [EFFECT_OVERHEAT] = CLASS_PLAIN,
    [EFFECT_SKY_UPPERCUT] = CLASS_PLAIN,
    [EFFECT_POISON_TAIL] = CLASS_PLAIN,
    [EFFECT_SNORE] = CLASS_PLAIN,
    [EFFECT_FAKE_OUT] = CLASS_PLAIN,

    [EFFECT_RETURN] = CLASS_VARIABLE,
    [EFFECT_FRUSTRATION] = CLASS_VARIABLE,
    [EFFECT_FLAIL] = CLASS_VARIABLE,
    [EFFECT_ERUPTION] = CLASS_VARIABLE,
    [EFFECT_HIDDEN_POWER] = CLASS_VARIABLE,
    [EFFECT_LOW_KICK] = CLASS_VARIABLE,
    [EFFECT_FACADE] = CLASS_VARIABLE,
    [EFFECT_SMELLINGSALT] = CLASS_VARIABLE,
    [EFFECT_WEATHER_BALL] = CLASS_VARIABLE,
    [EFFECT_GUST] = CLASS_VARIABLE,
    [EFFECT_TWISTER] = CLASS_VARIABLE,
    [EFFECT_EARTHQUAKE] = CLASS_VARIABLE,
    [EFFECT_FLINCH_MINIMIZE_HIT] = CLASS_VARIABLE,

    [EFFECT_LEVEL_DAMAGE] = CLASS_FIXED,
    [EFFECT_DRAGON_RAGE] = CLASS_FIXED,
    [EFFECT_SONICBOOM] = CLASS_FIXED,
    [EFFECT_SUPER_FANG] = CLASS_FIXED,
    [EFFECT_ENDEAVOR] = CLASS_FIXED,
    [EFFECT_SPIT_UP] = CLASS_FIXED,

    [EFFECT_OHKO] = CLASS_OHKO,
};

uint8_t damage_move_type(const DamageCtx* ctx, const DamageMon* atk, uint16_t move_id, const DamageMove* mv) {
    (void)move_id;
    if (mv->effect == EFFECT_HIDDEN_POWER) {
        /* Cmd_hiddenpowercalc: bit 0 of each IV, order HP, Atk, Def, Spe, SpA, SpD. */
        uint32_t bits = 0;
        for (int i = 0; i < 6; i++) bits |= ((atk->iv_word >> (5 * i)) & 1u) << i;
        uint8_t type = (uint8_t)(15 * bits / 63 + 1);
        if (type >= TYPE_MYSTERY) type++;
        return type;
    }
    if (mv->effect == EFFECT_WEATHER_BALL && ctx->weather_has_effect) {
        /* Cmd_setweatherballtype */
        if (ctx->weather & B_WEATHER_RAIN) return TYPE_WATER;
        if (ctx->weather & B_WEATHER_SANDSTORM) return TYPE_ROCK;
        if (ctx->weather & B_WEATHER_SUN) return TYPE_FIRE;
        if (ctx->weather & B_WEATHER_HAIL) return TYPE_ICE;
        return TYPE_NORMAL;
    }
    return mv->type;
}

/* APPLY_STAT_MOD. Returns -1 for a stage outside the table or a zero denominator. */
static int32_t stat_mod(const DamageTables* t, uint32_t stat, uint8_t stage) {
    if (stage > MAX_STAT_STAGE || t->stat_ratio[stage][1] == 0) return -1;
    int32_t v = (int32_t)(stat * t->stat_ratio[stage][0]);
    return v / t->stat_ratio[stage][1];
}

static bool badge(const DamageMon* m, uint8_t bit) {
    return m->badges_apply && (m->badges & bit);
}

/* Screens and spread moves: CountAliveMonsInBattle(BATTLE_ALIVE_DEF_SIDE) == 2 in doubles. */
static bool two_on_def_side(const DamageCtx* ctx, const DamageMon* def) {
    return (ctx->battle_flags & BATTLE_TYPE_DOUBLE) && ctx->present_on_side[def->side & 1] == 2;
}

/* (stat * power * level factor) / def / 50 as the game's s32 math does it: the products wrap
 * like the ARM multiply (done in u32 to avoid C overflow UB), the divisions are signed. */
static int32_t scaled(int32_t a, uint16_t power, int32_t level_factor, int32_t d) {
    int32_t prod = (int32_t)((uint32_t)a * power * (uint32_t)level_factor);
    return prod / d / 50;
}

/* CalculateBaseDamage (src/pokemon.c:3106) for a non-critical hit, including the final +2.
 * `power` is the table power or the script's gDynamicBasePower. Returns false when a divisor
 * would be zero or the s32 math wrapped negative (absurd stats). */
static bool base_damage(const DamageTables* t, const DamageCtx* ctx, const DamageMon* atk,
                        const DamageMon* def, uint16_t move_id, const DamageMove* mv, uint8_t type,
                        uint16_t power, uint16_t side_status, int32_t* out) {
    uint16_t attack = atk->attack, defense = def->defense;
    uint16_t sp_attack = atk->sp_attack, sp_defense = def->sp_defense;
    uint8_t ahe = atk->hold_effect, dhe = def->hold_effect;
    bool frontier = (ctx->battle_flags & BATTLE_TYPE_FRONTIER) != 0;

    if (atk->ability == ABILITY_HUGE_POWER || atk->ability == ABILITY_PURE_POWER) attack *= 2;

    if (badge(atk, BADGE_STONE)) attack = (uint16_t)(110 * attack / 100);
    if (badge(def, BADGE_BALANCE)) defense = (uint16_t)(110 * defense / 100);
    if (badge(atk, BADGE_MIND)) sp_attack = (uint16_t)(110 * sp_attack / 100);
    if (badge(def, BADGE_MIND)) sp_defense = (uint16_t)(110 * sp_defense / 100);

    uint8_t n = t->hold_type_count > DMG_HOLD_TYPE_MAX ? DMG_HOLD_TYPE_MAX : t->hold_type_count;
    for (uint8_t i = 0; i < n; i++) {
        if (ahe == t->hold_type[i][0] && type == t->hold_type[i][1]) {
            if (IS_TYPE_PHYSICAL(type))
                attack = (uint16_t)(attack * (atk->hold_param + 100) / 100);
            else
                sp_attack = (uint16_t)(sp_attack * (atk->hold_param + 100) / 100);
            break;
        }
    }

    if (ahe == HOLD_EFFECT_CHOICE_BAND) attack = (uint16_t)(150 * attack / 100);
    if (ahe == HOLD_EFFECT_SOUL_DEW && !frontier &&
        (atk->species == SPECIES_LATIAS || atk->species == SPECIES_LATIOS))
        sp_attack = (uint16_t)(150 * sp_attack / 100);
    if (dhe == HOLD_EFFECT_SOUL_DEW && !frontier &&
        (def->species == SPECIES_LATIAS || def->species == SPECIES_LATIOS))
        sp_defense = (uint16_t)(150 * sp_defense / 100);
    if (ahe == HOLD_EFFECT_DEEP_SEA_TOOTH && atk->species == SPECIES_CLAMPERL) sp_attack *= 2;
    if (dhe == HOLD_EFFECT_DEEP_SEA_SCALE && def->species == SPECIES_CLAMPERL) sp_defense *= 2;
    if (ahe == HOLD_EFFECT_LIGHT_BALL && atk->species == SPECIES_PIKACHU) sp_attack *= 2;
    if (dhe == HOLD_EFFECT_METAL_POWDER && def->species == SPECIES_DITTO) defense *= 2;
    if (ahe == HOLD_EFFECT_THICK_CLUB &&
        (atk->species == SPECIES_CUBONE || atk->species == SPECIES_MAROWAK))
        attack *= 2;

    if (def->ability == ABILITY_THICK_FAT && (type == TYPE_FIRE || type == TYPE_ICE)) sp_attack /= 2;
    if (atk->ability == ABILITY_HUSTLE) attack = (uint16_t)(150 * attack / 100);
    if (atk->ability == ABILITY_PLUS && ctx->minus_on_field) sp_attack = (uint16_t)(150 * sp_attack / 100);
    if (atk->ability == ABILITY_MINUS && ctx->plus_on_field) sp_attack = (uint16_t)(150 * sp_attack / 100);
    if (atk->ability == ABILITY_GUTS && atk->status1) attack = (uint16_t)(150 * attack / 100);
    if (def->ability == ABILITY_MARVEL_SCALE && def->status1) defense = (uint16_t)(150 * defense / 100);
    if (type == TYPE_ELECTRIC && ctx->mud_sport) power /= 2;
    if (type == TYPE_FIRE && ctx->water_sport) power /= 2;
    bool pinch = atk->hp <= atk->max_hp / 3;
    if (pinch && ((type == TYPE_GRASS && atk->ability == ABILITY_OVERGROW) ||
                  (type == TYPE_FIRE && atk->ability == ABILITY_BLAZE) ||
                  (type == TYPE_WATER && atk->ability == ABILITY_TORRENT) ||
                  (type == TYPE_BUG && atk->ability == ABILITY_SWARM)))
        power = (uint16_t)(150 * power / 100);

    if (mv->effect == EFFECT_EXPLOSION) defense /= 2;

    int32_t damage = 0;
    int32_t level_factor = 2 * atk->level / 5 + 2;
    bool two = two_on_def_side(ctx, def);
    bool spread = two && mv->target == MOVE_TARGET_BOTH;

    if (IS_TYPE_PHYSICAL(type)) {
        int32_t a = stat_mod(t, attack, atk->stages[STAT_ATK]);
        int32_t d = stat_mod(t, defense, def->stages[STAT_DEF]);
        if (a < 0 || d <= 0) return false;
        damage = scaled(a, power, level_factor, d);
        if ((atk->status1 & STATUS1_BURN) && atk->ability != ABILITY_GUTS) damage /= 2;
        if (side_status & SIDE_STATUS_REFLECT) damage = two ? 2 * (damage / 3) : damage / 2;
        if (spread) damage /= 2;
        if (damage == 0) damage = 1;
    }

    if (type == TYPE_MYSTERY) damage = 0;

    if (IS_TYPE_SPECIAL(type)) {
        int32_t a = stat_mod(t, sp_attack, atk->stages[STAT_SPATK]);
        int32_t d = stat_mod(t, sp_defense, def->stages[STAT_SPDEF]);
        if (a < 0 || d <= 0) return false;
        damage = scaled(a, power, level_factor, d);
        if (side_status & SIDE_STATUS_LIGHTSCREEN) damage = two ? 2 * (damage / 3) : damage / 2;
        if (spread) damage /= 2;
        if (!ctx->weather_negated) {
            if (ctx->weather & B_WEATHER_RAIN_TEMPORARY) {
                if (type == TYPE_FIRE) damage /= 2;
                else if (type == TYPE_WATER) damage = 15 * damage / 10;
            }
            if ((ctx->weather & (B_WEATHER_RAIN | B_WEATHER_SANDSTORM | B_WEATHER_HAIL)) &&
                move_id == MOVE_SOLAR_BEAM)
                damage /= 2;
            if (ctx->weather & B_WEATHER_SUN) {
                if (type == TYPE_FIRE) damage = 15 * damage / 10;
                else if (type == TYPE_WATER) damage /= 2;
            }
        }
        if (atk->flash_fire && type == TYPE_FIRE) damage = 15 * damage / 10;
    }

    if (damage < 0) return false;
    *out = damage + 2;
    return true;
}

/* ModulateDmgByType, including the result flags Wonder Guard reads. */
static void modulate(int32_t* damage, uint8_t mult, uint8_t power, uint8_t* flags) {
    *damage = *damage * mult / 10;
    if (*damage == 0 && mult != 0) *damage = 1;
    bool no_effect = (*flags & RESULT_DOESNT_AFFECT) != 0;
    if (mult == 0) {
        *flags |= RESULT_DOESNT_AFFECT;
        *flags &= (uint8_t)~(RESULT_NOT_EFFECTIVE | RESULT_SUPER_EFFECTIVE);
    } else if (mult == 5 && power && !no_effect) {
        if (*flags & RESULT_SUPER_EFFECTIVE) *flags &= (uint8_t)~RESULT_SUPER_EFFECTIVE;
        else *flags |= RESULT_NOT_EFFECTIVE;
    } else if (mult == 20 && power && !no_effect) {
        if (*flags & RESULT_NOT_EFFECTIVE) *flags &= (uint8_t)~RESULT_NOT_EFFECTIVE;
        else *flags |= RESULT_SUPER_EFFECTIVE;
    }
}

/* Cmd_typecalc after STAB: Levitate, chart rows in table order, Wonder Guard.
 * Returns false when the move does not affect the target. */
static bool type_chart(const DamageTables* t, const DamageMon* def, uint8_t type, uint8_t power,
                       int32_t* damage) {
    if (def->ability == ABILITY_LEVITATE && type == TYPE_GROUND) return false;
    uint8_t flags = 0;
    uint32_t len = t->type_len > DMG_TYPE_TABLE_MAX ? DMG_TYPE_TABLE_MAX : t->type_len;
    for (uint32_t i = 0; i + 2 < len; i += 3) {
        uint8_t a = t->type_chart[i], d = t->type_chart[i + 1], mult = t->type_chart[i + 2];
        if (a == TYPE_ENDTABLE) break;
        if (a == TYPE_FORESIGHT) {
            if (def->status2 & STATUS2_FORESIGHT) break;
            continue;
        }
        if (a != type) continue;
        if (d == def->type1) modulate(damage, mult, power, &flags);
        if (d == def->type2 && def->type1 != def->type2) modulate(damage, mult, power, &flags);
    }
    if (flags & RESULT_DOESNT_AFFECT) return false;
    if (def->ability == ABILITY_WONDER_GUARD && power) {
        uint8_t se_nve = flags & (RESULT_SUPER_EFFECTIVE | RESULT_NOT_EFFECTIVE);
        if (!(flags & RESULT_SUPER_EFFECTIVE) || se_nve == (RESULT_SUPER_EFFECTIVE | RESULT_NOT_EFFECTIVE))
            return false;
    }
    return true;
}

/* Abilities that stop the move before damage: absorbers (AbilityBattleEffects ABSORBING) and
 * Soundproof against its damaging sound moves. */
static bool blocked_by_ability(const DamageMon* def, uint16_t move_id, const DamageMove* mv, uint8_t type) {
    switch (def->ability) {
    case ABILITY_VOLT_ABSORB: return type == TYPE_ELECTRIC && mv->power != 0;
    case ABILITY_WATER_ABSORB: return type == TYPE_WATER && mv->power != 0;
    case ABILITY_FLASH_FIRE: return type == TYPE_FIRE && !(def->status1 & STATUS1_FREEZE);
    case ABILITY_SOUNDPROOF:
        return move_id == MOVE_SNORE || move_id == MOVE_UPROAR || move_id == MOVE_HYPER_VOICE;
    default: return false;
    }
}

/* Keeps every later s32 step (dmgMultiplier, Charge, Spit Up's stockpile, STAB, chart rows and
 * the 85..100 roll) inside int32, so no wrap ever needs modelling. Real base damage is far lower. */
#define BASE_DAMAGE_MAX 800000

/* The target state a move deals x2 against (dmgMultiplier 2 in its script), else 0. */
static uint32_t doubling_state(uint16_t move_id, const DamageMove* mv) {
    switch (mv->effect) {
    case EFFECT_GUST:                                    /* battle_scripts_1.s:1892-1896 */
    case EFFECT_TWISTER: return STATUS3_ON_AIR;          /* :1826-1829 */
    case EFFECT_EARTHQUAKE: return STATUS3_UNDERGROUND;  /* :1841-1843 */
    case EFFECT_FLINCH_MINIMIZE_HIT: return STATUS3_MINIMIZED; /* :1898-1901 */
    case EFFECT_HIT: return move_id == MOVE_SURF ? STATUS3_UNDERWATER : 0;      /* :237-240 */
    case EFFECT_TRAP: return move_id == MOVE_WHIRLPOOL ? STATUS3_UNDERWATER : 0; /* :830-834 */
    default: return 0;
    }
}

/* Power (gDynamicBasePower) and dmgMultiplier that a CLASS_VARIABLE script sets before damagecalc.
 * Returns false when an input is unknown. */
static bool variable_power(const DamageTables* t, const DamageCtx* ctx, const DamageMon* atk,
                           const DamageMon* def, const DamageMove* mv, uint16_t* power, int32_t* mult) {
    uint16_t dyn = 0;
    switch (mv->effect) {
    case EFFECT_RETURN: /* Cmd_friendshiptodamagecalculation */
        dyn = (uint16_t)(10 * atk->friendship / 25);
        break;
    case EFFECT_FRUSTRATION:
        dyn = (uint16_t)(10 * (255 - atk->friendship) / 25);
        break;
    case EFFECT_FLAIL: { /* Cmd_remaininghptopower, GetScaledHPFraction (battle_interface.c:2517) */
        int16_t hp = (int16_t)atk->hp, max_hp = (int16_t)atk->max_hp;
        if (max_hp <= 0 || hp < 0) return false;
        uint8_t f = (uint8_t)(hp * 48 / max_hp);
        if (f == 0 && hp > 0) f = 1;
        uint8_t i = 0;
        while (i < 6 && f > t->flail[i][0]) i++;
        if (i == 6) return false;
        dyn = t->flail[i][1];
        break;
    }
    case EFFECT_ERUPTION: /* Cmd_scaledamagebyhealthratio */
        if (atk->max_hp == 0) return false;
        dyn = (uint16_t)(atk->hp * mv->power / atk->max_hp);
        if (dyn == 0) dyn = 1;
        break;
    case EFFECT_HIDDEN_POWER: { /* Cmd_hiddenpowercalc: bit 1 of each IV */
        uint32_t bits = 0;
        for (int i = 0; i < 6; i++) bits |= ((atk->iv_word >> (5 * i + 1)) & 1u) << i;
        dyn = (uint16_t)(40 * bits / 63 + 30);
        break;
    }
    case EFFECT_LOW_KICK: { /* Cmd_weightdamagecalculation: first key > weight, 0xFFFF ends at 120 */
        if (def->weight_hg == 0) return false;
        uint8_t i = 0;
        while (i < 6 && t->weight[i][0] != 0xFFFF && t->weight[i][0] <= def->weight_hg) i++;
        if (i == 6) return false;
        dyn = t->weight[i][0] == 0xFFFF ? 120 : t->weight[i][1];
        break;
    }
    case EFFECT_FACADE: /* battle_scripts_1.s:2252-2258 */
        if (atk->status1 & (STATUS1_POISON | STATUS1_BURN | STATUS1_PARALYSIS | STATUS1_TOXIC_POISON))
            *mult = 2;
        break;
    case EFFECT_SMELLINGSALT: /* :2268-2275 */
        if (!(def->status2 & STATUS2_SUBSTITUTE) && (def->status1 & STATUS1_PARALYSIS)) *mult = 2;
        break;
    case EFFECT_WEATHER_BALL: /* Cmd_setweatherballtype; the type comes from damage_move_type */
        if (ctx->weather_has_effect && (ctx->weather & B_WEATHER_ANY)) *mult = 2;
        break;
    default:
        break;
    }
    if (dyn) *power = dyn; /* pokemon.c:3119-3122: a dynamic power of 0 keeps the table power */
    return true;
}

/* damagecalc, typecalc and the random roll (Cmd_damagecalc .. adjustnormaldamage) for one
 * dmgMultiplier. Returns DMG_RANGE, DMG_IMMUNE or DMG_NONE. */
static uint8_t normal_hit(const DamageTables* t, const DamageCtx* ctx, const DamageMon* atk,
                          const DamageMon* def, uint16_t move_id, const DamageMove* mv, uint8_t type,
                          uint16_t power, int32_t mult, uint16_t side_status, uint32_t* lo, uint32_t* hi) {
    int32_t damage;
    if (!base_damage(t, ctx, atk, def, move_id, mv, type, power, side_status, &damage)) return DMG_NONE;
    if (damage > BASE_DAMAGE_MAX) return DMG_NONE;
    damage *= mult;

    /* Cmd_damagecalc: Charge uses the move's table type. */
    if ((atk->status3 & STATUS3_CHARGED_UP) && mv->type == TYPE_ELECTRIC) damage *= 2;

    /* Cmd_typecalc; Struggle returns before STAB and the chart. */
    if (move_id != MOVE_STRUGGLE) {
        if (atk->type1 == type || atk->type2 == type) damage = damage * 15 / 10;
        if (!type_chart(t, def, type, mv->power, &damage)) return DMG_IMMUNE;
    }

    /* ApplyRandomDmgMultiplier: 85..100 percent, applied last. */
    *hi = (uint32_t)damage;
    *lo = *hi * 85 / 100;
    if (*hi != 0 && *lo == 0) *lo = 1;
    return DMG_RANGE;
}

/* Section 3b: set damage, then typecalc only for immunity, Levitate and Wonder Guard (table power
 * is 1, so Wonder Guard applies), adjustsetdamage without a roll. Spit Up keeps STAB and chart. */
static void fixed_damage(const DamageTables* t, const DamageCtx* ctx, const DamageMon* atk,
                         const DamageMon* def, uint16_t move_id, const DamageMove* mv, uint8_t type,
                         uint16_t side_status, DamageResult* out) {
    int32_t damage;
    switch (mv->effect) {
    case EFFECT_LEVEL_DAMAGE: damage = atk->level; break; /* Cmd_dmgtolevel */
    case EFFECT_DRAGON_RAGE: damage = 40; break;
    case EFFECT_SONICBOOM: damage = 20; break;
    case EFFECT_SUPER_FANG: /* Cmd_damagetohalftargethp */
        damage = def->hp / 2;
        if (damage == 0) damage = 1;
        break;
    case EFFECT_ENDEAVOR: /* Cmd_setdamagetohealthdifference fails before typecalc */
        if (def->hp <= atk->hp) {
            out->kind = DMG_FAILS;
            return;
        }
        damage = def->hp - atk->hp;
        break;
    case EFFECT_SPIT_UP: /* Cmd_stockpiletobasedamage: fails at 0 before typecalc; no dynamic type */
        if (atk->stockpile == 0) {
            out->kind = DMG_FAILS;
            return;
        }
        if (!base_damage(t, ctx, atk, def, move_id, mv, mv->type, mv->power, side_status, &damage)) return;
        if (damage > BASE_DAMAGE_MAX) return;
        damage *= atk->stockpile;
        if (atk->type1 == type || atk->type2 == type) damage = damage * 15 / 10;
        if (!type_chart(t, def, type, mv->power, &damage)) {
            out->kind = DMG_IMMUNE;
            return;
        }
        out->kind = DMG_FIXED;
        out->lo = out->hi = (uint32_t)damage;
        return;
    default:
        return;
    }
    int32_t scratch = damage; /* typecalc's own math is overwritten by the set damage */
    if (!type_chart(t, def, type, mv->power, &scratch)) {
        out->kind = DMG_IMMUNE;
        return;
    }
    out->kind = DMG_FIXED;
    out->lo = out->hi = (uint32_t)damage;
}

/* Effects whose script stops before the hit (R5), checked after attackcanceler's Soundproof. */
static bool move_fails(const DamageCtx* ctx, const DamageMon* atk, const DamageMon* def, const DamageMove* mv) {
    switch (mv->effect) {
    case EFFECT_DREAM_EATER: /* battle_scripts_1.s:425-428 */
        return (def->status2 & STATUS2_SUBSTITUTE) || !(def->status1 & STATUS1_SLEEP);
    case EFFECT_SNORE: return !(atk->status1 & STATUS1_SLEEP); /* :1257-1262 */
    case EFFECT_FAKE_OUT: return !atk->first_turn;              /* Cmd_jumpifnotfirstturn */
    case EFFECT_EXPLOSION: return ctx->damp_on_field;           /* Cmd_tryexplosion: any battler */
    default: return false;
    }
}

void damage_predict(const DamageTables* t, const DamageCtx* ctx, const DamageMon* atk,
                    const DamageMon* def, uint16_t move_id, const DamageMove* mv, DamageResult* out) {
    memset(out, 0, sizeof(*out));
    out->kind = DMG_NONE;
    out->type = damage_move_type(ctx, atk, move_id, mv);

    if (!t->valid || (ctx->battle_flags & BATTLE_TYPE_SAFARI)) return;
    if (atk->hp == 0 || def->hp == 0 || atk->absent || def->absent) return;
    if (atk->item == ITEM_ENIGMA_BERRY || def->item == ITEM_ENIGMA_BERRY) return;
    if (mv->effect >= NUM_BATTLE_MOVE_EFFECTS) return;
    uint8_t cls = k_effect_class[mv->effect];
    if (cls == CLASS_FALLBACK) return;

    /* R11: a locked or charging attacker drops the variable-power and doubling rows. */
    uint32_t state = doubling_state(move_id, mv);
    if ((cls == CLASS_VARIABLE || state) && (atk->status2 & STATUS2_MULTIPLETURNS)) return;

    uint8_t type = out->type;
    if (blocked_by_ability(def, move_id, mv, type)) {
        out->kind = DMG_IMMUNE;
        return;
    }
    if (move_fails(ctx, atk, def, mv)) {
        out->kind = DMG_FAILS;
        return;
    }

    /* Brick Break clears the screens on the attacker's opposing side before damagecalc. */
    uint16_t side_status = ctx->side_status[def->side & 1];
    if (mv->effect == EFFECT_BRICK_BREAK && def->side != atk->side) side_status = 0;

    /* R10: a target in Fly/Dig/Dive may land before or after the hit, so never colour a KO. */
    out->no_ko = (def->status2 & STATUS2_SUBSTITUTE) || (def->status3 & STATUS3_SEMI_INVULNERABLE);
    out->leaves_one = mv->effect == EFFECT_FALSE_SWIPE;

    if (cls == CLASS_FIXED) {
        fixed_damage(t, ctx, atk, def, move_id, mv, type, side_status, out);
        return;
    }
    if (cls == CLASS_OHKO) {
        /* typecalc first (immunity, Levitate, Wonder Guard), then Cmd_tryKO: Sturdy, level. */
        int32_t scratch = 1;
        if (!type_chart(t, def, type, mv->power, &scratch)) {
            out->kind = DMG_IMMUNE;
        } else if (def->ability == ABILITY_STURDY || atk->level < def->level) {
            out->kind = DMG_FAILS;
        } else {
            out->kind = DMG_OHKO;
            out->lo = out->hi = def->hp;
        }
        return;
    }

    uint16_t power = mv->power;
    int32_t mult = 1;
    if (cls == CLASS_VARIABLE && !variable_power(t, ctx, atk, def, mv, &power, &mult)) return;
    bool doubled = state && (def->status3 & state);
    if (doubled) mult = 2;

    uint32_t lo, hi;
    uint8_t kind = normal_hit(t, ctx, atk, def, move_id, mv, type, power, mult, side_status, &lo, &hi);
    if (kind == DMG_RANGE && doubled && (state & STATUS3_SEMI_INVULNERABLE)) {
        /* R10: the target may have landed first, so the low end is the x1 hit. */
        uint32_t lo1, hi1;
        if (normal_hit(t, ctx, atk, def, move_id, mv, type, power, 1, side_status, &lo1, &hi1) != DMG_RANGE)
            return;
        lo = lo1;
    }
    out->kind = kind;
    if (kind == DMG_RANGE) {
        out->lo = lo;
        out->hi = hi;
    }
}
