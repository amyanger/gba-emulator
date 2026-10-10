#ifndef BATTLE_INFO_H
#define BATTLE_INFO_H

#include <stdbool.h>
#include <stdint.h>

/* Item ids of the balls (pokeemerald include/constants/items.h). */
#define BALL_MASTER  1
#define BALL_ULTRA   2
#define BALL_GREAT   3
#define BALL_POKE    4
#define BALL_SAFARI  5
#define BALL_NET     6
#define BALL_DIVE    7
#define BALL_NEST    8
#define BALL_REPEAT  9
#define BALL_TIMER   10
#define BALL_LUXURY  11
#define BALL_PREMIER 12

#define TYPE_GROUND 4
#define TYPE_BUG    6
#define TYPE_WATER  11

#define ABILITY_WONDER_GUARD 25
#define ABILITY_LEVITATE     26

#define STATUS1_SLEEP     0x07
#define STATUS1_POISON    0x08
#define STATUS1_BURN      0x10
#define STATUS1_FREEZE    0x20
#define STATUS1_PARALYSIS 0x40
#define STATUS1_TOXIC     0x80

/* Effectiveness of a move type against a defender, in quarters
 * (0 = immune, 4 = neutral, 16 = x4), walking the game's triplet table the
 * way battle_main.c TypeCalc does. */
uint8_t battle_type_quarters(const uint8_t* table, uint32_t len, uint8_t move_type,
                             uint8_t def_type1, uint8_t def_type2, bool foresight);
uint8_t battle_apply_ability(uint8_t quarters, uint8_t move_type, uint8_t def_ability);

typedef struct {
    uint8_t ball;          /* item id */
    uint8_t catch_rate;    /* species catch rate */
    uint16_t hp, max_hp;
    uint32_t status1;
    uint8_t level;
    uint8_t type1, type2;
    bool owned;            /* Pokedex caught flag, for the Repeat Ball */
    uint8_t turn_counter;  /* gBattleResults.battleTurnCounter, for the Timer Ball */
    bool underwater;       /* map type is underwater, for the Dive Ball */
} CatchInput;

uint8_t battle_ball_multiplier(const CatchInput* in);
/* Exact capture probability in per mille, following Cmd_handleballthrow. */
uint16_t battle_catch_permille(const CatchInput* in);
uint32_t battle_isqrt(uint32_t n);

#endif // BATTLE_INFO_H
