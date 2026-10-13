#ifndef GAME_H
#define GAME_H

#include <stdio.h>
#include "game/game_mem.h"
#include "game/emerald_profile.h"
#include "game/pokemon.h"
#include "game/encounters.h"
#include "game/mirage.h"
#include "game/berry.h"
#include "game/species_search.h"
#include "game/damage.h"

typedef struct GBA GBA;

typedef enum {
    GAME_CTX_NONE,
    GAME_CTX_OVERWORLD,
    GAME_CTX_BATTLE_WILD,
    GAME_CTX_BATTLE_TRAINER
} GameContext;

#define GAME_NAME_LEN 16
#define GAME_MAP_NAME_LEN 24

typedef struct {
    bool present, bad, egg;
    char nickname[GAME_NAME_LEN], species[GAME_NAME_LEN];
    uint8_t level;
    MonGender gender;
    char nature[GAME_NAME_LEN];
    int8_t nature_up, nature_down;
    uint8_t ivs[6], evs[6];
    char hp_type[GAME_NAME_LEN];
    uint8_t hp_power;
    uint8_t friendship;
    char ability[GAME_NAME_LEN], item[GAME_NAME_LEN];
    uint32_t exp_to_next;
    uint16_t hp, max_hp;
    uint8_t type_ids[2]; /* 0xFF when unknown */
} GamePartyMon;

typedef struct {
    char move[GAME_NAME_LEN];
    uint8_t quarters;
    bool status_move;
    uint8_t type; /* effective matchup type, 0xFF when the move id is out of range or unreadable */
    uint8_t dmg_kind;          /* DamageKind; DMG_NONE when no prediction (overworld, enemy 1, fallback) */
    bool dmg_no_ko, dmg_leaves_one;
    uint32_t dmg_lo, dmg_hi;   /* HP, non-critical hit */
} GameMoveEff;

typedef struct {
    char species[GAME_NAME_LEN];
    uint8_t level;
    MonGender gender;
    char nature[GAME_NAME_LEN];
    int8_t nature_up, nature_down;
    uint8_t ivs[6];
    uint16_t hp, max_hp;
    char ability[GAME_NAME_LEN], item[GAME_NAME_LEN];
    uint32_t status1;
    char type1[GAME_NAME_LEN], type2[GAME_NAME_LEN];
    uint8_t type_ids[2];
    uint8_t eff_count;
    GameMoveEff eff[4];      /* the player's active moves against this enemy */
    uint8_t move_count;
    GameMoveEff moves[4];    /* this enemy's moves against the player's active battler;
                                quarters is 0xFF when that battler is unknown */
    uint16_t player_hp, player_max_hp; /* the player's active battler, target of moves[] */
} GameEnemy;

typedef struct {
    char ball[GAME_NAME_LEN];
    uint16_t quantity;
    int16_t permille; /* -1 = not available */
} GameCatchRow;

typedef struct {
    char species[GAME_NAME_LEN];
    uint8_t min_level, max_level, percent;
    bool caught;
} GameEncRow;

typedef struct {
    bool valid;
    GameSupport support; /* OK unless the ROM itself is unsupported */
    char reason[48];
    GameContext context;
    char active_name[GAME_NAME_LEN]; /* player's active battler, empty when unknown */
    uint8_t party_count;
    GamePartyMon party[6];
    uint8_t enemy_count;
    GameEnemy enemies[2];
    uint8_t catch_count;
    GameCatchRow catch_rows[16];
    uint8_t map_group, map_num, map_type;
    char map_name[GAME_MAP_NAME_LEN]; /* region map section name, empty when unknown */
    bool has_encounters;
    uint8_t enc_count[ENC_METHOD_COUNT];
    GameEncRow enc[ENC_METHOD_COUNT][12];
    MirageInfo mirage;
    BerryView berries;
    uint8_t dex_caught[52]; /* bit (national-1): owned per FLAG_GET_CAUGHT, zero when !valid */
} GameSnapshot;

typedef struct {
    const GameProfile* profile;
    GameSupport support;
    GameSnapshot snap;
    MirageCache mirage;  /* owned Pokemon and last forecast, kept between snapshots */
    MirageClock clock;   /* set by game_update from the cartridge RTC */
    BerryPlaces places;  /* berry tree places, built from the ROM by game_init */
    SpeciesIndex search; /* species search index, built from the ROM by game_init */
    DamageTables dmg_tables; /* damage calculator ROM tables, copied by game_init */
} GameState;

/* Item name from the ROM's item table ("--" for 0, "???" when out of range). */
void game_item_name(const GameMem* m, const GameProfile* p, uint16_t item, char* out);

/* Pick the profile for this ROM (NULL when unsupported). */
void game_init(GameState* gs, const uint8_t* rom, uint32_t rom_size);
/* Rebuild gs->snap from memory. Pure reader: never writes emulated memory. */
void game_update_mem(GameState* gs, const GameMem* mem);
/* Builds a GameMem from gba->bus / gba->cart and calls game_update_mem. */
void game_update(GameState* gs, const GBA* gba);
/* ROM name lookups; species gives "???" and mapsec "" when out of range. */
/* Just the berry trees, for alerts while the panel is closed (much cheaper than game_update). */
void game_read_berries(GameState* gs, const GBA* gba, BerryView* out);
void game_species_name(const GameMem* m, const GameProfile* p, uint16_t species, char* out);
void game_nature_name(const GameMem* m, const GameProfile* p, uint8_t nature, char* out);
void game_mapsec_name(const GameMem* m, const GameProfile* p, uint8_t sec, char* out);
/* National dex number of a species, 0 when out of range or unreadable. */
uint16_t game_species_national(const GameMem* m, const GameProfile* p, uint16_t species);
/* Plain-text dump of a snapshot (used by --game-dump). */
bool game_dump(const GameSnapshot* snap, FILE* out);
/* "Grass", "Surf", "Rock Smash", "Old Rod", "Good Rod", "Super Rod" */
const char* game_method_name(EncMethod m);

#endif // GAME_H
