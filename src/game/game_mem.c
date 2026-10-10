#include "game/game_mem.h"
#include <string.h>

#define GM_EWRAM_BASE 0x02000000u
#define GM_EWRAM_SIZE 0x00040000u
#define GM_IWRAM_BASE 0x03000000u
#define GM_IWRAM_SIZE 0x00008000u
#define GM_ROM_BASE   0x08000000u

static const uint8_t* game_mem_span(const GameMem* mem, uint32_t addr, uint32_t len) {
    const uint8_t* base;
    uint32_t start, size;
    if (len == 0) return NULL;
    if (addr >= GM_EWRAM_BASE && addr - GM_EWRAM_BASE < GM_EWRAM_SIZE) {
        base = mem->ewram; start = GM_EWRAM_BASE; size = GM_EWRAM_SIZE;
    } else if (addr >= GM_IWRAM_BASE && addr - GM_IWRAM_BASE < GM_IWRAM_SIZE) {
        base = mem->iwram; start = GM_IWRAM_BASE; size = GM_IWRAM_SIZE;
    } else if (addr >= GM_ROM_BASE && addr - GM_ROM_BASE < mem->rom_size) {
        base = mem->rom; start = GM_ROM_BASE; size = mem->rom_size;
    } else {
        return NULL;
    }
    if (!base) return NULL;
    uint32_t off = addr - start;
    if (len > size - off) return NULL;
    return base + off;
}

bool game_mem_ok(const GameMem* mem, uint32_t addr, uint32_t len) {
    return game_mem_span(mem, addr, len) != NULL;
}

bool game_mem_copy(const GameMem* mem, uint32_t addr, void* dst, uint32_t len) {
    const uint8_t* p = game_mem_span(mem, addr, len);
    if (!p) return false;
    memcpy(dst, p, len);
    return true;
}

bool game_mem_read8(const GameMem* mem, uint32_t addr, uint8_t* out) {
    const uint8_t* p = game_mem_span(mem, addr, 1);
    if (!p) return false;
    *out = p[0];
    return true;
}

bool game_mem_read16(const GameMem* mem, uint32_t addr, uint16_t* out) {
    const uint8_t* p = game_mem_span(mem, addr, 2);
    if (!p) return false;
    *out = (uint16_t)(p[0] | (p[1] << 8));
    return true;
}

bool game_mem_read32(const GameMem* mem, uint32_t addr, uint32_t* out) {
    const uint8_t* p = game_mem_span(mem, addr, 4);
    if (!p) return false;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
    return true;
}

bool game_mem_in_ewram(uint32_t addr, uint32_t len) {
    if (len == 0 || addr < GM_EWRAM_BASE) return false;
    uint32_t off = addr - GM_EWRAM_BASE;
    return off < GM_EWRAM_SIZE && len <= GM_EWRAM_SIZE - off;
}

void game_strcpy(char* dst, size_t dst_size, const char* src) {
    size_t i = 0;
    if (dst_size == 0) return;
    while (src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}
