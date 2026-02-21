#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "common.h"

#define MAX_ROM_SIZE 0x2000000 // 32MB

typedef enum {
    SAVE_NONE,
    SAVE_SRAM,
    SAVE_FLASH64,
    SAVE_FLASH128,
    SAVE_EEPROM
} SaveType;

typedef enum {
    FLASH_READY,
    FLASH_CMD1,
    FLASH_CMD2,
    FLASH_AUTOSELECT,
    FLASH_ERASE,
    FLASH_WRITE,
    FLASH_BANKSWITCH
} FlashState;

typedef struct {
    uint8_t data[0x20000]; // 128KB (two 64KB banks)
    FlashState state;
    uint8_t bank;          // Current bank (0 or 1)
    uint8_t manufacturer;  // Device ID
    uint8_t device;
} FlashChip;

typedef enum {
    RTC_IDLE,
    RTC_COMMAND,
    RTC_DATA
} RTCStateEnum;

typedef struct {
    uint8_t data_pin;
    uint8_t direction;
    uint8_t control;
    RTCStateEnum state;
    uint8_t command;
    uint8_t bit_index;
    uint8_t byte_index;
    uint8_t data_buffer[8];
} RTCState;

struct Cartridge {
    uint8_t* rom;
    uint32_t rom_size;

    SaveType save_type;
    FlashChip flash;
    uint8_t sram[0x8000]; // 32KB
    RTCState rtc;

    // ROM header info
    char title[13];
    char game_code[5];

    // Save file path
    char save_path[256];
};
typedef struct Cartridge Cartridge;

bool cartridge_load(Cartridge* cart, const char* path);
void cartridge_destroy(Cartridge* cart);
void cartridge_detect_save_type(Cartridge* cart);
uint8_t cartridge_read8(Cartridge* cart, uint32_t addr);
void cartridge_write8(Cartridge* cart, uint32_t addr, uint8_t val);
void cartridge_save_to_file(Cartridge* cart);
void cartridge_load_save_file(Cartridge* cart);

#endif // CARTRIDGE_H
