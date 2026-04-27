#ifndef SCREENSHOT_H
#define SCREENSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Build the screenshot output path: "<rom_path>.YYYYMMDD-HHMMSS.png".
 * Mirrors savestate_slot_path — appends to the full ROM path including
 * the ".gba" extension so screenshots sit next to the ROM and saves.
 * The timestamp is formatted in UTC for reproducibility across machines.
 * Output is always null-terminated when out_size > 0. */
void screenshot_path(const char* rom_path, time_t now, char* out, size_t out_size);

/* Encode the GBA framebuffer (240x160 ABGR1555, native PPU output) as a PNG.
 * Returns true on success, false on encode/write error. */
bool screenshot_save(const uint16_t* framebuffer_abgr1555, const char* path);

#endif // SCREENSHOT_H
