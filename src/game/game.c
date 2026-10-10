#include "game/game.h"
#include "game/battle_info.h"
#include "game/gen3_text.h"
#include "gba.h"
#include <string.h>

/* SaveBlock1 / SaveBlock2 (pokeemerald include/global.h) */
#define SB1_MAP_GROUP    0x04
#define SB1_MAP_NUM      0x05
#define SB1_BALL_POCKET  0x650
#define SB1_DEX_SEEN1    0x988
#define SB1_DEX_SEEN2    0x3B24
#define SB1_MIN_SIZE     (SB1_DEX_SEEN2 + 52)
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

static void species_name(const GameMem* m, const GameProfile* p, uint16_t s, char* out) {
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
static void item_name(const GameMem* m, const GameProfile* p, uint16_t item, char* out) {
    if (item == 0) {
        game_strcpy(out, GAME_NAME_LEN, "--");
        return;
    }
    table_name(m, p->items, p->item_count, ITEM_SIZE, ITEM_NAME_LEN, item, out);
}

static void nature_name(const GameMem* m, const GameProfile* p, uint8_t nature, char* out) {
    uint32_t ptr;
    uint8_t raw[12];
    uint32_t n = 0;
    game_strcpy(out, GAME_NAME_LEN, "???");
    if (nature >= 25 || !game_mem_read32(m, p->nature_name_ptrs + 4u * nature, &ptr)) return;
    while (n < sizeof(raw) && game_mem_read8(m, ptr + n, &raw[n]) && raw[n] != 0xFF) n++;
    if (n > 0) gen3_decode(raw, n, out, GAME_NAME_LEN);
}

/* Mirrors GetMapName: gRegionMapEntries[gMapHeader.regionMapSectionId].name. */
static void map_name(const GameMem* m, const GameProfile* p, char* out) {
    uint8_t sec;
    uint32_t ptr;
    uint8_t raw[GAME_MAP_NAME_LEN];
    uint32_t n = 0;
    out[0] = '\0';
    if (!game_mem_read8(m, p->map_header + MAP_HEADER_REGION_MAP_SEC, &sec) ||
        sec >= p->region_map_count ||
        !game_mem_read32(m, p->region_map_entries + sec * REGION_MAP_ENTRY_SIZE + REGION_MAP_ENTRY_NAME,
                         &ptr))
        return;
    while (n < sizeof(raw) && game_mem_read8(m, ptr + n, &raw[n]) && raw[n] != 0xFF) n++;
    if (n > 0) gen3_decode(raw, n, out, GAME_MAP_NAME_LEN);
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

/* Mirrors GetSetPokedexFlag(FLAG_GET_CAUGHT) without its write-back. */
static bool dex_caught(const GameMem* m, const GameProfile* p, uint32_t sb1, uint32_t sb2,
                       uint16_t species) {
    uint16_t national;
    if (species == 0 || species >= p->species_count) return false;
    if (!game_mem_read16(m, p->species_to_national + (species - 1u) * 2, &national) ||
        national == 0 || national > 52 * 8)
        return false;
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

static void fill_party_mon(const GameMem* m, const GameProfile* p, const MonData* d,
                           GamePartyMon* out) {
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
    species_name(m, p, d->species, out->species);
    out->level = d->level;
    memcpy(out->ivs, d->ivs, 6);
    memcpy(out->evs, d->evs, 6);
    uint8_t nature = pokemon_nature(d->personality);
    nature_name(m, p, nature, out->nature);
    pokemon_nature_effect(nature, &out->nature_up, &out->nature_down);
    type_name(m, p, pokemon_hidden_power_type(d->ivs), out->hp_type);
    out->hp_power = pokemon_hidden_power_power(d->ivs);
    out->friendship = d->friendship;
    item_name(m, p, d->held_item, out->item);
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
        fill_party_mon(m, p, &d, &s->party[s->party_count++]);
    }
}

/* Battlers: returns enemy battle structs (up to 2) and the player's active one. */
static uint8_t read_battlers(const GameMem* m, const GameProfile* p,
                             uint8_t enemies[2][BMON_SIZE], uint8_t player[BMON_SIZE],
                             bool* have_player, uint8_t* player_battler) {
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
            if (species != 0 && species < p->species_count && n < 2) memcpy(enemies[n++], mon, BMON_SIZE);
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

static void fill_enemy(const GameMem* m, const GameProfile* p, const uint8_t* mon,
                       const uint8_t* player, bool have_player, const uint8_t* type_table,
                       uint32_t type_len, GameEnemy* e) {
    uint8_t info[SPECIES_INFO_SIZE];
    uint16_t species = rd16(mon + BMON_SPECIES);
    uint32_t personality = rd32(mon + BMON_PERSONALITY);
    uint8_t ability = mon[BMON_ABILITY];
    uint8_t t1 = mon[BMON_TYPE1], t2 = mon[BMON_TYPE2];

    memset(e, 0, sizeof(*e));
    e->type_ids[0] = t1;
    e->type_ids[1] = t2;
    species_name(m, p, species, e->species);
    e->level = mon[BMON_LEVEL];
    e->gender = species_info(m, p, species, info)
                    ? pokemon_gender(info[SI_GENDER_RATIO], personality) : MON_GENDER_NONE;
    uint8_t nature = pokemon_nature(personality);
    nature_name(m, p, nature, e->nature);
    pokemon_nature_effect(nature, &e->nature_up, &e->nature_down);
    pokemon_unpack_ivs(rd32(mon + BMON_IVS), e->ivs);
    e->hp = rd16(mon + BMON_HP);
    e->max_hp = rd16(mon + BMON_MAX_HP);
    ability_name(m, p, ability, e->ability);
    item_name(m, p, rd16(mon + BMON_ITEM), e->item);
    e->status1 = rd32(mon + BMON_STATUS1);
    type_name(m, p, t1, e->type1);
    if (t2 != t1) type_name(m, p, t2, e->type2);

    if (!have_player) return;
    bool foresight = (rd32(mon + BMON_STATUS2) & STATUS2_FORESIGHT) != 0;
    for (int i = 0; i < 4; i++) {
        uint16_t mv = rd16(player + BMON_MOVES + 2 * i);
        uint8_t bm[BATTLE_MOVE_SIZE];
        if (mv == 0) continue;
        GameMoveEff* eff = &e->eff[e->eff_count++];
        eff->type = 0xFF;
        move_name(m, p, mv, eff->move);
        if (mv >= p->move_count ||
            !game_mem_copy(m, p->battle_moves + (uint32_t)mv * BATTLE_MOVE_SIZE, bm, sizeof(bm))) {
            eff->status_move = true;
            continue;
        }
        uint8_t move_type = bm[2];
        if (bm[1] == 0) {
            eff->status_move = true;
            eff->type = move_type;
            continue;
        }
        if (mv == MOVE_HIDDEN_POWER) {
            uint8_t ivs[6];
            pokemon_unpack_ivs(rd32(player + BMON_IVS), ivs);
            move_type = pokemon_hidden_power_type(ivs);
        }
        eff->type = move_type;
        uint8_t q = battle_type_quarters(type_table, type_len, move_type, t1, t2, foresight);
        eff->quarters = battle_apply_ability(q, move_type, ability);
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
        item_name(m, p, id, r->ball);
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
            species_name(m, p, src->species, row->species);
            row->min_level = src->min_level;
            row->max_level = src->max_level;
            row->percent = src->percent;
            row->caught = dex_caught(m, p, sb1, sb2, src->species);
        }
    }
}

void game_init(GameState* gs, const uint8_t* rom, uint32_t rom_size) {
    memset(gs, 0, sizeof(*gs));
    gs->profile = game_profile_detect(rom, rom_size);
}

void game_update_mem(GameState* gs, const GameMem* m) {
    const GameProfile* p = gs->profile;
    GameSnapshot* s = &gs->snap;
    uint32_t sb1 = 0, sb2 = 0;

    memset(s, 0, sizeof(*s));
    if (!p) {
        game_strcpy(s->reason, sizeof(s->reason), "No game info for this ROM");
        return;
    }
    if (!game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_read32(m, p->save_block2_ptr, &sb2) ||
        !game_mem_in_ewram(sb1, SB1_MIN_SIZE) || !game_mem_in_ewram(sb2, SB2_MIN_SIZE)) {
        game_strcpy(s->reason, sizeof(s->reason), "Waiting for save data");
        return;
    }
    s->valid = true;
    game_mem_read8(m, sb1 + SB1_MAP_GROUP, &s->map_group);
    game_mem_read8(m, sb1 + SB1_MAP_NUM, &s->map_num);
    game_mem_read8(m, p->map_header + MAP_HEADER_MAP_TYPE, &s->map_type);
    map_name(m, p, s->map_name);
    read_party(m, p, s);

    uint8_t enemies[2][BMON_SIZE], player[BMON_SIZE];
    bool have_player;
    uint8_t player_battler = 0;
    uint8_t n = read_battlers(m, p, enemies, player, &have_player, &player_battler);
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
    for (uint8_t i = 0; i < n; i++)
        fill_enemy(m, p, enemies[i], player, have_player, type_table, type_len, &s->enemies[i]);
    s->enemy_count = n;
    if (have_player) read_active_name(m, p, player_battler, s);
    if (s->context == GAME_CTX_BATTLE_WILD) read_catch_rows(m, p, sb1, sb2, enemies[0], flags, s);
}

void game_update(GameState* gs, const GBA* gba) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    game_update_mem(gs, &m);
}

const char* game_method_name(EncMethod method) {
    static const char* const names[ENC_METHOD_COUNT] = {
        "Grass", "Surf", "Rock Smash", "Old Rod", "Good Rod", "Super Rod",
    };
    return (unsigned)method < ENC_METHOD_COUNT ? names[method] : "?";
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
    for (uint8_t i = 0; i < s->enemy_count; i++) {
        const GameEnemy* e = &s->enemies[i];
        ok &= fprintf(out, "enemy %u %s lv%u hp %u/%u nature %s iv %u/%u/%u/%u/%u/%u ability %s item %s\n",
                      i, e->species, e->level, e->hp, e->max_hp, e->nature, e->ivs[0], e->ivs[1],
                      e->ivs[2], e->ivs[3], e->ivs[4], e->ivs[5], e->ability, e->item) >= 0;
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
