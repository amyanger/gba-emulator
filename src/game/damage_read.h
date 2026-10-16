#ifndef DAMAGE_READ_H
#define DAMAGE_READ_H

#include <stdbool.h>
#include <stdint.h>
#include "game/damage.h"
#include "game/game_profile.h"
#include "game/game_mem.h"

/* Builds damage.h inputs from emulated memory. Pure observer: game_mem_* reads only. */

/* Copies the ROM tables; false (and out->valid false) when any is unreadable or too big. */
bool damage_tables_load(const GameMem* m, const GameProfile* p, DamageTables* out);
/* Fills ctx and the two mons; false when anything needed is unreadable or stale (R4). */
bool damage_read_battle(const GameMem* m, const GameProfile* p, uint8_t player_battler,
                        uint8_t enemy_battler, DamageCtx* ctx, DamageMon* player, DamageMon* enemy);
/* gBattleMoves[move_id]; false when out of range or unreadable. */
bool damage_read_move(const GameMem* m, const GameProfile* p, uint16_t move_id, DamageMove* out);

#endif // DAMAGE_READ_H
