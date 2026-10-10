#include "game/emerald_profile.h"
#include <string.h>

/* Pokemon Emerald (US) rev 0, game code BPEE. Addresses come from pret/pokeemerald's
 * pokeemerald.sym (symbols branch). The ROM table addresses were checked against the
 * bytes of a BPEE rev 0 ROM; the party, map, wild battle and
 * enemy RAM addresses were checked live with --game-dump. */
const GameProfile g_emerald_profile = {
    .name = "Pokemon Emerald (US)",
    /* "Pokemon - Emerald Version (USA, Europe)", the retail cartridge dump. Any ROM
     * hack changes the bytes, so it is rejected instead of being misread. */
    .game_code = {'B', 'P', 'E', 'E'},
    .rom_size = 0x1000000, .rom_crc32 = 0x1F1C08FB,

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

    .species_names = 0x083185C8, .species_count = 412,
    .move_names = 0x0831977C, .move_count = 355,
    .battle_moves = 0x0831C898,
    .ability_names = 0x0831B6DB, .ability_count = 78,
    .type_names = 0x0831AE38, .type_count = 18,
    .type_effectiveness = 0x0831ACE8, .type_effectiveness_len = 0x150,
    .items = 0x085839A0, .item_count = 377,
    .nature_name_ptrs = 0x0861CB50,
    .species_info = 0x083203CC,
    .experience_tables = 0x0831F72C,
    .species_to_national = 0x0831DC82,
    .wild_mon_headers = 0x08552D48, .wild_header_cap = 200,
    .region_map_entries = 0x085A147C, .region_map_count = 213,

    .object_events = 0x02037350,
    .sprites = 0x02020630,
    .sprite_coord_offset = 0x02021BBC,
    .field_camera = 0x03005DD0,
    .camera_pan = 0x03000E28,
    .backup_map_layout = 0x03005DC0,
    .backup_map_data = 0x02032318,
    .main_callback2 = 0x030022C4,
    .cb2_overworld = 0x08085E5D,
    .palette_fade_byte = 0x02037FDB,
    .lock_field_controls = 0x03000F2C,
    .trainers = 0x08310030, .trainer_count = 855,
    .trainer_class_names = 0x0830FCD4, .trainer_class_count = 66,
    .map_groups = 0x08486578, .map_group_count = 34,
};

uint32_t game_crc32(const uint8_t* data, uint32_t len) {
    static uint32_t table[256];
    static bool ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

GameSupport game_profile_match(const GameProfile* p, const uint8_t* rom, uint32_t rom_size) {
    if (!rom || rom_size <= 0xAF || memcmp(rom + 0xAC, p->game_code, 4) != 0)
        return GAME_SUPPORT_OTHER_GAME;
    if (rom_size != p->rom_size || game_crc32(rom, rom_size) != p->rom_crc32)
        return GAME_SUPPORT_MODIFIED;
    return GAME_SUPPORT_OK;
}

const GameProfile* game_profile_detect(const uint8_t* rom, uint32_t rom_size, GameSupport* support) {
    *support = game_profile_match(&g_emerald_profile, rom, rom_size);
    return *support == GAME_SUPPORT_OK ? &g_emerald_profile : NULL;
}
