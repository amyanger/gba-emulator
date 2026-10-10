#ifndef EMERALD_PROFILE_H
#define EMERALD_PROFILE_H

#include <stdint.h>

/* Every address the game layer needs, for one ROM. Nothing else in
 * src/game/ hardcodes an address, so supporting another build (e.g. a
 * pokeemerald-expansion hack) means adding a second profile. */
typedef struct {
    const char* name;

    /* RAM */
    uint32_t battle_type_flags;     /* u32 gBattleTypeFlags */
    uint32_t battlers_count;        /* u8 gBattlersCount */
    uint32_t battler_party_indexes; /* u16[4] */
    uint32_t battler_positions;     /* u8[4] */
    uint32_t battle_mons;           /* BattlePokemon[4], 0x58 each */
    uint32_t player_party_count;    /* u8 */
    uint32_t player_party;          /* Pokemon[6], 100 each */
    uint32_t enemy_party;
    uint32_t main_in_battle;        /* byte of gMain holding inBattle (bit 1) */
    uint32_t battle_turn_counter;   /* u8 gBattleResults.battleTurnCounter */
    uint32_t save_block1_ptr;       /* IWRAM pointers; blocks move at runtime */
    uint32_t save_block2_ptr;
    uint32_t map_header;            /* struct MapHeader gMapHeader */

    /* ROM tables */
    uint32_t species_names;      uint16_t species_count;  /* 11-byte stride */
    uint32_t move_names;         uint16_t move_count;     /* 13-byte stride */
    uint32_t battle_moves;                                /* 12-byte stride */
    uint32_t ability_names;      uint16_t ability_count;  /* 13-byte stride */
    uint32_t type_names;         uint8_t type_count;      /* 7-byte stride */
    uint32_t type_effectiveness; uint16_t type_effectiveness_len; /* bytes */
    uint32_t items;              uint16_t item_count;     /* 44-byte stride */
    uint32_t nature_name_ptrs;                            /* 25 pointers */
    uint32_t species_info;                                /* 28-byte stride */
    uint32_t experience_tables;                           /* u32[growth][101] */
    uint32_t species_to_national;                         /* u16, index species-1 */
    uint32_t wild_mon_headers;   uint16_t wild_header_cap; /* 20-byte stride */
} GameProfile;

extern const GameProfile g_emerald_profile;

/* Returns the profile matching this ROM, or NULL when unsupported. */
const GameProfile* game_profile_detect(const uint8_t* rom, uint32_t rom_size);

#endif // EMERALD_PROFILE_H
