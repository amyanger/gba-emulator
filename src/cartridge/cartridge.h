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

/* EEPROM bit-serial protocol (GBATEK).
 *
 * Two sizes exist: 4Kbit (512B, 6-bit address) and 64Kbit (8KB, 14-bit
 * address). The size is determined dynamically from the first DMA transfer
 * count: 9 or 73 → 4Kbit; 17 or 81 → 64Kbit. Each DMA halfword carries one
 * protocol bit (LSB only). We always allocate the 8KB max so the same
 * struct serves both, and so a savestate captured at one size loads at the
 * other without truncation. */
#define EEPROM_MAX_SIZE 0x2000  /* 8KB */

typedef enum {
    EEPROM_IDLE,        /* Awaiting DMA begin */
    EEPROM_RX_CMD,      /* Receiving 2-bit command */
    EEPROM_RX_ADDR_R,   /* Receiving address bits for read */
    EEPROM_RX_ADDR_W,   /* Receiving address bits for write */
    EEPROM_RX_DATA,     /* Receiving 64 data bits for write */
    EEPROM_TX_DATA,     /* Transmitting 4 ignore + 64 data bits */
    EEPROM_READY        /* Write complete; subsequent reads return 1 */
} EEPROMState;

typedef struct {
    uint8_t  data[EEPROM_MAX_SIZE];
    uint8_t  addr_bits;     /* 0=unknown, 6=4Kbit, 14=64Kbit */

    EEPROMState state;
    uint16_t  addr;         /* current command's target address */
    uint16_t  rx_count;     /* bits received in current RX state */
    uint16_t  expected_dma; /* DMA count for the in-flight transfer */
    uint64_t  shift;        /* shift register for tx (data out) / rx (data in) */
    uint16_t  tx_count;     /* bits sent so far in TX_DATA */
    bool     dirty;         /* set when data is modified */
} EEPROMChip;

struct Cartridge {
    uint8_t* rom;
    uint32_t rom_size;

    SaveType save_type;
    FlashChip flash;
    uint8_t sram[0x8000]; // 32KB
    EEPROMChip eeprom;
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

/* Halfword write to the GamePak ROM bus (GPIO / EEPROM). The ROM bus is
 * 16-bit — byte writes are ignored on hardware, so this is the only write
 * entry for the 0x08-0x0D region. */
void cartridge_write16(Cartridge* cart, uint32_t addr, uint16_t val);
void cartridge_save_to_file(Cartridge* cart);
void cartridge_load_save_file(Cartridge* cart);

/* Called once per frame from the main loop. Flushes save data to disk
 * if it has changed since the last flush AND the debounce window has
 * elapsed. No-op when nothing has changed or no save chip is present. */
void cartridge_save_tick(Cartridge* cart, time_t now);

#endif // CARTRIDGE_H
