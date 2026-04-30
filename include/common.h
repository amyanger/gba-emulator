#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Bit manipulation macros
#define BIT(val, n) (((val) >> (n)) & 1)
#define BITS(val, hi, lo) (((val) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1))
#define SET_BIT(val, n) ((val) | (1u << (n)))
#define CLR_BIT(val, n) ((val) & ~(1u << (n)))

// Logging
#ifdef DEBUG
#define LOG_DEBUG(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...)
#endif

#define LOG_INFO(fmt, ...) fprintf(stderr, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) fprintf(stderr, "[WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

// GBA timing constants
#define CPU_FREQ 16777216 // 16.78 MHz

#define CYCLES_PER_PIXEL 4
#define HDRAW_PIXELS 240
#define HBLANK_PIXELS 68
#define SCANLINE_CYCLES 1232 // (240 + 68) * 4

// Sub-scanline timing edges (per GBATEK).
// HBlank flag becomes 1 at cycle 1006 of each 1232-cycle scanline,
// not at the end of pixel transmission (cycle 960). Tests + the
// orchestrator use these to split chunk 1.
#define HBLANK_FLAG_SET_CYCLE 1006
#define HBLANK_TAIL_CYCLES    226   // 1232 - 1006

#define VDRAW_LINES 160
#define VBLANK_LINES 68
#define TOTAL_LINES 228 // 160 + 68
#define FRAME_CYCLES 280896 // 228 * 1232

// Screen dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160

#endif // COMMON_H
