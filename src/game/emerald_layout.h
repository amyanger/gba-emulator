#ifndef EMERALD_LAYOUT_H
#define EMERALD_LAYOUT_H

/* pret/pokeemerald struct offsets, sizes and game constants that more than one reader
 * needs. Offsets only: every address lives in emerald_profile.c. */

/* struct SaveBlock1 (include/global.h) */
#define SB1_LOC_GROUP 0x04   /* location.mapGroup */
#define SB1_LOC_NUM   0x05   /* location.mapNum */

/* struct Roamer (include/global.h), same layout in FireRed/LeafGreen */
#define ROAMER_IVS     0x00
#define ROAMER_SPECIES 0x08
#define ROAMER_HP      0x0A
#define ROAMER_LEVEL   0x0C
#define ROAMER_STATUS  0x0D
#define ROAMER_ACTIVE  0x13
#define ROAMER_SIZE    0x14

/* struct SaveBlock2 (include/global.h) */
#define SB2_TRAINER_ID        0x0A
#define SB2_DEX_OWNED         0x28 /* pokedex (+0x18) owned[52] (+0x10) */
#define SB2_DEX_SEEN          0x5C /* pokedex seen[52] (+0x44) */
#define SB2_LOCAL_TIME_OFFSET 0x98 /* struct Time: s16 days, s8 hours, minutes, seconds */

/* struct BattlePokemon (include/pokemon.h) */
#define BMON_SPECIES     0x00
#define BMON_ATTACK      0x02
#define BMON_DEFENSE     0x04
#define BMON_SPEED       0x06
#define BMON_SP_ATTACK   0x08
#define BMON_SP_DEFENSE  0x0A
#define BMON_MOVES       0x0C
#define BMON_IVS         0x14
#define BMON_STAGES      0x18
#define BMON_ABILITY     0x20
#define BMON_TYPE1       0x21
#define BMON_TYPE2       0x22
#define BMON_HP          0x28
#define BMON_LEVEL       0x2A
#define BMON_FRIENDSHIP  0x2B
#define BMON_MAX_HP      0x2C
#define BMON_ITEM        0x2E
#define BMON_PERSONALITY 0x48
#define BMON_STATUS1     0x4C
#define BMON_STATUS2     0x50
#define BMON_SIZE        0x58

/* struct SpeciesInfo (include/pokemon.h) */
#define SI_TYPE1          0x06
#define SI_TYPE2          0x07
#define SI_CATCH_RATE     0x08
#define SI_GENDER_RATIO   0x10
#define SI_GROWTH_RATE    0x13
#define SI_EGG_GROUP1     0x14
#define SI_EGG_GROUP2     0x15
#define SI_ABILITY1       0x16
#define SI_ABILITY2       0x17
#define SPECIES_INFO_SIZE 28

/* struct MapHeader, MapEvents, MapLayout, the map grid (include/global.fieldmap.h, fieldmap.c) */
#define MAP_HEADER_LAYOUT         0x00
#define MAP_HEADER_EVENTS         0x04
#define MAP_HEADER_REGION_MAP_SEC 0x14
#define EVENTS_OBJ_COUNT          0x00
#define EVENTS_OBJS               0x04
#define TEMPLATE_SIZE             0x18 /* struct ObjectEventTemplate */
#define LAYOUT_WIDTH              0x00
#define LAYOUT_HEIGHT             0x04
#define LAYOUT_PRIMARY            0x10
#define LAYOUT_SECONDARY          0x14
#define TILESET_ATTRS             0x10
#define PRIMARY_METATILES         512
#define MAP_OFFSET                7    /* VMap border around the map */
#define VMAP_MAX_CELLS            10240
#define GRID_UNDEFINED            0x03FF
#define GRID_COLLISION            0x0C00
#define TILE_FLAG_SURFABLE        0x02
#define MB_TALL_GRASS             0x02
#define MB_LONG_GRASS             0x03

/* struct ObjectEvent and gObjectEvents (include/global.fieldmap.h) */
#define OBJ_ELEVATION  0x0B /* currentElevation:4, previousElevation:4 */
#define OBJ_CUR        0x10 /* s16 x, y in VMap coords */
#define OBJ_PREV       0x14
#define OBJ_DIRS       0x18 /* low nibble: facingDirection 1..4 */
#define OBJ_SIZE       0x24
#define OBJ_COUNT      16
#define AVATAR_SURFING 0x08 /* gPlayerAvatar.flags */

/* Other struct sizes (pokemon.h, item.h, data.h, wild_encounter.h, pokemon_storage_system.h) */
#define BOX_MON_SIZE     0x50 /* struct BoxPokemon, the start of a DaycareMon */
#define ITEM_SIZE        44   /* struct Item: name[14] first */
#define BATTLE_MOVE_SIZE 12
#define TRAINER_SIZE     0x28
#define WILD_HEADER_SIZE 20
#define EVO_ENTRY_SIZE   8
#define EVOS_PER_MON     5
#define STORAGE_BOXES    4    /* boxes[14][30] after the u8 currentBox and padding */

/* Game constants (include/constants/) */
#define FLAG_SYS_CLOCK_SET   0x895 /* Emerald only: berry.c and mirage.c run behind FEAT_BERRIES / FEAT_MIRAGE */
#define BATTLE_TYPE_TRAINER  (1u << 3)
#define BATTLE_TYPE_SAFARI   (1u << 7)
#define STATUS2_FORESIGHT    (1u << 29)
#define TYPE_ELECTRIC        13
#define ABILITY_HUSTLE       55
#define ABILITY_PLUS         57
#define ABILITY_MINUS        58

#endif // EMERALD_LAYOUT_H
