#ifndef SCRIPT_WALK_H
#define SCRIPT_WALK_H

#include <stdint.h>
#include "game/game_mem.h"

#define SW_MAX_GIFTS 2

typedef struct {
    uint16_t item[2];      /* constant item ids; two = a choice under one flag */
    uint8_t item_count;
    uint16_t flag;         /* set once received; 0x20..0x4FF */
} ScriptGift;

typedef struct {
    uint8_t count;         /* 0 = no tag */
    ScriptGift gift[SW_MAX_GIFTS];   /* walk order */
} ScriptGifts;

/* Statically walks an NPC's event script (Emerald bytecode) and returns the items
 * it hands out with the flag that marks each received, following
 * docs/superpowers/specs/emerald-game-layer/overworld-gifts.md. Reads ROM only. */
void script_walk_gifts(const GameMem* m, uint32_t script, uint8_t local_id, uint16_t hide_flag,
                       ScriptGifts* out);

#endif // SCRIPT_WALK_H
