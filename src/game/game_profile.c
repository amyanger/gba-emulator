#include "game/game_profile.h"
#include <string.h>

#define HEADER_CODE    0xAC
#define HEADER_VERSION 0xBC
#define QUEST_LOG_PLAYBACK 2   /* QL_STATE_PLAYBACK (pfr include/quest_log.h) */

const GameProfile* const g_game_profiles[] = {&g_emerald_profile, &g_firered_profile,
                                              &g_leafgreen_profile};
const uint8_t g_game_profile_count = (uint8_t)(sizeof(g_game_profiles) / sizeof(g_game_profiles[0]));

uint32_t game_crc32_update(uint32_t crc, const uint8_t* data, uint32_t len) {
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
    crc ^= 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

uint32_t game_crc32(const uint8_t* data, uint32_t len) {
    return game_crc32_update(0, data, len);
}

GameSupport game_profile_match(const GameProfile* p, const uint8_t* rom, uint32_t rom_size) {
    if (!rom || rom_size <= HEADER_VERSION || memcmp(rom + HEADER_CODE, p->game_code, 4) != 0)
        return GAME_SUPPORT_OTHER_GAME;
    if (rom[HEADER_VERSION] != p->version) return GAME_SUPPORT_OTHER_REVISION;
    if (rom_size != p->rom_size || game_crc32(rom, rom_size) != p->rom_crc32)
        return GAME_SUPPORT_MODIFIED;
    return GAME_SUPPORT_OK;
}

const GameProfile* game_profile_find(const char code[4]) {
    for (uint8_t i = 0; i < g_game_profile_count; i++)
        if (memcmp(g_game_profiles[i]->game_code, code, 4) == 0) return g_game_profiles[i];
    return NULL;
}

const GameProfile* game_profile_detect_in(const GameProfile* const* list, uint8_t count, const uint8_t* rom,
                                          uint32_t rom_size, GameSupport* support) {
    *support = GAME_SUPPORT_OTHER_GAME;
    if (!rom || rom_size <= HEADER_VERSION) return NULL;
    for (uint8_t i = 0; i < count; i++) {
        if (memcmp(rom + HEADER_CODE, list[i]->game_code, 4) != 0) continue;
        *support = game_profile_match(list[i], rom, rom_size);   /* one CRC, for this profile only */
        return *support == GAME_SUPPORT_OK ? list[i] : NULL;
    }
    return NULL;
}

const GameProfile* game_profile_detect(const uint8_t* rom, uint32_t rom_size, GameSupport* support) {
    return game_profile_detect_in(g_game_profiles, g_game_profile_count, rom, rom_size, support);
}

bool game_quest_log_playing(const GameProfile* p, const GameMem* m) {
    uint8_t state;
    return p && p->quest_log_state && game_mem_read8(m, p->quest_log_state, &state) &&
           state == QUEST_LOG_PLAYBACK;
}
