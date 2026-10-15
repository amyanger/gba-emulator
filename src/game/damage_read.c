#include "game/damage_read.h"
#include "game/emerald_layout.h"
#include "game/game.h"
#include <string.h>

/* Offsets and constants: docs/superpowers/specs/emerald-game-layer/damage-data.md. */

/* struct Pokemon: personality is the first word of the 100-byte party struct. */
#define PARTY_MON_SIZE   100

/* struct DisableStruct (include/battle.h), 0x1C each */
#define DISABLE_SIZE       0x1C
#define DISABLE_STOCKPILE  0x09
#define DISABLE_FIRST_TURN 0x16

/* struct Item (include/item.h) */
#define ITEM_HOLD_EFFECT      0x12
#define ITEM_HOLD_EFFECT_PARAM 0x13

/* struct PokedexEntry (include/pokedex.h) */
#define DEX_ENTRY_SIZE   0x20
#define DEX_ENTRY_WEIGHT 0x0E

/* struct BattleResources: flags pointer at +4; struct ResourceFlags is u32 flags[4]. */
#define RESOURCES_FLAGS        0x04
#define RESOURCE_FLAG_FLASH_FIRE 1

#define BATTLE_TYPE_LINK            (1u << 1)
#define BATTLE_TYPE_EREADER_TRAINER (1u << 11)
#define BATTLE_TYPE_RECORDED_LINK   (1u << 25)
#define TRAINER_SECRET_BASE         1024

#define STATUS3_MUDSPORT   (1u << 16)
#define STATUS3_WATERSPORT (1u << 17)

/* include/constants/abilities.h */
#define ABILITY_DAMP       6
#define ABILITY_CLOUD_NINE 13
#define ABILITY_AIR_LOCK   77

#define MAX_BATTLERS 4

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool damage_tables_load(const GameMem* m, const GameProfile* p, DamageTables* out) {
    uint8_t raw[24];
    memset(out, 0, sizeof(*out));
    if (!p || p->type_effectiveness_len > DMG_TYPE_TABLE_MAX ||
        p->hold_effect_to_type_count > DMG_HOLD_TYPE_MAX)
        return false;
    if (!game_mem_copy(m, p->type_effectiveness, out->type_chart, p->type_effectiveness_len) ||
        !game_mem_copy(m, p->stat_stage_ratios, out->stat_ratio, sizeof(out->stat_ratio)) ||
        !game_mem_copy(m, p->hold_effect_to_type, out->hold_type, 2u * p->hold_effect_to_type_count) ||
        !game_mem_copy(m, p->flail_table, out->flail, sizeof(out->flail)) ||
        !game_mem_copy(m, p->weight_table, raw, sizeof(raw))) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out->weight[i][0] = rd16(raw + 4 * i);
        out->weight[i][1] = rd16(raw + 4 * i + 2);
    }
    out->type_len = p->type_effectiveness_len;
    out->hold_type_count = p->hold_effect_to_type_count;
    out->valid = true;
    return true;
}

bool damage_read_move(const GameMem* m, const GameProfile* p, uint16_t move_id, DamageMove* out) {
    uint8_t bm[BATTLE_MOVE_SIZE];
    memset(out, 0, sizeof(*out));
    if (move_id >= p->move_count ||
        !game_mem_copy(m, p->battle_moves + (uint32_t)move_id * BATTLE_MOVE_SIZE, bm, sizeof(bm)))
        return false;
    out->effect = bm[0];
    out->power = bm[1];
    out->type = bm[2];
    out->accuracy = bm[3];
    out->pp = bm[4];
    out->chance = bm[5];
    out->target = bm[6];
    out->priority = (int8_t)bm[7];
    out->flags = bm[8];
    return true;
}

/* R4: gBattleMons[b] must be the party mon at gBattlerPartyIndexes[b]; at battle start the
 * structs still hold the previous battle's mons for a few frames. */
static bool battler_current(const GameMem* m, const GameProfile* p, uint8_t b, uint8_t side,
                            const uint8_t* mon) {
    uint16_t idx;
    uint32_t personality;
    uint32_t party = side == 0 ? p->player_party : p->enemy_party;
    if (!game_mem_read16(m, p->battler_party_indexes + 2u * b, &idx) || idx > 5 ||
        !game_mem_read32(m, party + (uint32_t)idx * PARTY_MON_SIZE, &personality))
        return false;
    return personality == rd32(mon + BMON_PERSONALITY);
}

/* GetItemHoldEffect / GetItemHoldEffectParam: an id past the table is ITEM_NONE (effect 0). */
static bool hold_effect(const GameMem* m, const GameProfile* p, DamageMon* d) {
    if (d->item >= p->item_count) return true;
    uint32_t base = p->items + (uint32_t)d->item * ITEM_SIZE;
    return game_mem_read8(m, base + ITEM_HOLD_EFFECT, &d->hold_effect) &&
           game_mem_read8(m, base + ITEM_HOLD_EFFECT_PARAM, &d->hold_param);
}

/* GetPokedexHeightWeight(SpeciesToNationalPokedexNum(species), 1); 0 = unknown. */
static uint16_t dex_weight(const GameMem* m, const GameProfile* p, uint16_t species) {
    uint16_t national = game_species_national(m, p, species), weight = 0;
    if (national == 0 || national >= p->pokedex_count ||
        !game_mem_read16(m, p->pokedex_entries + (uint32_t)national * DEX_ENTRY_SIZE + DEX_ENTRY_WEIGHT,
                         &weight))
        return 0;
    return weight;
}

/* Everything per battler, indexed by battler id (R3). */
static bool read_mon(const GameMem* m, const GameProfile* p, uint8_t b, const uint8_t* mon,
                     uint8_t pos, uint8_t absent, uint32_t flash_fire_flags, DamageMon* d) {
    uint8_t disable[DISABLE_SIZE];
    memset(d, 0, sizeof(*d));
    d->battler = b;
    d->side = pos & 1;
    d->absent = (absent >> b) & 1;
    d->species = rd16(mon + BMON_SPECIES);
    d->attack = rd16(mon + BMON_ATTACK);
    d->defense = rd16(mon + BMON_DEFENSE);
    d->speed = rd16(mon + BMON_SPEED);
    d->sp_attack = rd16(mon + BMON_SP_ATTACK);
    d->sp_defense = rd16(mon + BMON_SP_DEFENSE);
    d->iv_word = rd32(mon + BMON_IVS);
    memcpy(d->stages, mon + BMON_STAGES, 8);
    d->ability = mon[BMON_ABILITY];
    d->type1 = mon[BMON_TYPE1];
    d->type2 = mon[BMON_TYPE2];
    d->hp = rd16(mon + BMON_HP);
    d->max_hp = rd16(mon + BMON_MAX_HP);
    d->level = mon[BMON_LEVEL];
    d->friendship = mon[BMON_FRIENDSHIP];
    d->item = rd16(mon + BMON_ITEM);
    d->status1 = rd32(mon + BMON_STATUS1);
    d->status2 = rd32(mon + BMON_STATUS2);
    d->flash_fire = (flash_fire_flags & RESOURCE_FLAG_FLASH_FIRE) != 0;
    if (!hold_effect(m, p, d) || !game_mem_read32(m, p->statuses3 + 4u * b, &d->status3) ||
        !game_mem_copy(m, p->disable_structs + (uint32_t)b * DISABLE_SIZE, disable, sizeof(disable)))
        return false;
    d->stockpile = disable[DISABLE_STOCKPILE];
    d->first_turn = disable[DISABLE_FIRST_TURN] != 0;
    d->weight_hg = dex_weight(m, p, d->species);
    return true;
}

/* gBattleResources->flags->flags[b]; false unless both pointers lie in EWRAM. */
static bool flash_fire_flags(const GameMem* m, const GameProfile* p, uint8_t b, uint32_t* out) {
    uint32_t res, flags;
    if (!game_mem_read32(m, p->battle_resources, &res) || !game_mem_in_ewram(res, RESOURCES_FLAGS + 4) ||
        !game_mem_read32(m, res + RESOURCES_FLAGS, &flags) || !game_mem_in_ewram(flags, 4u * MAX_BATTLERS))
        return false;
    return game_mem_read32(m, flags + 4u * b, out);
}

/* Bit n = FLAG_BADGE0(n+1)_GET, read from SaveBlock1 like FlagGet (the block moves, so the
 * pointer is re-read every call). */
static bool read_badges(const GameMem* m, const GameProfile* p, uint8_t* out) {
    uint32_t sb1;
    uint8_t lo, hi;
    uint32_t at = SB1_FLAGS + FLAG_BADGE01_GET / 8;
    if (!game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_in_ewram(sb1 + at, 2) ||
        !game_mem_read8(m, sb1 + at, &lo) || !game_mem_read8(m, sb1 + at + 1, &hi))
        return false;
    uint16_t bits = (uint16_t)(lo | (hi << 8)) >> (FLAG_BADGE01_GET % 8);
    *out = (uint8_t)bits;
    return true;
}

bool damage_read_battle(const GameMem* m, const GameProfile* p, uint8_t player_battler,
                        uint8_t enemy_battler, DamageCtx* ctx, DamageMon* player, DamageMon* enemy) {
    uint8_t count, absent, pos[MAX_BATTLERS], mons[MAX_BATTLERS][BMON_SIZE], badges;
    uint32_t status3[MAX_BATTLERS], ff_player, ff_enemy;
    uint16_t opponent_a;
    memset(ctx, 0, sizeof(*ctx));
    memset(player, 0, sizeof(*player));
    memset(enemy, 0, sizeof(*enemy));

    if (!game_mem_read32(m, p->battle_type_flags, &ctx->battle_flags) ||
        (ctx->battle_flags & BATTLE_TYPE_SAFARI))
        return false;
    if (!game_mem_read8(m, p->battlers_count, &count) || (count != 2 && count != 4) ||
        player_battler >= count || enemy_battler >= count || player_battler == enemy_battler)
        return false;
    if (!game_mem_read8(m, p->absent_battler_flags, &absent) ||
        !game_mem_read16(m, p->battle_weather, &ctx->weather) ||
        !game_mem_read16(m, p->side_statuses, &ctx->side_status[0]) ||
        !game_mem_read16(m, p->side_statuses + 2, &ctx->side_status[1]) ||
        !game_mem_read16(m, p->trainer_battle_opponent_a, &opponent_a) || !read_badges(m, p, &badges) ||
        !flash_fire_flags(m, p, player_battler, &ff_player) || !flash_fire_flags(m, p, enemy_battler, &ff_enemy))
        return false;
    for (uint8_t b = 0; b < count; b++) {
        if (!game_mem_read8(m, p->battler_positions + b, &pos[b]) ||
            !game_mem_copy(m, p->battle_mons + (uint32_t)b * BMON_SIZE, mons[b], BMON_SIZE) ||
            !game_mem_read32(m, p->statuses3 + 4u * b, &status3[b]))
            return false;
    }

    /* Field scans over gBattlersCount, as the game's loops do. */
    bool cloud_nine_any = false, cloud_nine_alive = false;
    for (uint8_t b = 0; b < count; b++) {
        uint8_t ability = mons[b][BMON_ABILITY];
        bool alive = rd16(mons[b] + BMON_HP) != 0;
        if (!((absent >> b) & 1)) ctx->present_on_side[pos[b] & 1]++; /* CountAliveMonsInBattle */
        if (status3[b] & STATUS3_MUDSPORT) ctx->mud_sport = true;     /* ABILITYEFFECT_MUD_SPORT */
        if (status3[b] & STATUS3_WATERSPORT) ctx->water_sport = true;
        if (ability == ABILITY_DAMP) ctx->damp_on_field = true;       /* Cmd_tryexplosion, no hp check */
        if (ability == ABILITY_PLUS) ctx->plus_on_field = true;       /* ABILITY_ON_FIELD2, no hp check */
        if (ability == ABILITY_MINUS) ctx->minus_on_field = true;
        if (ability == ABILITY_CLOUD_NINE || ability == ABILITY_AIR_LOCK) {
            cloud_nine_any = true;                                     /* ABILITY_ON_FIELD2 */
            if (alive) cloud_nine_alive = true;                        /* ABILITY_ON_FIELD: hp != 0 */
        }
    }
    ctx->weather_negated = cloud_nine_any;
    ctx->weather_has_effect = !cloud_nine_alive;

    const uint8_t* pm = mons[player_battler];
    const uint8_t* em = mons[enemy_battler];
    if (!battler_current(m, p, player_battler, pos[player_battler] & 1, pm) ||
        !battler_current(m, p, enemy_battler, pos[enemy_battler] & 1, em))
        return false;
    if (!read_mon(m, p, player_battler, pm, pos[player_battler], absent, ff_player, player) ||
        !read_mon(m, p, enemy_battler, em, pos[enemy_battler], absent, ff_enemy, enemy))
        return false;

    /* ShouldGetStatBadgeBoost: player side only, not link/e-Reader/Frontier, not a secret base. */
    bool badges_ok = !(ctx->battle_flags & (BATTLE_TYPE_LINK | BATTLE_TYPE_EREADER_TRAINER |
                                            BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_FRONTIER)) &&
                     !((ctx->battle_flags & BATTLE_TYPE_TRAINER) && opponent_a == TRAINER_SECRET_BASE);
    DamageMon* both[2] = {player, enemy};
    for (int i = 0; i < 2; i++) {
        both[i]->badges = badges;
        both[i]->badges_apply = badges_ok && both[i]->side == 0;
    }
    return true;
}
