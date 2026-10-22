#ifndef GAME_PROFILE_H
#define GAME_PROFILE_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"

/* Why a ROM does or doesn't get game info. */
typedef enum {
    GAME_SUPPORT_OK,
    GAME_SUPPORT_OTHER_GAME,     /* no profile has this game code (or the header is short) */
    GAME_SUPPORT_MODIFIED,       /* known code and revision, other contents: a ROM hack or altered dump */
    GAME_SUPPORT_OTHER_REVISION, /* known game code, other version byte (0xBC), e.g. FireRed 1.1 */
} GameSupport;

/* ---- Per family: values every ROM of one engine shares (Emerald; FireRed + LeafGreen) ---- */

typedef enum { GAME_REGION_HOENN, GAME_REGION_KANTO } GameRegion;

/* GameRules.features: Companion parts that exist only in some games. */
enum {
    FEAT_BERRIES   = 1u << 0, /* Berries page and berry toasts */
    FEAT_HUNT      = 1u << 1, /* Hunt page */
    FEAT_MIRAGE    = 1u << 2, /* Mirage Island card, J key */
    FEAT_FEEBAS    = 1u << 3, /* Feebas tiles: Search row, egg guess index, X-Ray */
    FEAT_OVERWORLD = 1u << 4, /* Overworld X-Ray (F4), --overworld-out, --gift-audit */
    FEAT_FOLLOWER  = 1u << 5, /* Following Pokemon (F1), --follower-hgss */
};

/* A run of maps in one map group (data/maps/map_groups.json indexes). */
typedef struct { uint8_t group, first, last; } MapRange;

typedef enum { STORY_FLAG, STORY_VAR_AT_LEAST } StoryKind;
typedef enum { STORY_TEXT_PLAIN, STORY_TEXT_STARTER, STORY_TEXT_SPECIES, STORY_TEXT_CHAMPION } StoryText;
/* One Run page story beat. Its index in GameRules.story is the RUN_EV_STORY id in .run files. */
typedef struct {
    uint8_t kind;       /* StoryKind */
    uint16_t id;        /* flag id, or var id for STORY_VAR_AT_LEAST */
    uint16_t at_least;  /* STORY_VAR_AT_LEAST: done once the var reaches this */
    uint8_t text_kind;  /* StoryText: what fills the %s in text */
    uint16_t species;   /* STORY_TEXT_SPECIES */
    const char* text;   /* "Got Surf", "%s awakened", "Beat Champion %s" */
} RunStoryDef;

typedef struct {
    GameRegion region;
    uint32_t features;                 /* FEAT_* */

    /* struct SaveBlock1 (pret include/global.h) */
    uint16_t sb1_min_size;             /* nuzlocke_read's SaveBlock1 bounds check */
    uint16_t sb1_flags;                /* u8 flags[] */
    uint16_t sb1_flag_bytes;           /* NUM_FLAG_BYTES */
    uint16_t sb1_vars;                 /* u16 vars[], index id - 0x4000 */
    uint16_t sb1_game_stats;           /* u32 gameStats[], stored ^ SaveBlock2 encryptionKey */
    uint16_t sb1_ball_pocket;          /* bagPocket_PokeBalls {u16 id, u16 qty ^ key} */
    uint8_t ball_pocket_slots;
    uint16_t sb1_dex_seen1, sb1_dex_seen2; /* u8[52] each */
    uint16_t sb1_rival_name;           /* u8 rivalName[8]; 0 = the game has none */
    /* struct SaveBlock2 */
    uint16_t sb2_encryption_key;       /* u32 */
    /* struct DayCare, from GameProfile.daycare_sb1_offset */
    uint16_t daycare_offspring_off;
    uint8_t daycare_offspring_size;    /* 4; 2 on FRLG, where the upper PID half rolls at pickup */
    uint16_t daycare_step_counter_off; /* u8 stepCounter */
    bool daycare_fire_halves;          /* Flame Body / Magma Armor in the party halve egg cycles */
    /* struct PokedexEntry */
    uint8_t pokedex_entry_size;
    /* Region map section names: entry (sec - mapsec_first) * stride, name pointer at +off,
     * valid for mapsec_first <= sec < GameProfile.region_map_count. */
    uint8_t mapsec_name_stride, mapsec_name_off, mapsec_first;

    /* gBattleTypeFlags masks (include/constants/battle.h) */
    uint32_t badge_boost_off_mask;     /* ShouldGetStatBadgeBoost: no badge boosts */
    uint16_t secret_base_trainer;      /* no boosts against this trainer id either; 0 = no such rule */
    uint32_t soul_dew_off_mask;        /* Soul Dew does nothing */
    uint32_t run_facility_mask;        /* the player's party isn't the run's own (R6) */
    uint32_t run_not_plain_mask;       /* gTrainerBattleOpponent_A is not a gTrainers id */
    uint32_t partner_battle_mask;      /* in-game partner battle: party slots 3+ are the partner's */
    uint32_t nuz_ignore_mask;          /* battles that never use a Nuzlocke encounter */
    uint32_t uncatchable_mask;         /* balls always miss */
    uint32_t ghost_mask, ghost_unveiled_mask; /* the foe shows as GHOST until unveiled (R21) */
    const MapRange* facility_maps;     /* Run R6 gate outside battle */
    uint8_t facility_map_count;

    /* ids (include/constants/) */
    uint16_t flag_badge01;             /* FLAG_BADGE01_GET; badges 2-8 follow */
    uint16_t var_starter;              /* VAR_STARTER_MON */
    uint16_t var_altering_cave;        /* VAR_ALTERING_CAVE_WILD_SET */
    uint32_t partner_otid;             /* OT ID of in-game partner mons; 0 = none */
    uint16_t leaders[8];               /* gym leaders in badge order */
    uint16_t league[16];               /* Elite Four and Champion trainer ids */
    uint8_t league_count;
    uint16_t champion_trainer;         /* trainer named in the Champion story beat */
    uint8_t rival_classes[3];          /* trainer classes printed with the rival's name; 0 = unused */
    uint16_t starters[3];              /* species by VAR_STARTER_MON value */
    const char* badge_names[8];
    const RunStoryDef* story;          /* at most RUN_STORY_COUNT entries */
    uint8_t story_count;
    uint8_t story_hof;                 /* index of the Hall of Fame beat (time backfill) */

    /* Text and rules the panel shows */
    const char* daycare_toast;         /* egg toast detail when the egg can't be guessed */
    const char* route_footer;
    const char* relearn_hint;      /* Party page: who relearns moves, and for what */
    bool evo_day_night_beauty;         /* EVO_FRIENDSHIP_DAY/NIGHT and EVO_BEAUTY can happen */
    bool clock_tv_stats;               /* GAME_STAT_CHECKED_CLOCK / WATCHED_TV are counted */
} GameRules;

/* ---- Per ROM: every address the game layer needs. Nothing else in src/game/ hardcodes one. ---- */

typedef struct {
    const char* name;                /* for logs: "Pokemon Emerald (US)" */
    const char* game_name;           /* for UI text: "Pokémon Emerald" */
    const char* short_name;          /* "Emerald", for "in %s"; NULL in test fixtures */
    char game_code[4];               /* header bytes 0xAC-0xAF */
    uint8_t version;                 /* header byte 0xBC */
    uint32_t rom_size, rom_crc32;    /* the exact retail ROM the addresses were verified on */
    const GameRules* rules;          /* never NULL */

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
    uint32_t pokedex_entries;     uint16_t pokedex_count; /* rules->pokedex_entry_size stride, by national dex */

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
    uint32_t mon_still_front_pics;   /* CompressedSpriteSheet[] gMonStillFrontPicTable {ptr,u16 size,u16 tag} */
    uint32_t mon_palettes;           /* CompressedSpritePalette[] gMonPaletteTable {ptr,u16 tag,u16 pad} */
    uint32_t mon_shiny_palettes;     /* gMonShinyPaletteTable, same layout */
    uint32_t badge_gfx;              /* LZ77 128x16 4bpp badge sheet (trainer card); 0 = none */
    uint32_t experience_tables;                           /* u32[growth][101] */
    uint32_t species_to_national;                         /* u16, index species-1 */
    uint32_t wild_mon_headers;   uint16_t wild_header_cap; /* 20-byte stride */
    uint32_t region_map_entries; uint8_t region_map_count; /* section names (rules shape); count = MAPSEC_NONE */
    uint32_t evolution_table;                             /* 5 x 8-byte entries per species */
    uint32_t level_up_learnsets;                          /* gLevelUpLearnsets: u16* per species */
    uint32_t berries;                                     /* gBerries, 28-byte stride, index berry-1 */
    uint32_t starter_mons;                                /* sStarterMon, 3 x u16; 0 = randomizer unsupported */

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
    uint32_t approaching_trainers;  /* u8 gNoOfApproachingTrainers */
    uint32_t last_talked;           /* u16 gSpecialVar_LastTalked */
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
    uint16_t species_ditto, species_nidoran_f, species_nidoran_m;   /* include/constants/species.h */
    uint16_t species_volbeat, species_illumise;
    uint16_t pending_egg_flag;                   /* FLAG_PENDING_DAYCARE_EGG */

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

    /* FireRed / LeafGreen only; 0 on Emerald */
    uint32_t quest_log_state;      /* u8 gQuestLogState: 2 = "Previously on your quest" (R15) */
    uint32_t teachy_tv_restore_cb; /* gMain.savedCallback while Teachy TV swaps the party (R16) */
} GameProfile;

extern const GameRules g_emerald_rules;
extern const GameProfile g_emerald_profile;
extern const GameRules g_frlg_rules;
extern const GameProfile g_firered_profile, g_leafgreen_profile;
/* Every shipped profile, in detection order, and how many there are. */
extern const GameProfile* const g_game_profiles[];
extern const uint8_t g_game_profile_count;

static inline bool game_has(const GameProfile* p, uint32_t feat) {
    return p && (p->rules->features & feat) != 0;
}

/* Whether this ROM is exactly the one profile p was verified against. */
GameSupport game_profile_match(const GameProfile* p, const uint8_t* rom, uint32_t rom_size);
/* The shipped profile with this game code, or NULL. */
const GameProfile* game_profile_find(const char code[4]);
/* The profile in list whose game code the ROM has, when version and contents match too;
 * NULL otherwise, and *support says why. */
const GameProfile* game_profile_detect_in(const GameProfile* const* list, uint8_t count, const uint8_t* rom,
                                          uint32_t rom_size, GameSupport* support);
/* game_profile_detect_in over g_game_profiles. */
const GameProfile* game_profile_detect(const uint8_t* rom, uint32_t rom_size, GameSupport* support);
/* Standard CRC-32 (zlib, IEEE 802.3). _update continues a running value (start at 0), like zlib's crc32(). */
uint32_t game_crc32(const uint8_t* data, uint32_t len);
uint32_t game_crc32_update(uint32_t crc, const uint8_t* data, uint32_t len);

/* R15: FRLG's quest log replay writes a recorded scene's flags, vars and party into the live save.
 * Nothing in the save blocks is the player's while this is true. */
bool game_quest_log_playing(const GameProfile* p, const GameMem* m);

#endif // GAME_PROFILE_H
