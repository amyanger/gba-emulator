#include "game/runlog_read.h"
#include <string.h>

/* SaveBlock1 / SaveBlock2 (pret include/global.h) */
#define SB1_LOCATION   0x04     /* struct WarpData: s8 mapGroup, s8 mapNum */
#define SB1_FLAGS      0x1270   /* u8 flags[NUM_FLAG_BYTES] */
#define SB1_VARS       0x139C   /* u16 vars[], index id - 0x4000 */
#define SB1_GAME_STATS 0x159C   /* u32 gameStats[64], stored ^ SB2 encryptionKey */
#define SB1_READ_SIZE  (SB1_GAME_STATS + 4 * RUN_STATS)
#define SB1_FLAG_BYTES 300      /* NUM_FLAG_BYTES */
#define SB2_NAME       0x00
#define SB2_DEX_OWNED  0x28     /* pokedex (+0x18) owned[52] (+0x10) */
#define SB2_DEX_SEEN   0x5C     /* pokedex seen[52] (+0x44) */
#define SB2_KEY        0xAC     /* u32 encryptionKey */
#define SB2_READ_SIZE  (SB2_KEY + 4)
#define DEX_COUNT      386      /* NATIONAL_DEX_COUNT, bit n-1 = national dex n */

/* include/constants/flags.h, vars.h */
#define FLAG_BADGE01_GET 0x867
#define TRAINER_FLAGS_START 0x500
#define VAR_STARTER_MON 0x4023
#define TRAINER_WALLACE 335

/* struct BattlePokemon (include/pokemon.h) */
#define BMON_SIZE    0x58
#define BMON_SPECIES 0x00
#define BMON_HP      0x28
#define BMON_LEVEL   0x2A
#define BMON_MAX_HP  0x2C

/* struct Trainer (include/data.h); ROM party entries are 16 bytes with custom
 * moves (F_TRAINER_PARTY_CUSTOM_MOVESET), else 8, species at +4. */
#define TRAINER_SIZE       0x28
#define TRAINER_PARTY_SIZE 0x20
#define TRAINER_PARTY_PTR  0x24

/* gBattleTypeFlags bits (include/constants/battle.h) */
#define BT_LINK             (1u << 1)
#define BT_TRAINER          (1u << 3)
#define BT_BATTLE_TOWER     (1u << 8)
#define BT_EREADER_TRAINER  (1u << 11)
#define BT_DOME             (1u << 16)
#define BT_PALACE           (1u << 17)
#define BT_ARENA            (1u << 18)
#define BT_FACTORY          (1u << 19)
#define BT_PIKE             (1u << 20)
#define BT_PYRAMID          (1u << 21)
#define BT_INGAME_PARTNER   (1u << 22)
#define BT_TOWER_LINK_MULTI (1u << 23)
#define BT_RECORDED         (1u << 24)
#define BT_RECORDED_LINK    (1u << 25)
#define BT_TRAINER_HILL     (1u << 26)
#define BT_SECRET_BASE      (1u << 27)
#define BT_FRONTIER (BT_BATTLE_TOWER | BT_DOME | BT_PALACE | BT_ARENA | BT_FACTORY | BT_PIKE | BT_PYRAMID)

/* Battles whose player party is not the run's own (rentals, frontier picks, link,
 * recorded playback) or that bring a partner's mons (Steven). */
#define BT_FACILITY (BT_LINK | BT_FRONTIER | BT_INGAME_PARTNER | BT_TOWER_LINK_MULTI | BT_RECORDED | \
                     BT_RECORDED_LINK | BT_TRAINER_HILL)
/* gTrainerBattleOpponent_A is a gTrainers id only without these. */
#define BT_NOT_PLAIN (BT_LINK | BT_FRONTIER | BT_TOWER_LINK_MULTI | BT_RECORDED | BT_RECORDED_LINK | \
                      BT_TRAINER_HILL | BT_SECRET_BASE | BT_EREADER_TRAINER)

/* Battle Tent, Battle Frontier facility and Trainer Hill maps, from pret
 * data/maps/map_groups.json (group index, map index). The Frontier's outside
 * areas, lounges, Center and Mart are left out: the player's own party is there. */
typedef struct { uint8_t group, first, last; } MapRange;
static const MapRange k_facility_maps[] = {
    {5, 1, 3},    /* FallarborTown_BattleTent Lobby, Corridor, BattleRoom */
    {6, 0, 2},    /* VerdanturfTown_BattleTent Lobby, Corridor, BattleRoom */
    {9, 2, 4},    /* SlateportCity_BattleTent Lobby, Corridor, BattleRoom */
    {26, 5, 8},   /* BattleFrontier_BattleTower Lobby, Elevator, Corridor, BattleRoom */
    {26, 15, 39}, /* BattleTower multi rooms, Dome, Palace, Pyramid, Arena, Factory, Pike */
    {26, 60, 65}, /* TrainerHill Entrance, 1F-4F, Roof */
    {26, 88, 88}, /* TrainerHill_Elevator */
};

typedef struct { uint8_t story; uint16_t flag; } StoryFlag;
static const StoryFlag k_story_flags[] = {
    {RUN_STORY_STARTER, 0x860},  /* FLAG_SYS_POKEMON_GET */
    {RUN_STORY_POKEDEX, 0x861},  /* FLAG_SYS_POKEDEX_GET */
    {RUN_STORY_CHIMNEY, 0x8B},   /* FLAG_DEFEATED_EVIL_TEAM_MT_CHIMNEY */
    {RUN_STORY_SURF, 0x7A},      /* FLAG_RECEIVED_HM_SURF */
    {RUN_STORY_GROUDON, 0x6F},   /* FLAG_GROUDON_AWAKENED_MAGMA_HIDEOUT */
    {RUN_STORY_KYOGRE, 0x81},    /* FLAG_KYOGRE_ESCAPED_SEAFLOOR_CAVERN */
    {RUN_STORY_E4, 0x107},       /* FLAG_ENTERED_ELITE_FOUR */
    {RUN_STORY_CHAMPION, TRAINER_FLAGS_START + TRAINER_WALLACE},
    {RUN_STORY_HOF, 0x864},      /* FLAG_SYS_GAME_CLEAR */
    {RUN_STORY_NATDEX, 0x896},   /* FLAG_SYS_NATIONAL_DEX */
};

static bool flag_set(const uint8_t* flags, uint16_t id) {
    return (id >> 3) < SB1_FLAG_BYTES && (flags[id >> 3] >> (id & 7)) & 1;
}

static uint16_t var_get(const GameMem* m, uint32_t sb1, uint16_t id) {
    uint16_t v = 0;
    game_mem_read16(m, sb1 + SB1_VARS + 2u * (uint32_t)(id - 0x4000u), &v);
    return v;
}

static uint16_t dex_count(const uint8_t* bits) {
    uint16_t n = 0;
    for (uint32_t i = 0; i < DEX_COUNT; i++) n = (uint16_t)(n + ((bits[i >> 3] >> (i & 7)) & 1));
    return n;
}

bool run_is_leader(uint16_t trainer) {
    return (trainer >= 261 && trainer <= 272) || trainer == TRAINER_WALLACE;
}

static bool facility_map(uint8_t group, uint8_t num) {
    for (uint32_t i = 0; i < sizeof(k_facility_maps) / sizeof(k_facility_maps[0]); i++) {
        const MapRange* r = &k_facility_maps[i];
        if (group == r->group && num >= r->first && num <= r->last) return true;
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
    if (flags & BT_FACILITY) f->facility = true;
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
    if ((flags & BT_TRAINER) && !(flags & BT_NOT_PLAIN) &&
        game_mem_read16(m, p->trainer_battle_opponent_a, &opp) && opp > 0 && opp < p->trainer_count) {
        f->opponent = opp;
        f->ace_species = trainer_ace(p, m, opp);
    }
}

bool run_read(const GameProfile* p, const GameMem* m, uint16_t enemy_species, RunFacts* f) {
    uint8_t flags[SB1_FLAG_BYTES];
    uint8_t sb2[SB2_READ_SIZE];
    uint32_t sb1_addr = 0, sb2_addr = 0;
    memset(f, 0, sizeof(*f));
    if (!p || !game_mem_read32(m, p->save_block1_ptr, &sb1_addr) ||
        !game_mem_read32(m, p->save_block2_ptr, &sb2_addr) ||
        !game_mem_in_ewram(sb1_addr, SB1_READ_SIZE) || !game_mem_in_ewram(sb2_addr, SB2_READ_SIZE) ||
        !game_mem_copy(m, sb1_addr + SB1_FLAGS, flags, sizeof(flags)) ||
        !game_mem_copy(m, sb2_addr, sb2, sizeof(sb2)))
        return false;
    f->valid = true;
    f->sb1 = sb1_addr;
    f->sb2 = sb2_addr;
    memcpy(f->player_name, sb2 + SB2_NAME, sizeof(f->player_name));

    /* Stats and key come from this same frame: the key changes on new game. */
    uint32_t key = (uint32_t)sb2[SB2_KEY] | ((uint32_t)sb2[SB2_KEY + 1] << 8) |
                   ((uint32_t)sb2[SB2_KEY + 2] << 16) | ((uint32_t)sb2[SB2_KEY + 3] << 24);
    for (uint32_t i = 0; i < RUN_STATS; i++) {
        uint32_t v = 0;
        game_mem_read32(m, sb1_addr + SB1_GAME_STATS + 4 * i, &v);
        f->stats[i] = v ^ key;
    }

    for (uint16_t b = 0; b < RUN_BADGES; b++)
        if (flag_set(flags, (uint16_t)(FLAG_BADGE01_GET + b))) f->badges |= (uint8_t)(1u << b);
    for (uint32_t i = 0; i < sizeof(k_story_flags) / sizeof(k_story_flags[0]); i++)
        if (flag_set(flags, k_story_flags[i].flag)) f->story |= (uint16_t)(1u << k_story_flags[i].story);
    if (var_get(m, sb1_addr, 0x40B3) >= 1) f->story |= 1u << RUN_STORY_WEATHER;    /* VAR_WEATHER_INSTITUTE_STATE */
    if (var_get(m, sb1_addr, 0x40CA) >= 2) f->story |= 1u << RUN_STORY_SOOTOPOLIS; /* VAR_SKY_PILLAR_STATE */
    f->starter = (uint8_t)var_get(m, sb1_addr, VAR_STARTER_MON);

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
    f->facility = facility_map(f->map_group, f->map_num);
    read_battle(p, m, f);
    return true;
}
