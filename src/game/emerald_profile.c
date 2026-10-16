#include "game/game_profile.h"

/* gBattleTypeFlags bits (pokeemerald include/constants/battle.h) */
#define BT_LINK             (1u << 1)
#define BT_TRAINER          (1u << 3)
#define BT_FIRST_BATTLE     (1u << 4)
#define BT_BATTLE_TOWER     (1u << 8)
#define BT_WALLY_TUTORIAL   (1u << 9)
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
#define BT_FRONTIER (BT_BATTLE_TOWER | BT_DOME | BT_PALACE | BT_ARENA | BT_FACTORY | BT_PIKE | BT_PYRAMID) /* 0x003F0100 */
#define TRAINER_FLAGS_START 0x500

/* Battle Tent, Battle Frontier facility and Trainer Hill maps, from pret
 * data/maps/map_groups.json (group index, map index). The Frontier's outside
 * areas, lounges, Center and Mart are left out: the player's own party is there. */
static const MapRange k_emerald_facility_maps[] = {
    {5, 1, 3},    /* FallarborTown_BattleTent Lobby, Corridor, BattleRoom */
    {6, 0, 2},    /* VerdanturfTown_BattleTent Lobby, Corridor, BattleRoom */
    {9, 2, 4},    /* SlateportCity_BattleTent Lobby, Corridor, BattleRoom */
    {26, 5, 8},   /* BattleFrontier_BattleTower Lobby, Elevator, Corridor, BattleRoom */
    {26, 15, 39}, /* BattleTower multi rooms, Dome, Palace, Pyramid, Arena, Factory, Pike */
    {26, 60, 65}, /* TrainerHill Entrance, 1F-4F, Roof */
    {26, 88, 88}, /* TrainerHill_Elevator */
};

/* Index = RunStory (runlog_read.h), the id .run files store. */
static const RunStoryDef k_emerald_story[] = {
    {STORY_FLAG, 0x860, 0, STORY_TEXT_STARTER, 0, "Chose %s"},                    /* FLAG_SYS_POKEMON_GET */
    {STORY_FLAG, 0x861, 0, STORY_TEXT_PLAIN, 0, "Got the Pok\xC3\xA9" "dex"},     /* FLAG_SYS_POKEDEX_GET */
    {STORY_FLAG, 0x8B, 0, STORY_TEXT_PLAIN, 0, "Stopped Team Magma at Mt. Chimney"}, /* FLAG_DEFEATED_EVIL_TEAM_MT_CHIMNEY */
    {STORY_VAR_AT_LEAST, 0x40B3, 1, STORY_TEXT_PLAIN, 0, "Saved the Weather Institute"}, /* VAR_WEATHER_INSTITUTE_STATE */
    {STORY_FLAG, 0x7A, 0, STORY_TEXT_PLAIN, 0, "Got Surf"},                       /* FLAG_RECEIVED_HM_SURF */
    {STORY_FLAG, 0x6F, 0, STORY_TEXT_SPECIES, 405, "%s awakened"},                /* FLAG_GROUDON_AWAKENED_MAGMA_HIDEOUT */
    {STORY_FLAG, 0x81, 0, STORY_TEXT_SPECIES, 404, "%s escaped"},                 /* FLAG_KYOGRE_ESCAPED_SEAFLOOR_CAVERN */
    {STORY_VAR_AT_LEAST, 0x40CA, 2, STORY_TEXT_PLAIN, 0, "Calmed Sootopolis"},    /* VAR_SKY_PILLAR_STATE */
    {STORY_FLAG, 0x107, 0, STORY_TEXT_PLAIN, 0, "Entered the Elite Four"},        /* FLAG_ENTERED_ELITE_FOUR */
    {STORY_FLAG, TRAINER_FLAGS_START + 335, 0, STORY_TEXT_CHAMPION, 0, "Beat Champion %s"}, /* TRAINER_WALLACE */
    {STORY_FLAG, 0x864, 0, STORY_TEXT_PLAIN, 0, "Entered the Hall of Fame"},      /* FLAG_SYS_GAME_CLEAR */
    {STORY_FLAG, 0x896, 0, STORY_TEXT_PLAIN, 0, "Got the National Dex"},          /* FLAG_SYS_NATIONAL_DEX */
};

/* Values the old emerald_layout.h macros and reader #defines held; Emerald output is unchanged. */
const GameRules g_emerald_rules = {
    .region = GAME_REGION_HOENN,
    .features = FEAT_BERRIES | FEAT_HUNT | FEAT_MIRAGE | FEAT_FEEBAS | FEAT_OVERWORLD | FEAT_FOLLOWER,
    .sb1_min_size = 0x3E08,
    .sb1_flags = 0x1270, .sb1_flag_bytes = 300,
    .sb1_vars = 0x139C,
    .sb1_game_stats = 0x159C,
    .sb1_ball_pocket = 0x650, .ball_pocket_slots = 16,
    .sb1_dex_seen1 = 0x988, .sb1_dex_seen2 = 0x3B24,
    .sb1_rival_name = 0,
    .sb2_encryption_key = 0xAC,
    .daycare_offspring_off = 0x118, .daycare_offspring_size = 4, .daycare_step_counter_off = 0x11C,
    .daycare_fire_halves = true,
    .pokedex_entry_size = 0x20,
    .mapsec_name_stride = 8, .mapsec_name_off = 4, .mapsec_first = 0, /* struct RegionMapLocation */
    .badge_boost_off_mask = BT_LINK | BT_EREADER_TRAINER | BT_RECORDED_LINK | BT_FRONTIER,
    .secret_base_trainer = 1024,                                       /* TRAINER_SECRET_BASE */
    .soul_dew_off_mask = BT_FRONTIER,
    .run_facility_mask = BT_LINK | BT_FRONTIER | BT_INGAME_PARTNER | BT_TOWER_LINK_MULTI | BT_RECORDED |
                         BT_RECORDED_LINK | BT_TRAINER_HILL,
    .run_not_plain_mask = BT_LINK | BT_FRONTIER | BT_TOWER_LINK_MULTI | BT_RECORDED | BT_RECORDED_LINK |
                          BT_TRAINER_HILL | BT_SECRET_BASE | BT_EREADER_TRAINER,
    .partner_battle_mask = BT_INGAME_PARTNER,
    .nuz_ignore_mask = BT_TRAINER | BT_FIRST_BATTLE | BT_WALLY_TUTORIAL,
    .uncatchable_mask = 0, .ghost_mask = 0, .ghost_unveiled_mask = 0,
    .facility_maps = k_emerald_facility_maps,
    .facility_map_count = (uint8_t)(sizeof(k_emerald_facility_maps) / sizeof(k_emerald_facility_maps[0])),
    .flag_badge01 = 0x867, .var_starter = 0x4023, .var_altering_cave = 0x403E,
    .partner_otid = 61226,  /* Steven's mons: pret pokeemerald 731ad5b src/battle_tower.c STEVEN_OTID */
    .leaders = {265, 266, 267, 268, 269, 270, 271, 272},              /* TRAINER_ROXANNE_1 .. TRAINER_JUAN_1 */
    .league = {261, 262, 263, 264, 335}, .league_count = 5,           /* Sidney .. Drake, Wallace */
    .champion_trainer = 335,
    .rival_classes = {0, 0, 0},
    .starters = {277, 280, 283},                                       /* SPECIES_TREECKO, TORCHIC, MUDKIP */
    .badge_names = {"Stone", "Knuckle", "Dynamo", "Heat", "Balance", "Feather", "Mind", "Rain"},
    .story = k_emerald_story,
    .story_count = (uint8_t)(sizeof(k_emerald_story) / sizeof(k_emerald_story[0])),
    .story_hof = 10,
    .daycare_toast = "Talk to the old man on Route 117",
    .route_footer = "\xE2\x9C\x93 caught. Not shown: outbreaks, roamers, lead abilities, Pyramid/Pike.",
    .evo_day_night_beauty = true,
    .clock_tv_stats = true,
};

/* Pokemon Emerald (US) rev 0, game code BPEE. Addresses come from pret/pokeemerald's
 * pokeemerald.sym (symbols branch). The ROM table addresses were checked against the
 * bytes of a BPEE rev 0 ROM; the party, map, wild battle and
 * enemy RAM addresses were checked live with --game-dump. */
const GameProfile g_emerald_profile = {
    .name = "Pokemon Emerald (US)",
    .game_name = "Pok\xC3\xA9mon Emerald",
    .short_name = "Emerald",
    /* "Pokemon - Emerald Version (USA, Europe)", the retail cartridge dump. Any ROM
     * hack changes the bytes, so it is rejected instead of being misread. */
    .game_code = {'B', 'P', 'E', 'E'},
    .version = 0,
    .rom_size = 0x1000000, .rom_crc32 = 0x1F1C08FB,
    .rules = &g_emerald_rules,

    .battle_type_flags = 0x02022FEC,
    .battlers_count = 0x0202406C,
    .battler_party_indexes = 0x0202406E,
    .battler_positions = 0x02024076,
    .battle_mons = 0x02024084,
    .player_party_count = 0x020244E9,
    .player_party = 0x020244EC,
    .enemy_party = 0x02024744,
    .main_in_battle = 0x030026F9,
    .battle_turn_counter = 0x03005D23,
    .save_block1_ptr = 0x03005D8C,
    .save_block2_ptr = 0x03005D90,
    .map_header = 0x02037318,
    .battle_outcome = 0x0202433A,
    .pokemon_storage_ptr = 0x03005D94,
    .trainer_battle_opponent_a = 0x02038BCA, /* gTrainerBattleOpponent_A */
    .battler_attacker = 0x0202420B,          /* gBattlerAttacker */

    /* Damage calculator: RAM from pokeemerald.sym, ROM tables checked against ROM bytes
     * (docs/superpowers/specs/emerald-game-layer/damage-data.md). */
    .battle_weather = 0x020243CC,
    .side_statuses = 0x0202428E,
    .statuses3 = 0x020242AC,
    .absent_battler_flags = 0x02024210,
    .disable_structs = 0x020242BC,
    .battle_resources = 0x020244A8,
    .stat_stage_ratios = 0x08329D2E,
    .hold_effect_to_type = 0x08329D74, .hold_effect_to_type_count = 17,
    .flail_table = 0x0831C408,
    .weight_table = 0x0831C428,
    .pokedex_entries = 0x0856B5B0, .pokedex_count = 387, /* sym size 0x3060 = 387 x 0x20 */

    .species_names = 0x083185C8, .species_count = 412,
    .move_names = 0x0831977C, .move_count = 355,
    .battle_moves = 0x0831C898,
    .ability_names = 0x0831B6DB, .ability_count = 78,
    .type_names = 0x0831AE38, .type_count = 18,
    .type_effectiveness = 0x0831ACE8, .type_effectiveness_len = 0x150,
    .items = 0x085839A0, .item_count = 377,
    .nature_name_ptrs = 0x0861CB50,
    .species_info = 0x083203CC,
    .mon_still_front_pics = 0x08301418, /* gMonStillFrontPicTable; gMonFrontPicTable 0x0830A18C has 2-frame sheets */
    .mon_palettes = 0x08303678,         /* gMonPaletteTable */
    .mon_shiny_palettes = 0x08304438,   /* gMonShinyPaletteTable, tag = species + 500 */
    .badge_gfx = 0x0856F5CC,            /* trainer card badges, pointer in LoadCardGfx at 0x080C2BEC */
    .experience_tables = 0x0831F72C,
    .species_to_national = 0x0831DC82,
    .wild_mon_headers = 0x08552D48, .wild_header_cap = 200,
    .region_map_entries = 0x085A147C, .region_map_count = 213,
    .evolution_table = 0x0832531C,
    .berries = 0x0858A670,

    .object_events = 0x02037350,
    .sprites = 0x02020630,
    .sprite_coord_offset = 0x02021BBC,
    .field_camera = 0x03005DD0,
    .camera_pan = 0x03000E28,
    .backup_map_layout = 0x03005DC0,
    .backup_map_data = 0x02032318,
    .main_callback2 = 0x030022C4,
    .cb2_overworld = 0x08085E5D,
    .cb2_whiteout = 0x08085F59,     /* CB2_WhiteOut 0x08085F58 | Thumb */
    .palette_fade_byte = 0x02037FDB,
    .lock_field_controls = 0x03000F2C,
    .trainers = 0x08310030, .trainer_count = 855,
    .trainer_class_names = 0x0830FCD4, .trainer_class_count = 66,
    .map_groups = 0x08486578, .map_group_count = 34,

    /* Species search: map ids from pret data/maps/map_groups.json, MAPSEC ids from
     * src/data/region_map/region_map_sections.json, sWildFeebas in src/wild_encounter.c,
     * incense pairs from src/daycare.c AlterEggSpeciesWithIncenseItem. */
    .wild_feebas = 0x08553A78,
    .feebas_map_group = 0, .feebas_map_num = 34,
    .feebas_water_tiles = 0x08553A7C,
    .tile_bit_attributes = 0x08486EFC,
    .tasks = 0x03005E00,
    .task_fishing = 0x0808C8C1,
    .mirage_map_group = 0, .mirage_map_num = 45,
    .mirage_mapsec = 71,
    .dive_map = {{0, 50}, {0, 51}},
    .dive_surface_mapsec = {39, 41},
    .postgame_map = {{26, 12}, {26, 13}, {24, 98}, {24, 99}, {24, 100}},
    .low_tide_mapsec = 67,
    .incense_baby = {350, 360}, .incense_item = {220, 221},
    .species_ditto = 132, .species_nidoran_f = 29, .species_nidoran_m = 32,
    .species_volbeat = 386, .species_illumise = 387,
    .pending_egg_flag = 0x86,

    .player_avatar = 0x02037590,
    .camera = 0x02037334,
    .palette_fade = 0x02037FD4,
    .weather = 0x02038454,
    .script_status = 0x03000E38,
    .main_new_keys = 0x030022EE,
    .transition_data = 0x0203ABA0,
    .elevation_to_priority = 0x0850E634,
    .tall_grass_pic = 0x084F8738,
    .cry_table = 0x0869DCF4,
    .hoenn_cry_ids = 0x0831F61C,
    .sprite_palette_tags = 0x03000CF0,

    /* Shiny Hunt: values from the factcheck Addresses table and review F2 (sym, ROM, live there).
     * Re-checked 2026-10-13 with a scratch probe running hunt_read at line 160 on a temp copy of
     * the test save, walking into the Route 101 grass (factcheck recipe). */
    .rng_value = 0x03005D80,          /* live: equals 0x41C64E6D/0x6073 LCG from 0 stepped
                                       * rand_count times on all 2500 frames from boot */
    .rand_count = 0x020249C0,         /* live: see rng_value */
    .main_saved_callback = 0x030022C8, /* gMain 0x030022C0 + 8 (include/main.h) */
    .cb2_end_wild_battle = 0x080B0AF9, /* sym 0x080B0AF8 | Thumb; live: set 95 frames before
                                        * inBattle, held on the edge and through the battle */
    .cb2_end_safari_battle = 0x080FC191,        /* sym 0x080FC190 | Thumb (not reached live) */
    .cb2_end_scripted_wild_battle = 0x080B0B6D, /* sym 0x080B0B6C | Thumb (not reached live) */
    .roamer_location = 0x0203BC86,    /* live: 0.33 (Route 118) */
    .player_avatar_flags = 0x02037590, /* gPlayerAvatar + 0 (global.fieldmap.h); live 0x01 on foot */
    .roamer_sb1_offset = 0x31DC,      /* live: Latios 408 lv 40, active */
    .outbreak_sb1_offset = 0x2B90,    /* live: species 0, no outbreak */
    .safari_flag = 0x88C,             /* SYSTEM_FLAGS 0x860 + 0x2C (flags.h); live: clear */
    .repel_var = 0x4021,              /* VAR_REPEL_STEP_COUNT (vars.h); live: 0 */
    .altering_cave_map = {24, 106},   /* data/maps/map_groups.json */
    .prev_metatile_behavior = 0x020375D6, /* sym; field_control_avatar.c:39 */
    .wild_immunity_steps = 0x020375D4,    /* sym; field_control_avatar.c:38 */
    /* Step-script counters, checked live 2026-10-13 with a scratch probe walking the Route 101
     * grass on a temp copy of the test save (one tick per step, Repel/poison gates at var 1/3). */
    .friendship_step_var = 0x402A, /* VAR_FRIENDSHIP_STEP_COUNTER (vars.h); live: +1 per step */
    .poison_step_var = 0x402B,     /* VAR_POISON_STEP_COUNTER (vars.h); live: 0-3 per step */
    .match_call_flag = 0x12F,      /* FLAG_HAS_MATCH_CALL (flags.h); live: set */
    .daycare_sb1_offset = 0x3030,  /* global.h SaveBlock1; next field at 0x3150 = size 0x120;
                                    * live: empty, stepCounter (+0x11C) +1 per step */
    .match_call_state = 0x0203CD80, /* sym (static); live: stepCounter +6 counts 0-9 per step */
};
