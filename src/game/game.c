#include "game/game.h"
#include "game/battle_info.h"
#include "game/damage_read.h"
#include "game/daycare.h"
#include "game/gen3_text.h"
#include "gba.h"
#include "cartridge/rtc.h"
#include <string.h>
#include <time.h>

/* SaveBlock1 / SaveBlock2 (pokeemerald include/global.h) */
#define SB1_MAP_GROUP    0x04
#define SB1_MAP_NUM      0x05
#define SB1_BALL_POCKET  0x650
#define SB1_FLAGS        0x1270
#define SB1_DEX_SEEN1    0x988
#define SB1_DEX_SEEN2    0x3B24
#define SB1_MIN_SIZE     (SB1_DEX_SEEN2 + 52)
#define SB2_TRAINER_ID   0x0A
#define SB2_DEX_OWNED    0x28
#define SB2_DEX_SEEN     0x5C
#define SB2_ENCRYPT_KEY  0xAC
#define SB2_MIN_SIZE     (SB2_ENCRYPT_KEY + 4)
#define BALL_POCKET_SLOTS 16

/* struct BattlePokemon (include/pokemon.h) */
#define BMON_SIZE        0x58
#define BMON_SPECIES     0x00
#define BMON_MOVES       0x0C
#define BMON_IVS         0x14
#define BMON_ABILITY     0x20
#define BMON_TYPE1       0x21
#define BMON_TYPE2       0x22
#define BMON_HP          0x28
#define BMON_LEVEL       0x2A
#define BMON_MAX_HP      0x2C
#define BMON_ITEM        0x2E
#define BMON_PERSONALITY 0x48
#define BMON_STATUS1     0x4C
#define BMON_STATUS2     0x50
#define STATUS2_FORESIGHT (1u << 29)

/* struct SpeciesInfo */
#define SPECIES_INFO_SIZE 28
#define SI_TYPE1          0x06
#define SI_TYPE2          0x07
#define SI_CATCH_RATE     0x08
#define SI_GENDER_RATIO   0x10
#define SI_GROWTH_RATE    0x13
#define SI_EGG_GROUP1     0x14
#define SI_EGG_GROUP2     0x15
#define SI_ABILITY1       0x16
#define SI_ABILITY2       0x17

#define BATTLE_MOVE_SIZE  12
#define ITEM_SIZE         44
#define ITEM_NAME_LEN     14
#define SPECIES_NAME_STRIDE 11
#define MOVE_NAME_STRIDE  13
#define ABILITY_NAME_STRIDE 13
#define TYPE_NAME_STRIDE  7

#define BATTLE_TYPE_TRAINER (1u << 3)
#define BATTLE_TYPE_SAFARI  (1u << 7)
#define MAP_HEADER_REGION_MAP_SEC 0x14
#define MAP_HEADER_MAP_TYPE 0x17
#define MAP_TYPE_UNDERWATER 5
#define TYPE_TABLE_MAX 512
/* struct RegionMapLocation (include/region_map.h): x, y, width, height, name */
#define REGION_MAP_ENTRY_SIZE 8
#define REGION_MAP_ENTRY_NAME 4
/* Move id from the ROM's move-name table; its matchup type comes from the user's IVs. */
#define MOVE_HIDDEN_POWER 237

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* Fixed-stride ROM string table entry -> ASCII, "???" when out of range. */
static void table_name(const GameMem* m, uint32_t base, uint32_t count, uint32_t stride,
                       uint32_t len, uint32_t id, char* out) {
    uint8_t raw[16];
    if (id >= count || len > sizeof(raw) || !game_mem_copy(m, base + id * stride, raw, len)) {
        game_strcpy(out, GAME_NAME_LEN, "???");
        return;
    }
    gen3_decode(raw, len, out, GAME_NAME_LEN);
}

void game_species_name(const GameMem* m, const GameProfile* p, uint16_t s, char* out) {
    table_name(m, p->species_names, p->species_count, SPECIES_NAME_STRIDE,
               SPECIES_NAME_STRIDE, s, out);
}
static void move_name(const GameMem* m, const GameProfile* p, uint16_t mv, char* out) {
    table_name(m, p->move_names, p->move_count, MOVE_NAME_STRIDE, MOVE_NAME_STRIDE, mv, out);
}
static void ability_name(const GameMem* m, const GameProfile* p, uint8_t a, char* out) {
    table_name(m, p->ability_names, p->ability_count, ABILITY_NAME_STRIDE,
               ABILITY_NAME_STRIDE, a, out);
}
static void type_name(const GameMem* m, const GameProfile* p, uint8_t t, char* out) {
    table_name(m, p->type_names, p->type_count, TYPE_NAME_STRIDE, TYPE_NAME_STRIDE, t, out);
}
void game_item_name(const GameMem* m, const GameProfile* p, uint16_t item, char* out) {
    if (item == 0) {
        game_strcpy(out, GAME_NAME_LEN, "--");
        return;
    }
    table_name(m, p->items, p->item_count, ITEM_SIZE, ITEM_NAME_LEN, item, out);
}

void game_nature_name(const GameMem* m, const GameProfile* p, uint8_t nature, char* out) {
    uint32_t ptr;
    uint8_t raw[12];
    uint32_t n = 0;
    game_strcpy(out, GAME_NAME_LEN, "???");
    if (nature >= 25 || !game_mem_read32(m, p->nature_name_ptrs + 4u * nature, &ptr)) return;
    while (n < sizeof(raw) && game_mem_read8(m, ptr + n, &raw[n]) && raw[n] != 0xFF) n++;
    if (n > 0) gen3_decode(raw, n, out, GAME_NAME_LEN);
}

/* gRegionMapEntries[sec].name, as GetMapName reads it; empty when unknown. */
void game_mapsec_name(const GameMem* m, const GameProfile* p, uint8_t sec, char* out) {
    uint32_t ptr;
    uint8_t raw[GAME_MAP_NAME_LEN];
    uint32_t n = 0;
    out[0] = '\0';
    if (sec >= p->region_map_count ||
        !game_mem_read32(m, p->region_map_entries + sec * REGION_MAP_ENTRY_SIZE + REGION_MAP_ENTRY_NAME,
                         &ptr))
        return;
    while (n < sizeof(raw) && game_mem_read8(m, ptr + n, &raw[n]) && raw[n] != 0xFF) n++;
    if (n > 0) gen3_decode(raw, n, out, GAME_MAP_NAME_LEN);
}

/* Name of the current map's region map section (gMapHeader.regionMapSectionId). */
static void map_name(const GameMem* m, const GameProfile* p, char* out) {
    uint8_t sec;
    out[0] = '\0';
    if (game_mem_read8(m, p->map_header + MAP_HEADER_REGION_MAP_SEC, &sec)) game_mapsec_name(m, p, sec, out);
}

static bool species_info(const GameMem* m, const GameProfile* p, uint16_t species,
                         uint8_t info[SPECIES_INFO_SIZE]) {
    return species != 0 && species < p->species_count &&
           game_mem_copy(m, p->species_info + (uint32_t)species * SPECIES_INFO_SIZE, info,
                         SPECIES_INFO_SIZE);
}

static bool dex_bit(const GameMem* m, uint32_t base, uint16_t national) {
    uint8_t byte;
    if (!game_mem_read8(m, base + (national - 1u) / 8, &byte)) return false;
    return (byte >> ((national - 1u) % 8)) & 1;
}

/* National dex number of a species, 0 when out of range or unreadable. */
uint16_t game_species_national(const GameMem* m, const GameProfile* p, uint16_t species) {
    uint16_t national;
    if (species == 0 || species >= p->species_count) return 0;
    if (!game_mem_read16(m, p->species_to_national + (species - 1u) * 2, &national) ||
        national > 52 * 8)
        return 0;
    return national;
}

/* Mirrors GetSetPokedexFlag(FLAG_GET_CAUGHT) without its write-back. */
static bool dex_caught(const GameMem* m, const GameProfile* p, uint32_t sb1, uint32_t sb2,
                       uint16_t species) {
    uint16_t national = game_species_national(m, p, species);
    if (national == 0) return false;
    return dex_bit(m, sb2 + SB2_DEX_OWNED, national) && dex_bit(m, sb2 + SB2_DEX_SEEN, national) &&
           dex_bit(m, sb1 + SB1_DEX_SEEN1, national) && dex_bit(m, sb1 + SB1_DEX_SEEN2, national);
}

static uint32_t exp_to_next(const GameMem* m, const GameProfile* p, uint8_t growth,
                            uint8_t level, uint32_t exp) {
    uint32_t next;
    if (level == 0 || level >= 100 || growth > 5) return 0;
    if (!game_mem_read32(m, p->experience_tables + 4u * (growth * 101u + level + 1u), &next))
        return 0;
    return next > exp ? next - exp : 0;
}

void game_fill_mon(const GameMem* m, const GameProfile* p, const MonData* d, GamePartyMon* out) {
    uint8_t info[SPECIES_INFO_SIZE];
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->type_ids[0] = out->type_ids[1] = 0xFF;
    if (d->bad) {
        out->bad = true;
        return;
    }
    out->egg = d->is_egg;
    out->hp = d->hp;
    out->max_hp = d->max_hp;
    gen3_decode(d->nickname_raw, sizeof(d->nickname_raw), out->nickname, GAME_NAME_LEN);
    game_species_name(m, p, d->species, out->species);
    out->level = d->level;
    memcpy(out->ivs, d->ivs, 6);
    memcpy(out->evs, d->evs, 6);
    uint8_t nature = pokemon_nature(d->personality);
    game_nature_name(m, p, nature, out->nature);
    pokemon_nature_effect(nature, &out->nature_up, &out->nature_down);
    type_name(m, p, pokemon_hidden_power_type(d->ivs), out->hp_type);
    out->hp_power = pokemon_hidden_power_power(d->ivs);
    out->friendship = d->friendship;
    game_item_name(m, p, d->held_item, out->item);
    out->gender = MON_GENDER_NONE;
    if (species_info(m, p, d->species, info)) {
        out->gender = pokemon_gender(info[SI_GENDER_RATIO], d->personality);
        out->type_ids[0] = info[SI_TYPE1];
        out->type_ids[1] = info[SI_TYPE2];
        ability_name(m, p, d->ability_num ? info[SI_ABILITY2] : info[SI_ABILITY1], out->ability);
        out->exp_to_next = exp_to_next(m, p, info[SI_GROWTH_RATE], d->level, d->experience);
    } else {
        game_strcpy(out->ability, GAME_NAME_LEN, "???");
    }
}

static void read_party(const GameMem* m, const GameProfile* p, GameSnapshot* s) {
    uint8_t count;
    if (!game_mem_read8(m, p->player_party_count, &count)) return;
    if (count > 6) count = 6;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t raw[MON_PARTY_SIZE];
        MonData d;
        if (!game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, sizeof(raw)))
            break;
        if (!pokemon_decode(raw, true, &d)) continue;
        game_fill_mon(m, p, &d, &s->party[s->party_count++]);
    }
}

/* Battlers: returns enemy battle structs (up to 2) with their battler ids, and the player's
 * active one. */
static uint8_t read_battlers(const GameMem* m, const GameProfile* p,
                             uint8_t enemies[2][BMON_SIZE], uint8_t enemy_ids[2],
                             uint8_t player[BMON_SIZE], bool* have_player, uint8_t* player_battler) {
    uint8_t in_battle, count, n = 0;
    *have_player = false;
    if (!game_mem_read8(m, p->main_in_battle, &in_battle) || !(in_battle & 0x02)) return 0;
    if (!game_mem_read8(m, p->battlers_count, &count) || (count != 2 && count != 4)) return 0;
    for (uint8_t b = 0; b < count; b++) {
        uint8_t pos, mon[BMON_SIZE];
        if (!game_mem_read8(m, p->battler_positions + b, &pos)) return 0;
        if (!game_mem_copy(m, p->battle_mons + (uint32_t)b * BMON_SIZE, mon, BMON_SIZE)) return 0;
        uint16_t species = rd16(mon + BMON_SPECIES);
        if (pos & 1) {
            if (species != 0 && species < p->species_count && n < 2) {
                enemy_ids[n] = b;
                memcpy(enemies[n++], mon, BMON_SIZE);
            }
        } else if (!*have_player) {
            memcpy(player, mon, BMON_SIZE);
            *have_player = true;
            *player_battler = b;
        }
    }
    return n;
}

/* Nickname of gPlayerParty[gBattlerPartyIndexes[battler]]; left empty when out of range. */
static void read_active_name(const GameMem* m, const GameProfile* p, uint8_t battler,
                             GameSnapshot* s) {
    uint16_t idx;
    uint8_t raw[MON_PARTY_SIZE], count;
    MonData d;
    if (battler >= 4 || !game_mem_read8(m, p->player_party_count, &count) ||
        !game_mem_read16(m, p->battler_party_indexes + 2u * battler, &idx) || idx > 5 ||
        idx >= count)
        return;
    if (!game_mem_copy(m, p->player_party + (uint32_t)idx * MON_PARTY_SIZE, raw, sizeof(raw)) ||
        !pokemon_decode(raw, true, &d) || d.bad)
        return;
    gen3_decode(d.nickname_raw, sizeof(d.nickname_raw), s->active_name, GAME_NAME_LEN);
}

/* Damage calculator inputs for one direction; NULL in fill_move_eff when not computed. */
typedef struct {
    const DamageTables* t;
    const DamageCtx* ctx;
    const DamageMon* atk;
    const DamageMon* def;
} DamagePair;

/* One of attacker's moves against defender (NULL when unknown: quarters 0xFF).
 * Foresight is read from the defender, as TypeCalc checks the target's status2.
 * With dp, the effective type (R7) and the predicted damage are filled too. */
static void fill_move_eff(const GameMem* m, const GameProfile* p, uint16_t mv,
                          const uint8_t* attacker, const uint8_t* defender,
                          const uint8_t* type_table, uint32_t type_len, const DamagePair* dp,
                          GameMoveEff* eff) {
    uint8_t bm[BATTLE_MOVE_SIZE];
    DamageMove dmove;
    eff->type = 0xFF;
    eff->dmg_kind = DMG_NONE;
    eff->quarters = defender ? 0 : 0xFF;
    move_name(m, p, mv, eff->move);
    if (mv >= p->move_count ||
        !game_mem_copy(m, p->battle_moves + (uint32_t)mv * BATTLE_MOVE_SIZE, bm, sizeof(bm))) {
        eff->status_move = true;
        return;
    }
    uint8_t move_type = bm[2];
    if (bm[1] == 0) {
        eff->status_move = true;
        eff->type = move_type;
        return;
    }
    if (dp && !damage_read_move(m, p, mv, &dmove)) dp = NULL;
    if (dp) {
        move_type = damage_move_type(dp->ctx, dp->atk, mv, &dmove);
    } else if (mv == MOVE_HIDDEN_POWER) {
        uint8_t ivs[6];
        pokemon_unpack_ivs(rd32(attacker + BMON_IVS), ivs);
        move_type = pokemon_hidden_power_type(ivs);
    }
    eff->type = move_type;
    if (!defender) return;
    bool foresight = (rd32(defender + BMON_STATUS2) & STATUS2_FORESIGHT) != 0;
    uint8_t q = battle_type_quarters(type_table, type_len, move_type, defender[BMON_TYPE1],
                                     defender[BMON_TYPE2], foresight);
    eff->quarters = battle_apply_ability(q, move_type, defender[BMON_ABILITY]);
    if (!dp) return;
    DamageResult r;
    damage_predict(dp->t, dp->ctx, dp->atk, dp->def, mv, &dmove, &r);
    eff->dmg_kind = r.kind;
    eff->dmg_lo = r.lo;
    eff->dmg_hi = r.hi;
    eff->dmg_no_ko = r.no_ko;
    eff->dmg_leaves_one = r.leaves_one;
    if (r.kind == DMG_IMMUNE) eff->quarters = 0;
}

/* to_player / from_player: damage inputs for this enemy's moves and the player's, or NULL. */
static void fill_enemy(const GameMem* m, const GameProfile* p, const uint8_t* mon,
                       const uint8_t* player, bool have_player, const uint8_t* type_table,
                       uint32_t type_len, const DamagePair* to_player, const DamagePair* from_player,
                       GameEnemy* e) {
    uint8_t info[SPECIES_INFO_SIZE];
    uint16_t species = rd16(mon + BMON_SPECIES);
    uint32_t personality = rd32(mon + BMON_PERSONALITY);
    uint8_t ability = mon[BMON_ABILITY];
    uint8_t t1 = mon[BMON_TYPE1], t2 = mon[BMON_TYPE2];

    memset(e, 0, sizeof(*e));
    e->type_ids[0] = t1;
    e->type_ids[1] = t2;
    game_species_name(m, p, species, e->species);
    e->level = mon[BMON_LEVEL];
    e->gender = species_info(m, p, species, info)
                    ? pokemon_gender(info[SI_GENDER_RATIO], personality) : MON_GENDER_NONE;
    uint8_t nature = pokemon_nature(personality);
    game_nature_name(m, p, nature, e->nature);
    pokemon_nature_effect(nature, &e->nature_up, &e->nature_down);
    pokemon_unpack_ivs(rd32(mon + BMON_IVS), e->ivs);
    e->hp = rd16(mon + BMON_HP);
    e->max_hp = rd16(mon + BMON_MAX_HP);
    ability_name(m, p, ability, e->ability);
    game_item_name(m, p, rd16(mon + BMON_ITEM), e->item);
    e->status1 = rd32(mon + BMON_STATUS1);
    type_name(m, p, t1, e->type1);
    if (t2 != t1) type_name(m, p, t2, e->type2);

    for (int i = 0; i < 4; i++) {
        uint16_t mv = rd16(mon + BMON_MOVES + 2 * i);
        if (mv != 0)
            fill_move_eff(m, p, mv, mon, have_player ? player : NULL, type_table, type_len,
                          have_player ? to_player : NULL, &e->moves[e->move_count++]);
    }
    if (!have_player) return;
    e->player_hp = rd16(player + BMON_HP);
    e->player_max_hp = rd16(player + BMON_MAX_HP);
    for (int i = 0; i < 4; i++) {
        uint16_t mv = rd16(player + BMON_MOVES + 2 * i);
        if (mv != 0)
            fill_move_eff(m, p, mv, player, mon, type_table, type_len, from_player,
                          &e->eff[e->eff_count++]);
    }
}

static void read_catch_rows(const GameMem* m, const GameProfile* p, uint32_t sb1, uint32_t sb2,
                            const uint8_t* target, uint32_t battle_flags, GameSnapshot* s) {
    uint8_t info[SPECIES_INFO_SIZE];
    uint32_t key = 0;
    uint16_t species = rd16(target + BMON_SPECIES);

    if (battle_flags & BATTLE_TYPE_SAFARI) {
        GameCatchRow* r = &s->catch_rows[s->catch_count++];
        game_strcpy(r->ball, GAME_NAME_LEN, "SAFARI BALL");
        r->permille = -1;
        return;
    }
    if (!species_info(m, p, species, info)) return;
    game_mem_read32(m, sb2 + SB2_ENCRYPT_KEY, &key);

    CatchInput in = {0};
    in.catch_rate = info[SI_CATCH_RATE];
    in.hp = rd16(target + BMON_HP);
    in.max_hp = rd16(target + BMON_MAX_HP);
    in.status1 = rd32(target + BMON_STATUS1);
    in.level = target[BMON_LEVEL];
    in.type1 = target[BMON_TYPE1];
    in.type2 = target[BMON_TYPE2];
    in.owned = dex_caught(m, p, sb1, sb2, species);
    game_mem_read8(m, p->battle_turn_counter, &in.turn_counter);
    uint8_t map_type = 0;
    game_mem_read8(m, p->map_header + MAP_HEADER_MAP_TYPE, &map_type);
    in.underwater = map_type == MAP_TYPE_UNDERWATER;

    for (int i = 0; i < BALL_POCKET_SLOTS && s->catch_count < 16; i++) {
        uint16_t id, qty;
        uint32_t slot = sb1 + SB1_BALL_POCKET + 4u * i;
        if (!game_mem_read16(m, slot, &id) || !game_mem_read16(m, slot + 2, &qty)) break;
        qty ^= (uint16_t)key;
        if (id < BALL_MASTER || id > BALL_PREMIER || qty == 0) continue;
        GameCatchRow* r = &s->catch_rows[s->catch_count++];
        game_item_name(m, p, id, r->ball);
        r->quantity = qty;
        in.ball = (uint8_t)id;
        r->permille = (int16_t)battle_catch_permille(&in);
    }
}

static void read_encounters(const GameMem* m, const GameProfile* p, uint32_t sb1,
                            uint32_t sb2, GameSnapshot* s) {
    EncounterList lists[ENC_METHOD_COUNT];
    s->has_encounters = encounters_find(m, p->wild_mon_headers, p->wild_header_cap,
                                        p->species_count, s->map_group, s->map_num, lists);
    if (!s->has_encounters) return;
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        s->enc_count[k] = lists[k].count;
        for (uint8_t i = 0; i < lists[k].count; i++) {
            const EncounterEntry* src = &lists[k].entries[i];
            GameEncRow* row = &s->enc[k][i];
            game_species_name(m, p, src->species, row->species);
            row->min_level = src->min_level;
            row->max_level = src->max_level;
            row->percent = src->percent;
            row->caught = dex_caught(m, p, sb1, sb2, src->species);
        }
    }
}

void game_init(GameState* gs, const uint8_t* rom, uint32_t rom_size) {
    memset(gs, 0, sizeof(*gs));
    gs->profile = game_profile_detect(rom, rom_size, &gs->support);
    GameMem m = { NULL, NULL, rom, rom_size };
    berry_places_build(&gs->places, gs->profile, &m);
    species_index_build(&gs->search, gs->profile, &m);
    if (gs->profile) damage_tables_load(&m, gs->profile, &gs->dmg_tables);
}

void game_update_mem(GameState* gs, const GameMem* m) {
    const GameProfile* p = gs->profile;
    GameSnapshot* s = &gs->snap;
    uint32_t sb1 = 0, sb2 = 0;

    memset(s, 0, sizeof(*s));
    if (!p) {
        s->support = gs->support == GAME_SUPPORT_MODIFIED ? GAME_SUPPORT_MODIFIED
                                                          : GAME_SUPPORT_OTHER_GAME;
        game_strcpy(s->reason, sizeof(s->reason),
                    s->support == GAME_SUPPORT_MODIFIED ? "ROM hack or modified ROM not supported"
                                                        : "Game not supported");
        return;
    }
    if (!game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_read32(m, p->save_block2_ptr, &sb2) ||
        !game_mem_in_ewram(sb1, SB1_MIN_SIZE) || !game_mem_in_ewram(sb2, SB2_MIN_SIZE)) {
        game_strcpy(s->reason, sizeof(s->reason), "Waiting for save data");
        return;
    }
    s->valid = true;
    for (uint16_t sp = 1; sp < p->species_count; sp++) {
        uint16_t national = game_species_national(m, p, sp);
        if (national != 0 && dex_caught(m, p, sb1, sb2, sp))
            s->dex_caught[(national - 1u) / 8] |= (uint8_t)(1u << ((national - 1u) % 8));
    }
    game_mem_read8(m, sb1 + SB1_MAP_GROUP, &s->map_group);
    game_mem_read8(m, sb1 + SB1_MAP_NUM, &s->map_num);
    game_mem_read8(m, p->map_header + MAP_HEADER_MAP_TYPE, &s->map_type);
    map_name(m, p, s->map_name);
    read_party(m, p, s);
    mirage_read(&gs->mirage, p, m, &gs->clock, sb1, sb2, &s->mirage);
    berry_read(&gs->places, p, m, &gs->clock, &s->berries);

    uint8_t enemies[2][BMON_SIZE], player[BMON_SIZE], enemy_ids[2] = {0, 0};
    bool have_player;
    uint8_t player_battler = 0;
    uint8_t n = read_battlers(m, p, enemies, enemy_ids, player, &have_player, &player_battler);
    if (n == 0) {
        s->context = GAME_CTX_OVERWORLD;
        read_encounters(m, p, sb1, sb2, s);
        return;
    }

    uint32_t flags = 0;
    game_mem_read32(m, p->battle_type_flags, &flags);
    s->context = (flags & BATTLE_TYPE_TRAINER) ? GAME_CTX_BATTLE_TRAINER : GAME_CTX_BATTLE_WILD;
    uint8_t type_table[TYPE_TABLE_MAX];
    uint32_t type_len = p->type_effectiveness_len <= TYPE_TABLE_MAX ? p->type_effectiveness_len : 0;
    if (type_len && !game_mem_copy(m, p->type_effectiveness, type_table, type_len)) type_len = 0;
    /* Damage for enemies[0] only (R20); enemy 1 keeps DMG_NONE. */
    DamageCtx ctx;
    DamageMon dm_player, dm_enemy;
    bool dmg = have_player && gs->dmg_tables.valid &&
               damage_read_battle(m, p, player_battler, enemy_ids[0], &ctx, &dm_player, &dm_enemy);
    DamagePair to_player = { &gs->dmg_tables, &ctx, &dm_enemy, &dm_player };
    DamagePair from_player = { &gs->dmg_tables, &ctx, &dm_player, &dm_enemy };
    for (uint8_t i = 0; i < n; i++)
        fill_enemy(m, p, enemies[i], player, have_player, type_table, type_len,
                   dmg && i == 0 ? &to_player : NULL, dmg && i == 0 ? &from_player : NULL,
                   &s->enemies[i]);
    s->enemy_count = n;
    if (have_player) read_active_name(m, p, player_battler, s);
    if (s->context == GAME_CTX_BATTLE_WILD) read_catch_rows(m, p, sb1, sb2, enemies[0], flags, s);
}

/* The RTC date as the S-3511A reports it; only host-side cartridge state is read. */
static void read_clock(const GBA* gba, MirageClock* c) {
    struct tm t;
    memset(c, 0, sizeof(*c));
    if (!rtc_effective_tm(&gba->cart.rtc, &t) || t.tm_year < 100 || t.tm_year > 199) return;
    c->valid = true;
    c->year = (uint8_t)(t.tm_year - 100);
    c->month = (uint8_t)(t.tm_mon + 1);
    c->day = (uint8_t)t.tm_mday;
    c->hour = (uint8_t)t.tm_hour;
    c->minute = (uint8_t)t.tm_min;
    c->second = (uint8_t)t.tm_sec;
}

void game_update(GameState* gs, const GBA* gba) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    read_clock(gba, &gs->clock);
    game_update_mem(gs, &m);
}

void game_read_berries(GameState* gs, const GBA* gba, BerryView* out) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    read_clock(gba, &gs->clock);
    berry_read(&gs->places, gs->profile, &m, &gs->clock, out);
}

#define BOX_MON_SIZE 0x50 /* struct BoxPokemon, the start of a DaycareMon */

/* Level for exp on a growth rate's row of gExperienceTables; false when unreadable. */
static bool level_for_exp(const GameMem* m, const GameProfile* p, uint8_t growth, uint32_t exp,
                          uint8_t* level) {
    uint8_t raw[4 * 101];
    uint32_t table[101];
    if (growth > 5 ||
        !game_mem_copy(m, p->experience_tables + 4u * 101u * growth, raw, sizeof(raw)))
        return false;
    for (int i = 0; i <= 100; i++) table[i] = rd32(raw + 4 * i);
    *level = daycare_level_for_exp(table, exp);
    return true;
}

typedef struct {
    MonData d;
    uint8_t info[SPECIES_INFO_SIZE];
} DaycareRead;

/* Egg guess from the two clean parents and offspringPersonality (CreateMon with that PID). */
static void daycare_egg_guess(const GameState* gs, const GameMem* m, uint32_t sb2,
                              const DaycareRead par[2], uint32_t pid, GameDaycare* out) {
    const GameProfile* p = gs->profile;
    uint16_t species[2], items[2];
    MonGender gender[2];
    uint8_t info[SPECIES_INFO_SIZE];
    uint32_t tid;
    if (!gs->search.ok) return;
    for (uint8_t i = 0; i < 2; i++) {
        species[i] = par[i].d.species;
        items[i] = par[i].d.held_item;
        gender[i] = pokemon_gender(par[i].info[SI_GENDER_RATIO], par[i].d.personality);
    }
    uint16_t egg =
        daycare_egg_species(species, gender, items, pid, gs->search.parent, SPECIES_MAX, p);
    if (egg == 0 || !species_info(m, p, egg, info) ||
        !game_mem_read32(m, sb2 + SB2_TRAINER_ID, &tid))
        return;
    out->egg_known = true;
    game_species_name(m, p, egg, out->egg_species);
    out->egg_gender = pokemon_gender(info[SI_GENDER_RATIO], pid);
    uint8_t nature = pokemon_nature(pid);
    game_nature_name(m, p, nature, out->egg_nature);
    pokemon_nature_effect(nature, &out->egg_nature_up, &out->egg_nature_down);
    /* CreateBoxMon: the PID's low bit picks the slot only when the species has two abilities. */
    ability_name(m, p, info[SI_ABILITY2] != 0 && (pid & 1) ? info[SI_ABILITY2] : info[SI_ABILITY1],
                 out->egg_ability);
    out->egg_shiny = ((tid >> 16) ^ (tid & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8;
}

/* Flame Body check and hatch countdown for the party's eggs. Left at zero when a party mon's
 * ability can't be read, since the countdown would depend on it. pid/clean/torn: hold keys
 * by party slot, as in daycare_read_once. */
static void daycare_party(const GameMem* m, const GameProfile* p, uint8_t step_counter,
                          uint32_t pid[6], bool clean[6], bool torn[6], GameDaycare* out) {
    DaycareSlot slots[6];
    uint8_t count, n = 0;
    bool fire = false;
    memset(slots, 0, sizeof(slots));
    if (!game_mem_read8(m, p->player_party_count, &count)) return;
    if (count > 6) count = 6;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t raw[MON_PARTY_SIZE], info[SPECIES_INFO_SIZE];
        MonData d;
        if (!game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, sizeof(raw)))
            return;
        if (!pokemon_decode(raw, true, &d)) continue;
        DaycareSlot* s = &slots[n++];
        pid[i] = d.personality;
        torn[i] = s->bad = d.bad;
        if (d.bad) continue;
        if (!species_info(m, p, d.species, info)) return;
        clean[i] = true;
        /* abilities[abilityNum] with no fallback (GetAbilityBySpecies) */
        uint8_t ability = info[d.ability_num ? SI_ABILITY2 : SI_ABILITY1];
        s->egg = d.is_egg;
        s->cycles = d.friendship;
        s->fire = ability == ABILITY_MAGMA_ARMOR || ability == ABILITY_FLAME_BODY;
        if (!s->egg && s->fire) fire = true;
    }
    out->fire = fire;
    daycare_hatch_steps(slots, n, step_counter, out->hatch_steps);
}

/* One read without the hold. Hold keys, daycare slots 0-1 then party slots: pid[] is the
 * header personality, clean[] whether it decoded fully (parents: with a known level),
 * torn[] whether it is present but not clean (party: failed its checksum). */
static void daycare_read_once(const GameState* gs, const GameMem* m, GameDaycare* out,
                              uint32_t* sb1_out, uint32_t pid[GAME_DC_KEYS],
                              bool clean[GAME_DC_KEYS], bool torn[GAME_DC_KEYS]) {
    const GameProfile* p = gs->profile;
    uint32_t sb1, sb2, offspring = 0;
    uint8_t flags = 0, counter = 0;
    DaycareRead par[2];
    memset(out, 0, sizeof(*out));
    for (uint8_t i = 0; i < GAME_DC_KEYS; i++) {
        pid[i] = 0;
        clean[i] = torn[i] = false;
    }
    if (!p || !game_mem_read32(m, p->save_block1_ptr, &sb1) ||
        !game_mem_read32(m, p->save_block2_ptr, &sb2) || !game_mem_in_ewram(sb1, SB1_MIN_SIZE) ||
        !game_mem_in_ewram(sb2, SB2_MIN_SIZE))
        return;
    out->valid = true;
    *sb1_out = sb1;
    uint32_t dc = sb1 + p->daycare_sb1_offset;
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t raw[BOX_MON_SIZE], lv_deposit, lv_now;
        uint32_t at = dc + DAYCARE_MON_SIZE * i;
        DaycareRead* r = &par[out->parent_count];
        if (!game_mem_copy(m, at, raw, sizeof(raw))) continue;
        bool present = pokemon_decode(raw, false, &r->d);
        pid[i] = r->d.personality;
        if (!present) continue;
        uint8_t n = out->parent_count++;
        out->slot[n] = i;
        game_mem_read32(m, at + DAYCARE_MON_STEPS, &out->steps[n]);
        uint32_t exp = r->d.experience;
        uint32_t exp_now = exp + out->steps[n] < exp ? UINT32_MAX : exp + out->steps[n];
        clean[i] = !r->d.bad && species_info(m, p, r->d.species, r->info) &&
                   level_for_exp(m, p, r->info[SI_GROWTH_RATE], exp, &lv_deposit) &&
                   level_for_exp(m, p, r->info[SI_GROWTH_RATE], exp_now, &lv_now);
        torn[i] = !clean[i];
        MonData shown = r->d;
        if (clean[i]) {
            shown.level = lv_now;
            shown.experience = exp_now;
            out->gained[n] = (uint8_t)(lv_now - lv_deposit);
            out->cost[n] = daycare_cost(lv_now, lv_deposit);
        }
        game_fill_mon(m, p, &shown, &out->mon[n]);
    }

    game_mem_read32(m, dc + DAYCARE_OFFSPRING, &offspring);
    game_mem_read8(m, dc + DAYCARE_STEP_COUNTER, &counter);
    game_mem_read8(m, sb1 + SB1_FLAGS + p->pending_egg_flag / 8u, &flags);
    out->egg_waiting = offspring != 0 && ((flags >> (p->pending_egg_flag % 8u)) & 1);
    /* Both slots present means par[0] is slot 0 and par[1] slot 1. */
    if (out->parent_count == 2 && clean[0] && clean[1]) {
        DaycareParent dp[2];
        for (uint8_t i = 0; i < 2; i++) {
            dp[i].species = par[i].d.species;
            dp[i].ot_id = par[i].d.ot_id;
            dp[i].personality = par[i].d.personality;
            dp[i].egg_group[0] = par[i].info[SI_EGG_GROUP1];
            dp[i].egg_group[1] = par[i].info[SI_EGG_GROUP2];
            dp[i].gender_ratio = par[i].info[SI_GENDER_RATIO];
        }
        DaycareCompatReason reason;
        out->compat = daycare_compat(&dp[0], &dp[1], &reason);
        out->compat_reason = (uint8_t)reason;
        out->compat_known = true;
        /* TryProduceOrHatchEgg rolls only while offspringPersonality is 0. */
        if (offspring == 0 && out->compat > 0) out->next_roll = daycare_next_roll(out->steps[1]);
        if (out->egg_waiting) daycare_egg_guess(gs, m, sb2, par, offspring, out);
    }
    daycare_party(m, p, counter, pid + 2, clean + 2, torn + 2, out);
}

/* R7: MoveSaveBlocks_ResetHeap moves the SB1 pointer before copying the data back, so the
 * frame after a move can read garbage, and a copy in flight can tear a slot. F5: a party slot
 * can tear too while GetEggCyclesToSubtract decrypts the party at a hatch check. */
void game_read_daycare_mem(GameState* gs, const GameMem* m, GameDaycare* out) {
    uint32_t sb1 = 0, pid[GAME_DC_KEYS];
    bool clean[GAME_DC_KEYS], torn[GAME_DC_KEYS];
    daycare_read_once(gs, m, out, &sb1, pid, clean, torn);
    if (!out->valid) {
        gs->dc_have = false;
        return;
    }
    bool moved = gs->dc_have && sb1 != gs->dc_sb1;
    bool tear = false;
    for (uint8_t i = 0; i < GAME_DC_KEYS; i++)
        if (gs->dc_have && torn[i] && gs->dc_pid[i] != 0 && pid[i] == gs->dc_pid[i]) tear = true;
    gs->dc_sb1 = sb1;
    if (moved || (tear && gs->dc_held == 0)) {
        gs->dc_held++;
        *out = gs->dc_last;
        return;
    }
    gs->dc_held = 0;
    gs->dc_last = *out;
    gs->dc_have = true;
    for (uint8_t i = 0; i < GAME_DC_KEYS; i++) gs->dc_pid[i] = clean[i] ? pid[i] : 0;
}

/* Drops the hold's history: while the Daycare is not read, SB1 keeps moving, so the next
 * read would count as moved and return a stale view. */
void game_daycare_forget(GameState* gs) {
    gs->dc_have = false;
    gs->dc_held = 0;
}

/* One read with no stability hold, and the hold state untouched: for the egg toast, which
 * runs while the panel is closed and the hold's last view would be stale. */
void game_read_daycare_now_mem(const GameState* gs, const GameMem* m, GameDaycare* out) {
    uint32_t sb1, pid[GAME_DC_KEYS];
    bool clean[GAME_DC_KEYS], torn[GAME_DC_KEYS];
    daycare_read_once(gs, m, out, &sb1, pid, clean, torn);
}

void game_read_daycare_now(const GameState* gs, const GBA* gba, GameDaycare* out) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    game_read_daycare_now_mem(gs, &m, out);
}

void game_read_daycare(GameState* gs, const GBA* gba, GameDaycare* out) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    game_read_daycare_mem(gs, &m, out);
}

bool game_read_daycare_egg(const GameState* gs, const GBA* gba, uint32_t* pid, uint32_t* sb1) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    const GameProfile* p = gs->profile;
    uint32_t sb2, offspring;
    uint8_t flags;
    *pid = 0;
    *sb1 = 0;
    if (!p || !game_mem_read32(&m, p->save_block1_ptr, sb1) ||
        !game_mem_read32(&m, p->save_block2_ptr, &sb2) || !game_mem_in_ewram(*sb1, SB1_MIN_SIZE) ||
        !game_mem_in_ewram(sb2, SB2_MIN_SIZE) ||
        !game_mem_read32(&m, *sb1 + p->daycare_sb1_offset + DAYCARE_OFFSPRING, &offspring) ||
        !game_mem_read8(&m, *sb1 + SB1_FLAGS + p->pending_egg_flag / 8u, &flags))
        return false;
    if ((flags >> (p->pending_egg_flag % 8u)) & 1) *pid = offspring;
    return true;
}

bool game_dump_daycare(const GameDaycare* d, FILE* out) {
    int ok = 1;
    if (!d->valid) return fprintf(out, "daycare invalid\n") >= 0;
    ok &= fprintf(out, "daycare parents %u compat %u reason %u next_roll %u egg %d known %d",
                  d->parent_count, d->compat, d->compat_reason, d->next_roll, d->egg_waiting,
                  d->egg_known) >= 0;
    if (d->egg_known)
        ok &= fprintf(out, " species %s gender %d nature %s ability %s shiny %d", d->egg_species,
                      (int)d->egg_gender, d->egg_nature, d->egg_ability, d->egg_shiny) >= 0;
    ok &= fprintf(out, " fire %d\n", d->fire) >= 0;
    for (uint8_t i = 0; i < d->parent_count; i++) {
        if (d->mon[i].bad) {
            ok &= fprintf(out, "daycare parent %u bad\n", d->slot[i]) >= 0;
            continue;
        }
        ok &= fprintf(out, "daycare parent %u %s lv %u gained %u steps %u cost %u\n", d->slot[i],
                      d->mon[i].species, d->mon[i].level, d->gained[i], (unsigned)d->steps[i],
                      (unsigned)d->cost[i]) >= 0;
    }
    for (uint8_t i = 0; i < 6; i++)
        if (d->hatch_steps[i])
            ok &= fprintf(out, "daycare hatch %u %u\n", i, (unsigned)d->hatch_steps[i]) >= 0;
    return ok != 0;
}

const char* game_method_name(EncMethod method) {
    static const char* const names[ENC_METHOD_COUNT] = {
        "Grass", "Surf", "Rock Smash", "Old Rod", "Good Rod", "Super Rod",
    };
    return (unsigned)method < ENC_METHOD_COUNT ? names[method] : "?";
}

/* "<tag> <prefix><j> <name> type <t> x<q>", then " dmg <kind> <lo> <hi>" or " status". */
static int dump_move(FILE* out, const char* tag, const char* prefix, uint8_t j, const GameMoveEff* mv) {
    if (mv->status_move)
        return fprintf(out, "%s %s%u %s type %u x%u status\n", tag, prefix, j, mv->move, mv->type,
                       mv->quarters) >= 0;
    return fprintf(out, "%s %s%u %s type %u x%u dmg %u %u %u\n", tag, prefix, j, mv->move, mv->type,
                   mv->quarters, mv->dmg_kind, (unsigned)mv->dmg_lo, (unsigned)mv->dmg_hi) >= 0;
}

bool game_dump(const GameSnapshot* s, FILE* out) {
    int ok = 1;
    if (!s->valid) return fprintf(out, "invalid: %s\n", s->reason) >= 0;
    ok &= fprintf(out, "context %d map %u:%u type %u name %s\n", (int)s->context, s->map_group,
                  s->map_num, s->map_type, s->map_name) >= 0;
    for (uint8_t i = 0; i < s->party_count; i++) {
        const GamePartyMon* p = &s->party[i];
        if (p->bad) { ok &= fprintf(out, "party %u bad\n", i) >= 0; continue; }
        ok &= fprintf(out,
                      "party %u %s (%s) lv%u nature %s iv %u/%u/%u/%u/%u/%u ev %u/%u/%u/%u/%u/%u "
                      "hp %s %u ability %s item %s friend %u next %u%s\n",
                      i, p->nickname, p->species, p->level, p->nature, p->ivs[0], p->ivs[1],
                      p->ivs[2], p->ivs[3], p->ivs[4], p->ivs[5], p->evs[0], p->evs[1], p->evs[2],
                      p->evs[3], p->evs[4], p->evs[5], p->hp_type, p->hp_power, p->ability,
                      p->item, p->friendship, (unsigned)p->exp_to_next, p->egg ? " egg" : "") >= 0;
    }
    if (s->mirage.valid) {
        const MirageInfo* mi = &s->mirage;
        ok &= fprintf(out, "mirage value %08X clock_set %d known %d pending %u today %d/%u %s\n",
                      (unsigned)mi->value, mi->clock_set, mi->clock_known, mi->pending,
                      (int)mi->today.where, mi->today.slot, mi->today.name) >= 0;
        for (uint8_t i = 0; i < mi->forecast_count; i++)
            ok &= fprintf(out, "mirage_next %u %d/%u %s\n", mi->forecast[i].days,
                          (int)mi->forecast[i].where, mi->forecast[i].slot,
                          mi->forecast[i].name) >= 0;
    }
    if (s->berries.valid)
        ok &= fprintf(out, "berries count %u clock_set %d projected %d here %d\n", s->berries.count,
                      s->berries.clock_set, s->berries.projected,
                      s->berries.here == BERRY_PLACE_NONE ? -1 : (int)s->berries.here) >= 0;
    for (uint8_t i = 0; i < s->berries.count; i++) {
        const BerryTree* t = &s->berries.tree[i];
        const char* place = t->place < s->berries.place_count ? s->berries.place[t->place].name : "Other";
        ok &= fprintf(out, "berry %u %s %s stage %u next %u ripe %u yield %u-%u status %d water %u%s\n", t->id,
                      place, t->name, t->stage, t->next_min, (unsigned)t->ripe_min, t->yield_lo, t->yield_hi,
                      (int)t->status, t->watered, t->paused ? " paused" : "") >= 0;
    }
    for (uint8_t i = 0; i < s->enemy_count; i++) {
        const GameEnemy* e = &s->enemies[i];
        ok &= fprintf(out, "enemy %u %s lv%u hp %u/%u nature %s iv %u/%u/%u/%u/%u/%u ability %s item %s\n",
                      i, e->species, e->level, e->hp, e->max_hp, e->nature, e->ivs[0], e->ivs[1],
                      e->ivs[2], e->ivs[3], e->ivs[4], e->ivs[5], e->ability, e->item) >= 0;
        for (uint8_t j = 0; i == 0 && j < e->eff_count; j++)
            ok &= dump_move(out, "your_move", "", j, &e->eff[j]);
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%u ", i);
        for (uint8_t j = 0; j < e->move_count; j++) ok &= dump_move(out, "enemy_move", prefix, j, &e->moves[j]);
    }
    for (uint8_t i = 0; i < s->catch_count; i++)
        ok &= fprintf(out, "catch %s x%u %d\n", s->catch_rows[i].ball, s->catch_rows[i].quantity,
                      s->catch_rows[i].permille) >= 0;
    for (int k = 0; k < ENC_METHOD_COUNT; k++)
        for (uint8_t i = 0; i < s->enc_count[k]; i++)
            ok &= fprintf(out, "enc %s %s lv%u-%u %u%%%s\n", game_method_name((EncMethod)k),
                          s->enc[k][i].species, s->enc[k][i].min_level, s->enc[k][i].max_level,
                          s->enc[k][i].percent, s->enc[k][i].caught ? " caught" : "") >= 0;
    return ok != 0;
}

bool game_in_battle(const GameState* gs, const GBA* gba) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    uint8_t flags;
    return gs->profile && game_mem_read8(&m, gs->profile->main_in_battle, &flags) && (flags & 0x02);
}
