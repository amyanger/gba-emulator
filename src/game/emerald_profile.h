#ifndef EMERALD_PROFILE_H
#define EMERALD_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

/* Every address the game layer needs, for one ROM. Nothing else in
 * src/game/ hardcodes an address, so supporting another build (e.g. a
 * pokeemerald-expansion hack) means adding a second profile. */
/* Why a ROM does or doesn't get game info. */
typedef enum {
    GAME_SUPPORT_OK,
    GAME_SUPPORT_OTHER_GAME, /* not Pokemon Emerald (US/English) */
    GAME_SUPPORT_MODIFIED,   /* Emerald header, other contents: a ROM hack or altered dump */
} GameSupport;

typedef struct {
    const char* name;
    char game_code[4];               /* header bytes 0xAC-0xAF */
    uint32_t rom_size, rom_crc32;    /* the exact retail ROM the addresses were verified on */

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
    uint32_t battle_outcome;        /* u8 gBattleOutcome, 0 while a battle runs */
    uint32_t pokemon_storage_ptr;   /* IWRAM pointer to struct PokemonStorage (PC boxes) */

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
    uint32_t region_map_entries; uint8_t region_map_count; /* 8-byte stride */
    uint32_t evolution_table;                             /* 5 x 8-byte entries per species */

    /* Overworld X-Ray (docs/superpowers/specs/emerald-game-layer/overworld-*.md) */
    uint32_t object_events;         /* struct ObjectEvent[16], 0x24 each */
    uint32_t sprites;               /* struct Sprite[65], 0x44 each */
    uint32_t sprite_coord_offset;   /* s16 gSpriteCoordOffsetX, then Y */
    uint32_t field_camera;          /* struct CameraObject: s32 x @0x10, y @0x14 */
    uint32_t camera_pan;            /* s16 sHorizontalCameraPan, then sVerticalCameraPan */
    uint32_t backup_map_layout;     /* s32 width, s32 height, u16* map */
    uint32_t backup_map_data;       /* the address backup_map_layout.map must hold */
    uint32_t main_callback2;        /* gMain.callback2 */
    uint32_t cb2_overworld;         /* callback2 value on the overworld (Thumb bit set) */
    uint32_t palette_fade_byte;     /* gPaletteFade byte 7: bit 7 = fade active */
    uint32_t lock_field_controls;   /* u8 sLockFieldControls */
    uint32_t trainers;            uint16_t trainer_count;        /* 0x28-byte stride */
    uint32_t trainer_class_names; uint8_t trainer_class_count;   /* 13-byte stride */
    uint32_t map_groups;          uint8_t map_group_count;       /* pointer per group */
} GameProfile;

extern const GameProfile g_emerald_profile;

/* Whether this ROM is exactly the one profile p was verified against. */
GameSupport game_profile_match(const GameProfile* p, const uint8_t* rom, uint32_t rom_size);
/* Returns the profile matching this ROM, or NULL when unsupported; *support says why. */
const GameProfile* game_profile_detect(const uint8_t* rom, uint32_t rom_size, GameSupport* support);
/* Standard CRC-32 (zlib, IEEE 802.3). _update continues a running value (start at 0), like zlib's crc32(). */
uint32_t game_crc32(const uint8_t* data, uint32_t len);
uint32_t game_crc32_update(uint32_t crc, const uint8_t* data, uint32_t len);

#endif // EMERALD_PROFILE_H
