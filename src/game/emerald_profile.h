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
    uint32_t trainer_battle_opponent_a; /* u16 gTrainerBattleOpponent_A */
    uint32_t battler_attacker;      /* u8 gBattlerAttacker */

    /* Damage calculator (docs/superpowers/specs/emerald-game-layer/damage-data.md) */
    uint32_t battle_weather;        /* u16 gBattleWeather */
    uint32_t side_statuses;         /* u16 gSideStatuses[2], by side */
    uint32_t statuses3;             /* u32 gStatuses3[4], by battler */
    uint32_t absent_battler_flags;  /* u8 gAbsentBattlerFlags, bit per battler */
    uint32_t disable_structs;       /* struct DisableStruct[4], 0x1C each */
    uint32_t battle_resources;      /* gBattleResources pointer; ->flags at +4, NULL outside battle */
    uint32_t stat_stage_ratios;                           /* u8[13][2] gStatStageRatios */
    uint32_t hold_effect_to_type; uint8_t hold_effect_to_type_count; /* u8 pairs sHoldEffectToType */
    uint32_t flail_table;                                 /* u8[6][2] sFlailHpScaleToPowerTable */
    uint32_t weight_table;                                /* u16[6][2] sWeightToDamageTable */
    uint32_t pokedex_entries;     uint16_t pokedex_count; /* 0x20-byte stride, by national dex */

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
    uint32_t berries;                                     /* gBerries, 28-byte stride, index berry-1 */

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
    uint32_t cb2_whiteout;          /* callback2 value while whiting out (Thumb bit set) */
    uint32_t palette_fade_byte;     /* gPaletteFade byte 7: bit 7 = fade active */
    uint32_t lock_field_controls;   /* u8 sLockFieldControls */
    uint32_t trainers;            uint16_t trainer_count;        /* 0x28-byte stride */
    uint32_t trainer_class_names; uint8_t trainer_class_count;   /* 13-byte stride */
    uint32_t map_groups;          uint8_t map_group_count;       /* pointer per group */

    /* Species search (pret: data/maps/map_groups.json, region_map_sections.json,
     * src/daycare.c, src/wild_encounter.c) */
    uint32_t wild_feebas;              /* sWildFeebas {u8 min, u8 max, u16 species} */
    uint8_t  feebas_map_group, feebas_map_num;   /* Route 119 */
    uint32_t feebas_water_tiles;       /* sRoute119WaterTileData u16[3][3] {yMin, yMax, spot base} */
    uint32_t tile_bit_attributes;      /* sTileBitAttributes u8[], indexed by metatile behavior */
    uint32_t tasks;                    /* gTasks, struct Task[16], 0x28 each: func @0, isActive @4 */
    uint32_t task_fishing;             /* Task_Fishing (Thumb bit set), func while a rod is out */
    uint8_t  mirage_map_group, mirage_map_num;   /* Route 130 land = Mirage Island */
    uint8_t  mirage_mapsec;                      /* MAPSEC_MIRAGE_ISLAND */
    uint8_t  dive_map[2][2];                     /* Underwater_Route124, Underwater_Route126 */
    uint8_t  dive_surface_mapsec[2];             /* MAPSEC_ROUTE_124, MAPSEC_ROUTE_126 */
    uint8_t  postgame_map[5][2];                 /* Safari NE/SE, Desert Underpass, Artisan B1F/1F */
    uint8_t  low_tide_mapsec;                    /* MAPSEC_SHOAL_CAVE */
    uint16_t incense_baby[2], incense_item[2];   /* AlterEggSpeciesWithIncenseItem */

    /* Following Pokemon */
    uint32_t player_avatar;        /* 0x02037590 struct PlayerAvatar */
    uint32_t camera;               /* 0x02037334 struct Camera {u8 active; s32 x @4; s32 y @8} */
    uint32_t palette_fade;         /* 0x02037FD4 struct PaletteFadeControl */
    uint32_t weather;              /* 0x02038454 struct Weather */
    uint32_t script_status;        /* 0x03000E38 u8 sGlobalScriptContextStatus */
    uint32_t main_new_keys;        /* 0x030022EE u16 gMain.newKeys */
    uint32_t transition_data;      /* 0x0203ABA0 sTransitionData pointer, non-zero during a battle transition */
    uint32_t elevation_to_priority;/* 0x0850E634 u8[16] */
    uint32_t tall_grass_pic;       /* 0x084F8738 5 frames of 16x16 4bpp */
    uint32_t cry_table;            /* 0x0869DCF4 ToneData[388] */
    uint32_t hoenn_cry_ids;        /* 0x0831F61C u16 sHoennSpeciesIdToCryId[135], index species-277 */
    uint32_t sprite_palette_tags;  /* 0x03000CF0 u16 sSpritePaletteTags[16], tag per OBJ palette slot, 0xFFFF free */

    /* Shiny Hunt (docs/superpowers/specs/2026-10-13-shiny-hunt-factcheck.md, Addresses table) */
    uint32_t rng_value;            /* u32 gRngValue */
    uint32_t rand_count;           /* u32 sRandCount, Random() calls since boot */
    uint32_t main_saved_callback;  /* gMain.savedCallback */
    uint32_t cb2_end_wild_battle;  /* savedCallback values during a battle (Thumb bit set) */
    uint32_t cb2_end_safari_battle;
    uint32_t cb2_end_scripted_wild_battle;
    uint32_t roamer_location;      /* u8[2] sRoamerLocation {group, num}, not saved */
    uint32_t player_avatar_flags;  /* u8 gPlayerAvatar.flags */
    uint16_t roamer_sb1_offset;    /* struct Roamer in SaveBlock1 */
    uint16_t outbreak_sb1_offset;  /* outbreakPokemonSpecies, start of the outbreak fields */
    uint16_t safari_flag;          /* FLAG_SYS_SAFARI_MODE */
    uint16_t repel_var;            /* VAR_REPEL_STEP_COUNT */
    uint8_t  altering_cave_map[2]; /* MAP_ALTERING_CAVE: wild header picked by a var */
    uint32_t prev_metatile_behavior; /* u16 sPrevMetatileBehavior, last step tile's behavior */
    uint32_t wild_immunity_steps;  /* u8 sWildEncounterImmunitySteps, 0-4 */
    /* Step scripts that run before the wild check (field_control_avatar.c TryStartStepCountScript) */
    uint16_t friendship_step_var;  /* VAR_FRIENDSHIP_STEP_COUNTER, 0-127 */
    uint16_t poison_step_var;      /* VAR_POISON_STEP_COUNTER, 0-3 */
    uint16_t match_call_flag;      /* FLAG_HAS_MATCH_CALL */
    uint16_t daycare_sb1_offset;   /* struct DayCare in SaveBlock1 */
    uint32_t match_call_state;     /* sMatchCallState: u32 minutes, u16 trainerId, u8 stepCounter */
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
