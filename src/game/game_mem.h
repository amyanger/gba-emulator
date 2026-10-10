#ifndef GAME_MEM_H
#define GAME_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only view of emulated memory for the game layer. Reads go straight
 * to the backing arrays, never through the bus, so looking at game state
 * costs no cycles and cannot disturb emulation. A NULL region is unreadable. */
typedef struct {
    const uint8_t* ewram;  /* 0x02000000, 256 KB */
    const uint8_t* iwram;  /* 0x03000000, 32 KB */
    const uint8_t* rom;    /* 0x08000000, rom_size bytes */
    uint32_t rom_size;
} GameMem;

bool game_mem_ok(const GameMem* mem, uint32_t addr, uint32_t len);
bool game_mem_copy(const GameMem* mem, uint32_t addr, void* dst, uint32_t len);
bool game_mem_read8(const GameMem* mem, uint32_t addr, uint8_t* out);
bool game_mem_read16(const GameMem* mem, uint32_t addr, uint16_t* out);
bool game_mem_read32(const GameMem* mem, uint32_t addr, uint32_t* out);

/* True when [addr, addr+len) lies inside EWRAM. Used to validate pointers
 * the game stores in RAM before following them. */
bool game_mem_in_ewram(uint32_t addr, uint32_t len);

/* Copy with truncation; dst is always NUL-terminated when dst_size > 0. */
void game_strcpy(char* dst, size_t dst_size, const char* src);

#endif // GAME_MEM_H
