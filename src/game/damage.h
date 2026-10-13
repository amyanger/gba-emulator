#ifndef DAMAGE_H
#define DAMAGE_H
#include <stdbool.h>
#include <stdint.h>

#define DMG_TYPE_TABLE_MAX 512
#define DMG_HOLD_TYPE_MAX  24

/* ROM tables, copied once by game_init (Task 3). Tests fill them by hand. */
typedef struct {
    bool valid;
    uint8_t type_chart[DMG_TYPE_TABLE_MAX]; /* gTypeEffectiveness triplets */
    uint16_t type_len;
    uint8_t stat_ratio[13][2];              /* gStatStageRatios {numerator, denominator} */
    uint8_t hold_type[DMG_HOLD_TYPE_MAX][2];/* sHoldEffectToType {holdEffect, type} */
    uint8_t hold_type_count;
    uint8_t flail[6][2];                    /* sFlailHpScaleToPowerTable {key, power} */
    uint16_t weight[6][2];                  /* sWeightToDamageTable {key, power}, u16 pairs in ROM */
} DamageTables;

/* gBattleMoves row (12 bytes). */
typedef struct {
    uint8_t effect, power, type, accuracy, pp, chance, target;
    int8_t priority;
    uint8_t flags;
} DamageMove;

/* One battler, all fields from gBattleMons except where noted. */
typedef struct {
    uint8_t battler, side;          /* battler id 0-3; side = position & 1 (0 player) */
    bool absent;                    /* gAbsentBattlerFlags bit */
    uint16_t species;
    uint16_t attack, defense, speed, sp_attack, sp_defense;
    uint32_t iv_word;               /* BMON +0x14 */
    uint8_t stages[8];              /* 0..12, 6 neutral; HP, ATK, DEF, SPEED, SPATK, SPDEF, ACC, EVA */
    uint8_t ability, type1, type2;
    uint16_t hp, max_hp;
    uint8_t level, friendship;
    uint16_t item;
    uint8_t hold_effect, hold_param;/* from gItems; 0 when item id out of range (GetItemHoldEffect) */
    uint32_t status1, status2, status3; /* status3 = gStatuses3[battler] */
    bool badges_apply;              /* ShouldGetStatBadgeBoost side/battle-type/secret-base gate */
    uint8_t badges;                 /* bit n = FLAG_BADGE0(n+1)_GET */
    bool flash_fire;                /* gBattleResources->flags->flags[battler] & 1 */
    bool first_turn;                /* gDisableStructs[battler].isFirstTurn */
    uint8_t stockpile;              /* gDisableStructs[battler].stockpileCounter */
    uint16_t weight_hg;             /* Pokedex weight (hectograms) of species; 0 = unknown */
} DamageMon;

typedef struct {
    uint32_t battle_flags;          /* gBattleTypeFlags */
    uint16_t weather;               /* gBattleWeather */
    bool weather_negated;           /* Cloud Nine/Air Lock via ABILITY_ON_FIELD2 (no HP check), base damage */
    bool weather_has_effect;        /* WEATHER_HAS_EFFECT (HP-checked), Weather Ball only */
    uint16_t side_status[2];        /* gSideStatuses */
    uint8_t present_on_side[2];     /* non-absent battlers per side (Reflect/Light Screen 2/3, spread) */
    bool mud_sport, water_sport;    /* any battler's gStatuses3 bit */
    bool damp_on_field;             /* any battler has Damp (as Cmd_tryexplosion checks) */
    bool minus_on_field, plus_on_field; /* ABILITY_ON_FIELD2 for Plus/Minus */
} DamageCtx;

typedef enum {
    DMG_NONE = 0, /* not modelled: show the type multiplier only */
    DMG_RANGE,    /* lo..hi, 16 rolls */
    DMG_FIXED,    /* lo == hi, no roll */
    DMG_OHKO,     /* lo = hi = target hp */
    DMG_FAILS,
    DMG_IMMUNE,
} DamageKind;

typedef struct {
    uint8_t kind;      /* DamageKind */
    uint8_t type;      /* effective move type (Hidden Power, Weather Ball), 0xFF unknown */
    uint32_t lo, hi;
    bool no_ko;        /* Substitute or semi-invulnerable target: never colour as KO */
    bool leaves_one;   /* False Swipe: at most target hp - 1 */
} DamageResult;

/* Effective type of a move for this attacker (Hidden Power, Weather Ball), else mv->type. */
uint8_t damage_move_type(const DamageCtx* ctx, const DamageMon* atk, uint16_t move_id, const DamageMove* mv);
/* Predicts a non-critical hit of move_id from atk on def. Never reads memory. */
void damage_predict(const DamageTables* t, const DamageCtx* ctx, const DamageMon* atk,
                    const DamageMon* def, uint16_t move_id, const DamageMove* mv, DamageResult* out);
#endif // DAMAGE_H
