#include "cartridge.h"
#include "eeprom.h"
#include "flash.h"
#include "gpio.h"
#include "rtc.h"

#define RTC_TRAILER_MAGIC "GRTC"
#define RTC_TRAILER_VERSION 1u
#define RTC_TRAILER_SIZE 16

static size_t payload_size_for(const Cartridge* cart) {
    switch (cart->save_type) {
    case SAVE_SRAM:     return sizeof(cart->sram);
    case SAVE_FLASH64:  return 0x10000;
    case SAVE_FLASH128: return 0x20000;
    case SAVE_EEPROM:   return EEPROM_MAX_SIZE;
    default:            return 0;
    }
}

static void write_rtc_trailer(FILE* f, const Cartridge* cart) {
    uint8_t trailer[RTC_TRAILER_SIZE];
    memcpy(trailer, RTC_TRAILER_MAGIC, 4);
    uint32_t v = RTC_TRAILER_VERSION;
    trailer[4] = (uint8_t)v;
    trailer[5] = (uint8_t)(v >> 8);
    trailer[6] = (uint8_t)(v >> 16);
    trailer[7] = (uint8_t)(v >> 24);
    uint64_t u = (uint64_t)cart->rtc.offset_secs;
    for (int i = 0; i < 8; i++) trailer[8 + i] = (uint8_t)(u >> (i * 8));
    fwrite(trailer, 1, RTC_TRAILER_SIZE, f);
}

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

    size_t bytes_read = fread(cart->rom, 1, size, f);
    if (bytes_read != (size_t)size) {
        LOG_ERROR("ROM read incomplete: expected %ld, got %zu", size, bytes_read);
        free(cart->rom);
        cart->rom = NULL;
        fclose(f);
        return false;
    }

    cart->rom_size = (uint32_t)size;
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

    // Save file sits next to the ROM (append .sav). Absolute-path-correct
    // regardless of the emulator's CWD, and matches the savestate convention.
    snprintf(cart->save_path, sizeof(cart->save_path), "%s.sav", path);

    // Initialize save hardware
    if (cart->save_type == SAVE_FLASH128 || cart->save_type == SAVE_FLASH64) {
        flash_init(&cart->flash, cart->save_type == SAVE_FLASH128);
    }

    gpio_init(cart);
    rtc_init(&cart->rtc);
    eeprom_init(&cart->eeprom);

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

    // Guard: ROM too small to contain any save type string
    if (cart->rom_size < 12) return;

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
        if (eeprom_addr_in_range(cart, addr)) {
            /* EEPROM data port lives in bit 0 of each halfword. The LSB
             * sits in the low byte; the high byte returns 0. */
            if (addr & 1) return 0;
            return eeprom_read_bit(&cart->eeprom);
        }
        uint32_t offset = addr & 0x01FFFFFF;
        if (offset >= 0xC4 && offset <= 0xC9 && (cart->gpio.control & 1)) {
            uint32_t reg = offset & ~1u;
            uint16_t hw = gpio_read(cart, reg);
            return (offset & 1) ? (uint8_t)(hw >> 8) : (uint8_t)hw;
        }
        if (offset < cart->rom_size) {
            return cart->rom[offset];
        }
        return 0;
    }
    if (addr >= 0x0E000000 && addr < 0x10000000) {
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
    if (addr >= 0x08000000 && addr < 0x0E000000) {
        if (eeprom_addr_in_range(cart, addr)) {
            /* Only the low byte carries the protocol bit; high byte is a no-op. */
            if (addr & 1) return;
            eeprom_write_bit(&cart->eeprom, val & 1);
            if (cart->eeprom.dirty) {
                cart->save_dirty = true;
                cart->eeprom.dirty = false;
            }
            return;
        }
        uint32_t offset = addr & 0x01FFFFFF;
        if (offset >= 0xC4 && offset <= 0xC9) {
            uint32_t reg = offset & ~1u;
            uint16_t hw = gpio_read(cart, reg);
            uint16_t updated = (offset & 1)
                ? (uint16_t)((hw & 0x00FF) | ((uint16_t)val << 8))
                : (uint16_t)((hw & 0xFF00) | val);
            gpio_write(cart, reg, updated);
        }
        return;
    }
    if (addr >= 0x0E000000 && addr < 0x10000000) {
        uint32_t offset = addr & 0xFFFF;
        switch (cart->save_type) {
        case SAVE_SRAM:
            cart->sram[offset & 0x7FFF] = val;
            cart->save_dirty = true;
            break;
        case SAVE_FLASH64:
        case SAVE_FLASH128:
            /* Flash command bytes also pass through here; debounce
             * coalesces them into a single disk write. */
            flash_write(&cart->flash, offset, val);
            cart->save_dirty = true;
            break;
        default:
            break;
        }
    }
}

void cartridge_save_to_file(Cartridge* cart) {
    if (cart->save_type == SAVE_NONE) return;

    /* Write to a temp file then rename. POSIX rename() is atomic, so a
     * crash mid-write leaves the previous .sav intact rather than truncated. */
    char tmp_path[sizeof(cart->save_path) + 4];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cart->save_path);

    FILE* f = fopen(tmp_path, "wb");
    if (!f) {
        LOG_ERROR("Cannot save to %s", tmp_path);
        return;
    }

    size_t expected = 0;
    size_t written = 0;
    switch (cart->save_type) {
    case SAVE_SRAM:
        expected = sizeof(cart->sram);
        written = fwrite(cart->sram, 1, expected, f);
        break;
    case SAVE_FLASH64:
        expected = 0x10000;
        written = fwrite(cart->flash.data, 1, expected, f);
        break;
    case SAVE_FLASH128:
        expected = 0x20000;
        written = fwrite(cart->flash.data, 1, expected, f);
        break;
    case SAVE_EEPROM:
        expected = EEPROM_MAX_SIZE;
        written = fwrite(cart->eeprom.data, 1, expected, f);
        break;
    default:
        break;
    }

    if (expected > 0 && written == expected) write_rtc_trailer(f, cart);
    fclose(f);

    if (expected > 0 && written != expected) {
        LOG_WARN("Save write incomplete: expected %zu, wrote %zu", expected, written);
        remove(tmp_path);
        return;
    }

    if (rename(tmp_path, cart->save_path) != 0) {
        LOG_ERROR("Failed to commit save file %s", cart->save_path);
        remove(tmp_path);
        return;
    }

    cart->save_dirty = false;
    cart->last_save_flush = time(NULL);
    LOG_INFO("Save written to %s", cart->save_path);
}

void cartridge_save_tick(Cartridge* cart, time_t now) {
    if (!cart || !cart->save_dirty) return;
    if (cart->save_type == SAVE_NONE) return;
    if (now - cart->last_save_flush < CARTRIDGE_AUTOSAVE_DEBOUNCE_SECONDS) return;
    cartridge_save_to_file(cart);
}

void cartridge_load_save_file(Cartridge* cart) {
    if (cart->save_type == SAVE_NONE) return;

    FILE* f = fopen(cart->save_path, "rb");
    if (!f) return; // No save file yet, that's fine

    size_t payload = payload_size_for(cart);
    if (payload == 0) { fclose(f); return; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t to_read = (size >= (long)payload) ? payload : (size > 0 ? (size_t)size : 0);
    switch (cart->save_type) {
    case SAVE_SRAM:
        fread(cart->sram, 1, to_read, f);
        break;
    case SAVE_FLASH64:
    case SAVE_FLASH128:
        fread(cart->flash.data, 1, to_read, f);
        break;
    case SAVE_EEPROM:
        fread(cart->eeprom.data, 1, to_read, f);
        break;
    default:
        break;
    }

    cart->rtc.offset_secs = 0;

    if ((size_t)size == payload + RTC_TRAILER_SIZE) {
        uint8_t trailer[RTC_TRAILER_SIZE];
        if (fread(trailer, 1, RTC_TRAILER_SIZE, f) == RTC_TRAILER_SIZE &&
            memcmp(trailer, RTC_TRAILER_MAGIC, 4) == 0) {
            uint32_t ver = (uint32_t)trailer[4]
                         | ((uint32_t)trailer[5] << 8)
                         | ((uint32_t)trailer[6] << 16)
                         | ((uint32_t)trailer[7] << 24);
            if (ver == RTC_TRAILER_VERSION) {
                uint64_t u = 0;
                for (int i = 0; i < 8; i++) u |= (uint64_t)trailer[8 + i] << (i * 8);
                cart->rtc.offset_secs = (int64_t)u;
            } else {
                LOG_WARN("RTC trailer version %u not supported, ignoring", ver);
            }
        } else {
            LOG_WARN("RTC trailer magic mismatch, ignoring");
        }
    } else if ((size_t)size != payload) {
        LOG_WARN("Unexpected save size %ld (expected %zu or %zu), trailer ignored",
                 size, payload, payload + RTC_TRAILER_SIZE);
    }

    fclose(f);
    LOG_INFO("Save loaded from %s", cart->save_path);
}
