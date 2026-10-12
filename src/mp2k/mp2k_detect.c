#include "mp2k/mp2k_detect.h"
#include "game/emerald_profile.h"

#define POOL_INFO 0x6Cu
#define POOL_ID   0x70u
#define POOL_RAM  0x74u
#define SIG_LEN   48u

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool in_iwram(uint32_t a) { return a >= 0x03000000u && a < 0x03008000u; }

bool mp2k_detect_crc(const uint8_t* rom, uint32_t size, uint32_t crc, Mp2kDriver* out) {
    if (!rom || size < POOL_RAM + 4u) return false;
    /* The pool is word aligned; look for ID_NUMBER first, then check the code. */
    for (uint32_t id = POOL_ID; id + 8u <= size; id += 4u) {
        if (rd32(rom + id) != MP2K_ID_NUMBER) continue;
        uint32_t sm = id - POOL_ID;
        uint32_t info = rd32(rom + sm + POOL_INFO);
        uint32_t ram = rd32(rom + sm + POOL_RAM);
        if (!in_iwram(info) || !in_iwram(ram) || !(ram & 1u)) continue;
        if (game_crc32(rom + sm, SIG_LEN) != crc) continue;
        out->sound_main = 0x08000000u + sm;
        out->sound_info_ptr = info;
        out->hook_addr = ram & ~1u;
        return true;
    }
    return false;
}

bool mp2k_detect(const uint8_t* rom, uint32_t size, Mp2kDriver* out) {
    return mp2k_detect_crc(rom, size, MP2K_SOUNDMAIN_CRC, out);
}
