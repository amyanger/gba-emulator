#include "game/runlog_read.h"
#include "game/emerald_layout.h"
#include <string.h>

/* SaveBlock1 / SaveBlock2 (pret include/global.h); per-game offsets come from GameRules. */
#define SB1_LOCATION   0x04     /* struct WarpData: s8 mapGroup, s8 mapNum */
#define FLAG_BYTES_MAX 300      /* largest NUM_FLAG_BYTES (Emerald) */
#define SB2_NAME       0x00
#define SB2_READ_MAX   0xF24    /* largest encryptionKey + 4 (FRLG key at 0xF20) */
#define DEX_COUNT      386      /* NATIONAL_DEX_COUNT, bit n-1 = national dex n */

/* struct Trainer (include/data.h); ROM party entries are 16 bytes with custom
 * moves (F_TRAINER_PARTY_CUSTOM_MOVESET), else 8, species at +4. */
#define TRAINER_PARTY_SIZE 0x20
#define TRAINER_PARTY_PTR  0x24

/* gBattleTypeFlags bits (include/constants/battle.h) */
#define BT_TRAINER (1u << 3)

static bool flag_set(const uint8_t* flags, uint16_t bytes, uint16_t id) {
    return (id >> 3) < bytes && (flags[id >> 3] >> (id & 7)) & 1;
}

static uint16_t var_get(const GameMem* m, const GameRules* r, uint32_t sb1, uint16_t id) {
    uint16_t v = 0;
    game_mem_read16(m, sb1 + r->sb1_vars + 2u * (uint32_t)(id - 0x4000u), &v);
    return v;
}

static uint16_t dex_count(const uint8_t* bits) {
    uint16_t n = 0;
    for (uint32_t i = 0; i < DEX_COUNT; i++) n = (uint16_t)(n + ((bits[i >> 3] >> (i & 7)) & 1));
    return n;
}

bool run_is_leader(const GameRules* r, uint16_t trainer) {
    if (trainer == 0) return false;
    for (uint8_t i = 0; i < 8; i++)
        if (r->leaders[i] == trainer) return true;
    for (uint8_t i = 0; i < r->league_count && i < 16; i++)
        if (r->league[i] == trainer) return true;
    return false;
}

static bool facility_map(const GameRules* r, uint8_t group, uint8_t num) {
    for (uint8_t i = 0; i < r->facility_map_count; i++) {
        const MapRange* m = &r->facility_maps[i];
        if (group == m->group && num >= m->first && num <= m->last) return true;
    }
    return false;
}

/* Species of the trainer's last party slot, 0 when the ROM data looks wrong. */
static uint16_t trainer_ace(const GameProfile* p, const GameMem* m, uint16_t trainer) {
    uint32_t t = p->trainers + (uint32_t)trainer * TRAINER_SIZE;
    uint8_t flags, size;
    uint32_t party;
    uint16_t species;
    if (!game_mem_read8(m, t, &flags) || !game_mem_read8(m, t + TRAINER_PARTY_SIZE, &size) ||
        !game_mem_read32(m, t + TRAINER_PARTY_PTR, &party) || size < 1 || size > 6 ||
        (party >> 24) != 0x08)
        return 0;
    uint32_t stride = (flags & 1) ? 16 : 8;
    if (!game_mem_read16(m, party + (uint32_t)(size - 1) * stride + 4, &species) ||
        species >= p->species_count)
        return 0;
    return species;
}

static void read_battle(const GameProfile* p, const GameMem* m, RunFacts* f) {
    uint8_t in_battle, count;
    uint32_t flags = 0;
    if (!game_mem_read8(m, p->main_in_battle, &in_battle) || !(in_battle & 0x02) ||
        !game_mem_read8(m, p->battlers_count, &count) || (count != 2 && count != 4))
        return;
    game_mem_read32(m, p->battle_type_flags, &flags);
    if (flags & p->rules->run_facility_mask) f->facility = true;
    f->partner_battle = (flags & p->rules->partner_battle_mask) != 0;
    f->battlers = count;
    for (uint32_t b = 0; b < count; b++) {
        RunBattler* rb = &f->b[b];
        uint32_t bm = p->battle_mons + b * BMON_SIZE;
        game_mem_read16(m, bm + BMON_SPECIES, &rb->species);
        game_mem_read16(m, bm + BMON_HP, &rb->hp);
        game_mem_read8(m, bm + BMON_LEVEL, &rb->level);
        game_mem_read16(m, bm + BMON_MAX_HP, &rb->max_hp);
        game_mem_read8(m, p->battler_positions + b, &rb->pos);
        game_mem_read16(m, p->battler_party_indexes + 2 * b, &rb->party_index);
    }
    game_mem_read8(m, p->battler_attacker, &f->attacker);

    uint16_t opp = 0;
    if ((flags & BT_TRAINER) && !(flags & p->rules->run_not_plain_mask) &&
        game_mem_read16(m, p->trainer_battle_opponent_a, &opp) && opp > 0 && opp < p->trainer_count) {
        f->opponent = opp;
        f->ace_species = trainer_ace(p, m, opp);
        f->opponent_leader = run_is_leader(p->rules, opp);
    }
}

bool run_read(const GameProfile* p, const GameMem* m, uint16_t enemy_species, RunFacts* f) {
    static uint8_t sb2[SB2_READ_MAX];   /* static: 0xF24 bytes on FRLG */
    uint8_t flags[FLAG_BYTES_MAX];
    uint32_t sb1_addr = 0, sb2_addr = 0;
    memset(f, 0, sizeof(*f));
    if (!p || game_quest_log_playing(p, m)) return false;
    const GameRules* r = p->rules;
    uint16_t flag_bytes = r->sb1_flag_bytes < FLAG_BYTES_MAX ? r->sb1_flag_bytes : FLAG_BYTES_MAX;
    uint32_t sb1_len = r->sb1_game_stats + 4u * RUN_STATS;
    uint32_t sb2_len = r->sb2_encryption_key + 4u;
    if (sb2_len < SB2_DEX_SEEN + 52u) sb2_len = SB2_DEX_SEEN + 52u;
    if (sb2_len > SB2_READ_MAX ||
        !game_mem_read32(m, p->save_block1_ptr, &sb1_addr) || !game_mem_read32(m, p->save_block2_ptr, &sb2_addr) ||
        !game_mem_in_ewram(sb1_addr, sb1_len) || !game_mem_in_ewram(sb2_addr, sb2_len) ||
        !game_mem_copy(m, sb1_addr + r->sb1_flags, flags, flag_bytes) || !game_mem_copy(m, sb2_addr, sb2, sb2_len))
        return false;
    f->valid = true;
    f->sb1 = sb1_addr;
    f->sb2 = sb2_addr;
    memcpy(f->player_name, sb2 + SB2_NAME, sizeof(f->player_name));

    /* Stats and key come from this same frame: the key changes on new game. */
    uint32_t k = r->sb2_encryption_key;
    uint32_t key = (uint32_t)sb2[k] | ((uint32_t)sb2[k + 1] << 8) | ((uint32_t)sb2[k + 2] << 16) |
                   ((uint32_t)sb2[k + 3] << 24);
    for (uint32_t i = 0; i < RUN_STATS; i++) {
        uint32_t v = 0;
        game_mem_read32(m, sb1_addr + r->sb1_game_stats + 4 * i, &v);
        f->stats[i] = v ^ key;
    }

    for (uint16_t b = 0; b < RUN_BADGES; b++)
        if (flag_set(flags, flag_bytes, (uint16_t)(r->flag_badge01 + b))) f->badges |= (uint8_t)(1u << b);
    for (uint8_t i = 0; i < r->story_count && i < RUN_STORY_COUNT; i++) {
        const RunStoryDef* d = &r->story[i];
        bool done = d->kind == STORY_FLAG ? flag_set(flags, flag_bytes, d->id)
                                          : var_get(m, r, sb1_addr, d->id) >= d->at_least;
        if (done) f->story |= (uint16_t)(1u << i);
    }
    f->starter = (uint8_t)var_get(m, r, sb1_addr, r->var_starter);
    f->partner_otid = r->partner_otid;
    f->hof_story = r->story_hof;

    f->dex_owned = dex_count(sb2 + SB2_DEX_OWNED);
    f->dex_seen = dex_count(sb2 + SB2_DEX_SEEN);
    uint16_t natdex = 0;
    if (enemy_species > 0 && enemy_species < p->species_count &&
        game_mem_read16(m, p->species_to_national + 2u * (enemy_species - 1u), &natdex) && natdex >= 1 &&
        natdex <= DEX_COUNT)
        f->enemy_owned = (sb2[SB2_DEX_OWNED + ((natdex - 1) >> 3)] >> ((natdex - 1) & 7)) & 1;

    game_mem_read32(m, p->main_callback2, &f->callback2);
    f->whiteout = f->callback2 != 0 && f->callback2 == p->cb2_whiteout;
    game_mem_read8(m, sb1_addr + SB1_LOCATION, &f->map_group);
    game_mem_read8(m, sb1_addr + SB1_LOCATION + 1, &f->map_num);
    f->facility = facility_map(r, f->map_group, f->map_num);
    read_battle(p, m, f);
    return true;
}
