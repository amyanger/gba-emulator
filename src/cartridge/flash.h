#ifndef FLASH_H
#define FLASH_H

#include "cartridge.h"

void flash_init(FlashChip* flash, bool is_128k);
uint8_t flash_read(FlashChip* flash, uint32_t addr);
void flash_write(FlashChip* flash, uint32_t addr, uint8_t val);

#endif // FLASH_H
