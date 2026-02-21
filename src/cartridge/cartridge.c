#include "cartridge.h"
#include "flash.h"

bool cartridge_load(Cartridge* cart, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("Cannot open ROM: %s", path);
        return false;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > MAX_ROM_SIZE || size <= 0) {
        LOG_ERROR("Invalid ROM size: %ld bytes", size);
        fclose(f);
        return false;
    }

    cart->rom = (uint8_t*)malloc(size);
    if (!cart->rom) {
        LOG_ERROR("Failed to allocate ROM memory");
        fclose(f);
        return false;
    }

    cart->rom_size = (uint32_t)size;
    fread(cart->rom, 1, size, f);
    fclose(f);

    // Parse header
    memcpy(cart->title, &cart->rom[0xA0], 12);
    cart->title[12] = '\0';
    memcpy(cart->game_code, &cart->rom[0xAC], 4);
    cart->game_code[4] = '\0';

    LOG_INFO("ROM loaded: \"%s\" [%s] (%u KB)", cart->title, cart->game_code,
             cart->rom_size / 1024);

    // Detect save type
    cartridge_detect_save_type(cart);

    // Build save file path
    snprintf(cart->save_path, sizeof(cart->save_path), "saves/%s.sav", cart->game_code);

    // Initialize save hardware
    if (cart->save_type == SAVE_FLASH128 || cart->save_type == SAVE_FLASH64) {
        flash_init(&cart->flash, cart->save_type == SAVE_FLASH128);
    }

    // Try to load existing save
    cartridge_load_save_file(cart);

    return true;
}

void cartridge_destroy(Cartridge* cart) {
    if (cart->rom) {
        free(cart->rom);
        cart->rom = NULL;
    }
}

void cartridge_detect_save_type(Cartridge* cart) {
    cart->save_type = SAVE_NONE;

    // Scan ROM for save type magic strings
    for (uint32_t i = 0; i < cart->rom_size - 12; i++) {
        if (memcmp(&cart->rom[i], "FLASH1M_V", 9) == 0) {
            cart->save_type = SAVE_FLASH128;
            LOG_INFO("Save type detected: Flash 128KB");
            return;
        }
        if (memcmp(&cart->rom[i], "FLASH512_V", 10) == 0 ||
            memcmp(&cart->rom[i], "FLASH_V", 7) == 0) {
            cart->save_type = SAVE_FLASH64;
            LOG_INFO("Save type detected: Flash 64KB");
            return;
        }
        if (memcmp(&cart->rom[i], "SRAM_V", 6) == 0) {
            cart->save_type = SAVE_SRAM;
            LOG_INFO("Save type detected: SRAM 32KB");
            return;
        }
        if (memcmp(&cart->rom[i], "EEPROM_V", 8) == 0) {
            cart->save_type = SAVE_EEPROM;
            LOG_INFO("Save type detected: EEPROM");
            return;
        }
    }
    LOG_INFO("No save type detected");
}

uint8_t cartridge_read8(Cartridge* cart, uint32_t addr) {
    if (addr >= 0x08000000 && addr < 0x0E000000) {
        // ROM region
        uint32_t offset = addr & 0x01FFFFFF;
        if (offset < cart->rom_size) {
            return cart->rom[offset];
        }
        return 0;
    }

    if (addr >= 0x0E000000 && addr < 0x10000000) {
        // SRAM/Flash region
        uint32_t offset = addr & 0xFFFF;
        switch (cart->save_type) {
        case SAVE_SRAM:
            return cart->sram[offset & 0x7FFF];
        case SAVE_FLASH64:
        case SAVE_FLASH128:
            return flash_read(&cart->flash, offset);
        default:
            return 0;
        }
    }

    return 0;
}

void cartridge_write8(Cartridge* cart, uint32_t addr, uint8_t val) {
    if (addr >= 0x0E000000 && addr < 0x10000000) {
        uint32_t offset = addr & 0xFFFF;
        switch (cart->save_type) {
        case SAVE_SRAM:
            cart->sram[offset & 0x7FFF] = val;
            break;
        case SAVE_FLASH64:
        case SAVE_FLASH128:
            flash_write(&cart->flash, offset, val);
            break;
        default:
            break;
        }
    }
}

void cartridge_save_to_file(Cartridge* cart) {
    if (cart->save_type == SAVE_NONE) return;

    FILE* f = fopen(cart->save_path, "wb");
    if (!f) {
        LOG_ERROR("Cannot save to %s", cart->save_path);
        return;
    }

    switch (cart->save_type) {
    case SAVE_SRAM:
        fwrite(cart->sram, 1, sizeof(cart->sram), f);
        break;
    case SAVE_FLASH64:
        fwrite(cart->flash.data, 1, 0x10000, f);
        break;
    case SAVE_FLASH128:
        fwrite(cart->flash.data, 1, 0x20000, f);
        break;
    default:
        break;
    }

    fclose(f);
    LOG_INFO("Save written to %s", cart->save_path);
}

void cartridge_load_save_file(Cartridge* cart) {
    if (cart->save_type == SAVE_NONE) return;

    FILE* f = fopen(cart->save_path, "rb");
    if (!f) return; // No save file yet, that's fine

    switch (cart->save_type) {
    case SAVE_SRAM:
        fread(cart->sram, 1, sizeof(cart->sram), f);
        break;
    case SAVE_FLASH64:
        fread(cart->flash.data, 1, 0x10000, f);
        break;
    case SAVE_FLASH128:
        fread(cart->flash.data, 1, 0x20000, f);
        break;
    default:
        break;
    }

    fclose(f);
    LOG_INFO("Save loaded from %s", cart->save_path);
}
