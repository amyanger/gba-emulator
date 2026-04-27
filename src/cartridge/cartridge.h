#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include <time.h>

#include "common.h"

#define MAX_ROM_SIZE 0x2000000 // 32MB

/* Autosave debounce: flush save data at most once every N seconds even
 * when the game writes continuously. Pokemon Emerald's in-game save burst
 * fits inside this window, so one disk write captures the whole save. */
#define CARTRIDGE_AUTOSAVE_DEBOUNCE_SECONDS 5

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
    FLASH_ERASE_CMD1,
    FLASH_ERASE_CMD2,
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
    RTC_PHASE_IDLE,
    RTC_PHASE_CMD,
    RTC_PHASE_DATA_OUT,
    RTC_PHASE_DATA_IN,
    RTC_PHASE_STALL     /* bad command byte — wait for CS falling */
} RTCPhase;

typedef struct {
    RTCPhase phase;
    uint8_t  cmd_byte;       /* accumulated command bits */
    uint8_t  cmd_bits;       /* number of bits shifted so far (0..8) */
    uint8_t  payload[8];     /* max payload is 7 (DateTime); allow 8 for safety */
    uint8_t  payload_len;    /* bytes expected for the current command */
    uint8_t  payload_byte;   /* current byte index into payload */
    uint8_t  payload_bit;    /* current bit index within the current byte (0..7) */
    uint8_t  status_reg;     /* status register: bits 6=24H, 7=POWER, others reserved */
    int64_t  offset_secs;    /* signed offset applied to host time */

    /* Edge-detection scratch used by gpio.c. Not part of the protocol itself. */
    uint8_t  prev_cs;
    uint8_t  prev_sck;
    uint8_t  sio_out;        /* bit the RTC is currently driving */
} RTCState;

typedef struct {
    uint16_t data;        /* 0x080000C4: bits 0=SCK, 1=SIO, 2=CS, 3=unused */
    uint16_t direction;   /* 0x080000C6: per-bit 1 = GBA output, 0 = GBA input */
    uint16_t control;     /* 0x080000C8: bit 0 = read_enable */
} GPIOState;

struct Cartridge {
    uint8_t* rom;
    uint32_t rom_size;

    SaveType save_type;
    FlashChip flash;
    uint8_t sram[0x8000]; // 32KB
    RTCState rtc;
    GPIOState gpio;

    // ROM header info
    char title[13];
    char game_code[5];

    // Save file path
    char save_path[256];

    // Autosave bookkeeping
    bool   save_dirty;       // set by cartridge_write8 when save region changes
    time_t last_save_flush;  // wall clock of most recent successful flush
};
typedef struct Cartridge Cartridge;

bool cartridge_load(Cartridge* cart, const char* path);
void cartridge_destroy(Cartridge* cart);
void cartridge_detect_save_type(Cartridge* cart);
uint8_t cartridge_read8(Cartridge* cart, uint32_t addr);
void cartridge_write8(Cartridge* cart, uint32_t addr, uint8_t val);
void cartridge_save_to_file(Cartridge* cart);
void cartridge_load_save_file(Cartridge* cart);

/* Called once per frame from the main loop. Flushes save data to disk
 * if it has changed since the last flush AND the debounce window has
 * elapsed. No-op when nothing has changed or no save chip is present. */
void cartridge_save_tick(Cartridge* cart, time_t now);

#endif // CARTRIDGE_H
