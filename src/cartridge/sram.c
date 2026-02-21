#include "cartridge.h"

// SRAM is straightforward — 32KB of battery-backed static RAM.
// Read/write handled directly in cartridge.c through the sram[] array.
// This file reserved for any SRAM-specific logic if needed.
