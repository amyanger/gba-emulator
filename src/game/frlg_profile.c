#include "game/game_profile.h"

/* Pokemon FireRed (US) rev 0 (BPRE) and LeafGreen (US) rev 1 (BPGE). Addresses from
 * pret/pokefirered symbols 3b401ea (pokefirered.sym, pokeleafgreen_rev1.sym), struct offsets and
 * ids from pret master 037335f. docs/superpowers/specs/frlg-game-layer/addresses.md has each
 * value with its check: V = ROM bytes, S = sym/source only, LIVE = seen in a FireRed/LeafGreen run.
 * Every RAM address is the same in both ROMs (894 shared RAM symbols, 0 differ). */

/* gBattleTypeFlags bits (pfr include/constants/battle.h) */
#define BT_LINK             (1u << 1)
#define BT_TRAINER          (1u << 3)   /* LIVE: rival 0x18 */
#define BT_FIRST_BATTLE     (1u << 4)   /* LIVE: rival 0x18 */
#define BT_BATTLE_TOWER     (1u << 8)   /* S: Soul Dew gate, pfr pokemon.c:2466-2468 */
#define BT_OLD_MAN_TUTORIAL (1u << 9)   /* S: pfr battle.h:56 */
#define BT_EREADER_TRAINER  (1u << 11)  /* V: 0x802 in CalculateBaseDamage's pool */
#define BT_GHOST_UNVEILED   (1u << 13)  /* S: with GHOST, the Silph Scope Marowak */
#define BT_GHOST            (1u << 15)  /* S: pfr battle.h:64 */
#define BT_POKEDUDE         (1u << 16)  /* LIVE: 0x10000 in Teachy TV */
#define BT_TRAINER_TOWER    (1u << 19)  /* S: pfr battle.h:67 */
#define TRAINER_FLAGS_START 0x500       /* S: flags.h:1319 */

/* Index = RUN_EV_STORY id in FireRed/LeafGreen .run files. Flags confirmed on ROM bytes (the
 * setting script's setflag) unless noted. */
static const RunStoryDef k_frlg_story[] = {
    /* FLAG_SYS_POKEMON_GET, V+LIVE */
    {STORY_FLAG, 0x828, 0, STORY_TEXT_STARTER, 0, "Chose %s"},
    /* FLAG_SYS_POKEDEX_GET, V */
    {STORY_FLAG, 0x829, 0, STORY_TEXT_PLAIN, 0, "Got the Pok\xC3\xA9" "dex"},
    /* FLAG_GOT_SS_TICKET, V */
    {STORY_FLAG, 0x234, 0, STORY_TEXT_PLAIN, 0, "Got the S.S. Ticket from Bill"},
    /* TRAINER_BOSS_GIOVANNI, id V */
    {STORY_FLAG, TRAINER_FLAGS_START + 348, 0, STORY_TEXT_PLAIN, 0,
     "Beat Giovanni in the Rocket Hideout"},
    /* FLAG_RESCUED_MR_FUJI, V */
    {STORY_FLAG, 0x23C, 0, STORY_TEXT_PLAIN, 0, "Rescued Mr. Fuji"},
    /* FLAG_GOT_HM03, V */
    {STORY_FLAG, 0x239, 0, STORY_TEXT_PLAIN, 0, "Got Surf"},
    /* Giovanni, Silph Co. 11F, id V */
    {STORY_FLAG, TRAINER_FLAGS_START + 349, 0, STORY_TEXT_PLAIN, 0, "Freed Silph Co."},
    /* FLAG_WORLD_MAP_INDIGO_PLATEAU_EXTERIOR, V */
    {STORY_FLAG, 0x899, 0, STORY_TEXT_PLAIN, 0, "Reached Indigo Plateau"},
    /* FLAG_SYS_GAME_CLEAR (R10), V */
    {STORY_FLAG, 0x82C, 0, STORY_TEXT_CHAMPION, 0, "Beat Champion %s"},
    /* FLAG_SYS_GAME_CLEAR, V */
    {STORY_FLAG, 0x82C, 0, STORY_TEXT_PLAIN, 0, "Entered the Hall of Fame"},
    /* FLAG_SYS_NATIONAL_DEX, S+V */
    {STORY_FLAG, 0x840, 0, STORY_TEXT_PLAIN, 0, "Got the National Dex"},
    /* FLAG_RECOVERED_SAPPHIRE, V */
    {STORY_FLAG, 0x2DC, 0, STORY_TEXT_PLAIN, 0, "Recovered the Sapphire"},
};

const GameRules g_frlg_rules = {
    .region = GAME_REGION_KANTO,
    .features = 0,
    .sb1_min_size = 0x3D68,                             /* sizeof(SaveBlock1), V (GF header) */
    /* V+LIVE; NUM_FLAG_BYTES S (flags.h:1526) */
    .sb1_flags = 0xEE0, .sb1_flag_bytes = 0x120,
    .sb1_vars = 0x1000,                                       /* V+LIVE */
    .sb1_game_stats = 0x1200,                                 /* LIVE */
    .sb1_ball_pocket = 0x430, .ball_pocket_slots = 13,        /* V+LIVE */
    .sb1_dex_seen1 = 0x5F8, .sb1_dex_seen2 = 0x3A18,          /* V+LIVE */
    .sb1_rival_name = 0x3A4C,                                 /* S: spec R11, pfr global.h:813 */
    .sb2_encryption_key = 0xF20,                              /* LIVE */
    .daycare_offspring_off = 0x118, .daycare_offspring_size = 2, /* u16, LIVE+S */
    .daycare_step_counter_off = 0x11A,                        /* LIVE */
    .daycare_fire_halves = false,                             /* S: pfr daycare.c:1157-1180 */
    .pokedex_entry_size = 0x24,                               /* V */
    /* sMapNames[sec - 0x58], V */
    .mapsec_name_stride = 4, .mapsec_name_off = 0, .mapsec_first = 0x58,
    .badge_boost_off_mask = BT_LINK | BT_EREADER_TRAINER,     /* 0x802, V */
    .secret_base_trainer = 0,
    .soul_dew_off_mask = BT_BATTLE_TOWER,
    .run_facility_mask = BT_LINK | BT_OLD_MAN_TUTORIAL | BT_POKEDUDE,
    .run_not_plain_mask = BT_LINK | BT_EREADER_TRAINER | BT_TRAINER_TOWER,
    .partner_battle_mask = 0,
    .nuz_ignore_mask = BT_TRAINER | BT_FIRST_BATTLE | BT_OLD_MAN_TUTORIAL | BT_GHOST | BT_POKEDUDE,
    .uncatchable_mask = BT_GHOST,                             /* S: Cmd_handleballthrow */
    .ghost_mask = BT_GHOST, .ghost_unveiled_mask = BT_GHOST_UNVEILED,
    .facility_maps = NULL, .facility_map_count = 0,
    .flag_badge01 = 0x820,                                    /* V+LIVE */
    .var_starter = 0x4031,                    /* V+LIVE: 0 Bulbasaur, 1 Squirtle, 2 Charmander */
    .var_altering_cave = 0x4024,                              /* S: vars.h:71 */
    .partner_otid = 0,
    /* Brock..Sabrina, Blaine, Giovanni; S+V */
    .leaders = {414, 415, 416, 417, 418, 420, 419, 350},
    /* E4, rematch E4, Champions; V */
    .league = {410, 411, 412, 413, 735, 736, 737, 738, 438, 439, 440, 739, 740, 741},
    .league_count = 14,
    .champion_trainer = 438,           /* TRAINER_CHAMPION_FIRST_SQUIRTLE, class 90; V */
    .rival_classes = {81, 89, 90},     /* RIVAL_EARLY, RIVAL_LATE (S, R11), CHAMPION (V) */
    .starters = {1, 7, 4},             /* Bulbasaur, Squirtle, Charmander by var value */
    .badge_names = {"Boulder", "Cascade", "Thunder", "Rainbow", "Soul", "Marsh", "Volcano",
                    "Earth"},
    .story = k_frlg_story,
    .story_count = (uint8_t)(sizeof(k_frlg_story) / sizeof(k_frlg_story[0])),
    .story_hof = 9,
    .daycare_toast = "Talk to the old man on Four Island",
    .route_footer = "\xE2\x9C\x93 caught. Not shown: lead abilities.",
    .relearn_hint = "For a Big Mushroom or 2 Tiny Mushrooms, Move Maniac on Two Island",
    .evo_day_night_beauty = false,           /* S: pfr pokemon.c:5062-5075; no Pokeblocks */
    .clock_tv_stats = false,                                  /* S: never incremented in pfr */
};

/* RAM and family values, identical in both ROMs (addresses.md "GameProfile: RAM", all S unless
 * noted). Emerald-only map fields are 0xFF = none, since map 0:0 and mapsec 0 are real in FRLG. */
#define FRLG_RAM                                                                    \
    .rules = &g_frlg_rules,                                                         \
    .battle_type_flags = 0x02022B4C,                              /* LIVE */        \
    .battlers_count = 0x02023BCC, .battler_party_indexes = 0x02023BCE,              \
    .battler_positions = 0x02023BD6, .battle_mons = 0x02023BE4,                     \
    .player_party_count = 0x02024029, .player_party = 0x02024284, /* LIVE */        \
    .enemy_party = 0x0202402C,                                    /* LIVE */        \
    .main_in_battle = 0x03003529,                                 /* LIVE */        \
    .battle_turn_counter = 0x03004FA3,                                              \
    .save_block1_ptr = 0x03005008, .save_block2_ptr = 0x0300500C, /* LIVE */        \
    .map_header = 0x02036DFC,                                     /* LIVE */        \
    .roamer_location = 0x0203F3AE, .roamer_sb1_offset = 0x30D0,   /* LIVE FireRed */ \
    .battle_outcome = 0x02023E8A,                                 /* LIVE */        \
    .pokemon_storage_ptr = 0x03005010,                            /* LIVE */        \
    .trainer_battle_opponent_a = 0x020386AE,                      /* LIVE */        \
    .battler_attacker = 0x02023D6B,                                                 \
    .battle_weather = 0x02023F1C, .side_statuses = 0x02023DDE,                      \
    .statuses3 = 0x02023DFC,                                                        \
    .absent_battler_flags = 0x02023D70, .disable_structs = 0x02023E0C,              \
    .battle_resources = 0x02023FF4,                                                 \
    .main_callback2 = 0x030030F4, .main_saved_callback = 0x030030F8, /* LIVE */     \
    .quest_log_state = 0x0203ADFA,                 /* gQuestLogState, S (R15) */    \
    .pending_egg_flag = 0x266,                                    /* V */           \
    .daycare_sb1_offset = 0x2F80,                                 /* LIVE+S */      \
    .incense_baby = {350, 360}, .incense_item = {220, 221},                         \
    .species_ditto = 132, .species_nidoran_f = 29, .species_nidoran_m = 32,         \
    .species_volbeat = 386, .species_illumise = 387,                                \
    .altering_cave_map = {1, 122},                                                  \
    .feebas_map_group = 0xFF, .feebas_map_num = 0xFF, .wild_feebas = 0,             \
    .mirage_map_group = 0xFF, .mirage_map_num = 0xFF, .mirage_mapsec = 0xFF,        \
    .dive_map = {{0xFF, 0xFF}, {0xFF, 0xFF}}, .dive_surface_mapsec = {0xFF, 0xFF},  \
    .postgame_map = {{0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF},        \
                     {0xFF, 0xFF}},                                                 \
    .low_tide_mapsec = 0xFF

/* ROM tables: addresses.md "GameProfile: ROM tables", all V. */
const GameProfile g_firered_profile = {
    .name = "Pokemon FireRed (US)",
    .game_name = "Pok\xC3\xA9mon FireRed",
    .short_name = "FireRed",
    .game_code = {'B', 'P', 'R', 'E'}, .version = 0,
    .rom_size = 0x1000000, .rom_crc32 = 0xDD88761C,
    FRLG_RAM,
    .species_names = 0x08245EE0, .species_count = 412,
    .move_names = 0x08247094, .move_count = 355,
    .battle_moves = 0x08250C04,
    .ability_names = 0x0824FC40, .ability_count = 78,
    .type_names = 0x0824F1A0, .type_count = 18,
    .type_effectiveness = 0x0824F050, .type_effectiveness_len = 0x150,
    .items = 0x083DB028, .item_count = 375,
    .nature_name_ptrs = 0x08463E60,
    .species_info = 0x08254784,
    .mon_still_front_pics = 0x082350AC, /* gMonFrontPicTable: FRLG fronts are one frame */
    .mon_palettes = 0x0823730C,
    .mon_shiny_palettes = 0x082380CC,
    .badge_gfx = 0x083CD5E8, /* trainer card badges, pointer in LoadCardGfx at 0x08089530 */
    .experience_tables = 0x08253AE4,
    .species_to_national = 0x08251FEE,
    .wild_mon_headers = 0x083C9CB8, .wild_header_cap = 133,
    .region_map_entries = 0x083F1CAC, .region_map_count = 0xC5, /* sMapNames, MAPSEC_NONE */
    .evolution_table = 0x08259754,
    .level_up_learnsets = 0x0825D7B4,
    .trainers = 0x0823EAC8, .trainer_count = 743,
    .trainer_class_names = 0x0823E558, .trainer_class_count = 107,
    .map_groups = 0x083526A8, .map_group_count = 43,
    .pokedex_entries = 0x0844E850, .pokedex_count = 387,
    .stat_stage_ratios = 0x0825DEAD,
    .hold_effect_to_type = 0x0825DECC, .hold_effect_to_type_count = 17,
    .flail_table = 0x08250810,
    .weight_table = 0x08250830,
    .cb2_overworld = 0x080565B5, .cb2_whiteout = 0x080566A5,
    .teachy_tv_restore_cb = 0x0815BD59,     /* TeachyTvRestorePlayerPartyCallback | 1, LIVE */
};

const GameProfile g_leafgreen_profile = {
    .name = "Pokemon LeafGreen (US) rev 1",
    .game_name = "Pok\xC3\xA9mon LeafGreen",
    .short_name = "LeafGreen",
    .game_code = {'B', 'P', 'G', 'E'}, .version = 1,
    .rom_size = 0x1000000, .rom_crc32 = 0xDAFFECEC,
    FRLG_RAM,
    .species_names = 0x08245F2C, .species_count = 412,
    .move_names = 0x082470E0, .move_count = 355,
    .battle_moves = 0x08250C50,
    .ability_names = 0x0824FC8C, .ability_count = 78,
    .type_names = 0x0824F1EC, .type_count = 18,
    .type_effectiveness = 0x0824F09C, .type_effectiveness_len = 0x150,
    .items = 0x083DAED4, .item_count = 375,
    .nature_name_ptrs = 0x084638F0,
    .species_info = 0x082547D0,
    .mon_still_front_pics = 0x082350F8,
    .mon_palettes = 0x08237358,
    .mon_shiny_palettes = 0x08238118,
    .badge_gfx = 0x083CD494, /* trainer card badges, pointer in LoadCardGfx at 0x08089518 */
    .experience_tables = 0x08253B30,
    .species_to_national = 0x0825203A,
    .wild_mon_headers = 0x083C9B64, .wild_header_cap = 133,
    .region_map_entries = 0x083F1B58, .region_map_count = 0xC5,
    .evolution_table = 0x082597A4,
    .level_up_learnsets = 0x0825D804,
    .trainers = 0x0823EB14, .trainer_count = 743,
    .trainer_class_names = 0x0823E5A4, .trainer_class_count = 107,
    .map_groups = 0x083526F8, .map_group_count = 43,
    .pokedex_entries = 0x0844E2E0, .pokedex_count = 387,
    .stat_stage_ratios = 0x0825DEFD,
    .hold_effect_to_type = 0x0825DF1C, .hold_effect_to_type_count = 17,
    .flail_table = 0x0825085C,
    .weight_table = 0x0825087C,
    .cb2_overworld = 0x080565C9, .cb2_whiteout = 0x080566B9,
    .teachy_tv_restore_cb = 0x0815BDAD,     /* TeachyTvRestorePlayerPartyCallback | 1, LIVE */
};
