#include "flash.h"

// Macronix MX29L010 (128KB) — used by Pokemon Emerald
#define MACRONIX_MANUFACTURER 0xC2
#define MACRONIX_DEVICE_128K  0x09

// Sanyo (64KB)
#define SANYO_MANUFACTURER    0x62
#define SANYO_DEVICE_64K      0x13

void flash_init(FlashChip* flash, bool is_128k) {
    memset(flash->data, 0xFF, sizeof(flash->data));
    flash->state = FLASH_READY;
    flash->bank = 0;

    if (is_128k) {
        flash->manufacturer = MACRONIX_MANUFACTURER;
        flash->device = MACRONIX_DEVICE_128K;
    } else {
        flash->manufacturer = SANYO_MANUFACTURER;
        flash->device = SANYO_DEVICE_64K;
    }
}

uint8_t flash_read(FlashChip* flash, uint32_t addr) {
    addr &= 0xFFFF;

    if (flash->state == FLASH_AUTOSELECT) {
        if (addr == 0x0000) return flash->manufacturer;
        if (addr == 0x0001) return flash->device;
        return 0;
    }

    return flash->data[flash->bank * 0x10000 + addr];
}

void flash_write(FlashChip* flash, uint32_t addr, uint8_t val) {
    addr &= 0xFFFF;

    switch (flash->state) {
    case FLASH_READY:
        if (addr == 0x5555 && val == 0xAA) {
            flash->state = FLASH_CMD1;
        }
        break;

    case FLASH_CMD1:
        if (addr == 0x2AAA && val == 0x55) {
            flash->state = FLASH_CMD2;
        } else {
            flash->state = FLASH_READY;
        }
        break;

    case FLASH_CMD2:
        if (addr == 0x5555) {
            switch (val) {
            case 0x90: // Enter chip identification
                flash->state = FLASH_AUTOSELECT;
                return;
            case 0xF0: // Exit / reset
                flash->state = FLASH_READY;
                return;
            case 0x80: // Erase preparation
                flash->state = FLASH_ERASE;
                return;
            case 0xA0: // Byte program
                flash->state = FLASH_WRITE;
                return;
            case 0xB0: // Bank switch (128K only)
                flash->state = FLASH_BANKSWITCH;
                return;
            }
        }
        flash->state = FLASH_READY;
        break;

    case FLASH_AUTOSELECT:
        if (val == 0xF0) {
            flash->state = FLASH_READY;
        }
        break;

    case FLASH_ERASE:
        if (addr == 0x5555 && val == 0xAA) {
            // Start of second command sequence for erase (step 4 of 6)
            flash->state = FLASH_ERASE_CMD1;
        } else {
            flash->state = FLASH_READY;
        }
        break;

    case FLASH_ERASE_CMD1:
        if (addr == 0x2AAA && val == 0x55) {
            // Step 5 of 6 — advance to erase command selection
            flash->state = FLASH_ERASE_CMD2;
        } else {
            flash->state = FLASH_READY;
        }
        break;

    case FLASH_ERASE_CMD2:
        if (addr == 0x5555 && val == 0x10) {
            // Chip erase — fill all 128KB with 0xFF
            memset(flash->data, 0xFF, sizeof(flash->data));
            LOG_DEBUG("Flash: chip erase");
            flash->state = FLASH_READY;
        } else if (val == 0x30) {
            // Sector erase — erase 4KB sector at aligned address
            uint32_t sector = (flash->bank * 0x10000) + (addr & 0xF000);
            memset(&flash->data[sector], 0xFF, 0x1000);
            LOG_DEBUG("Flash: sector erase at 0x%06X", sector);
            flash->state = FLASH_READY;
        } else {
            flash->state = FLASH_READY;
        }
        break;

    case FLASH_WRITE:
        // Single byte program — can only clear bits (AND with existing)
        flash->data[flash->bank * 0x10000 + addr] &= val;
        flash->state = FLASH_READY;
        break;

    case FLASH_BANKSWITCH:
        if (addr == 0x0000) {
            flash->bank = val & 1;
        }
        flash->state = FLASH_READY;
        break;
    }
}
