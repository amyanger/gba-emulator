#include <string.h>

#include "eeprom.h"
#include "cartridge.h"

/* GBATEK reference: each EEPROM "transfer" is one halfword DMA write or
 * read whose LSB carries one protocol bit. The DMA count tells us:
 *
 *   command       4Kbit count   64Kbit count
 *   ------------  -----------   ------------
 *   read addr     9             17
 *   read data     68            68
 *   write         73            81
 *   ready poll    1             1
 *
 * We use the first 9/17/73/81 we see to lock in addr_bits (6 for 4Kbit,
 * 14 for 64Kbit). The size never changes after the first sighting unless a
 * savestate-load resets it. */

/* For ROMs <= 16MB the entire 0x0D000000-0x0DFFFFFF window is EEPROM.
 * For larger carts only the top 256 bytes (0x0DFFFF00-0x0DFFFFFF) map to
 * EEPROM — the rest is ROM mirror. */
bool eeprom_addr_in_range(const Cartridge* cart, uint32_t addr) {
    if (cart->save_type != SAVE_EEPROM) return false;
    if (addr < 0x0D000000 || addr >= 0x0E000000) return false;
    if (cart->rom_size > 0x01000000) {
        return addr >= 0x0DFFFF00;
    }
    return true;
}

void eeprom_init(EEPROMChip* eep) {
    memset(eep, 0, sizeof(*eep));
    /* Real EEPROM cells read 1 when erased. Initialise to 0xFF so a fresh
     * chip behaves like a freshly erased one. */
    memset(eep->data, 0xFF, sizeof(eep->data));
    eep->state = EEPROM_IDLE;
}

void eeprom_dma_begin(EEPROMChip* eep, uint32_t count) {
    switch (count) {
    case 9:
        if (eep->addr_bits == 0) eep->addr_bits = 6;
        eep->state = EEPROM_RX_CMD;
        eep->rx_count = 0;
        break;
    case 17:
        if (eep->addr_bits == 0) eep->addr_bits = 14;
        eep->state = EEPROM_RX_CMD;
        eep->rx_count = 0;
        break;
    case 73:
        if (eep->addr_bits == 0) eep->addr_bits = 6;
        eep->state = EEPROM_RX_CMD;
        eep->rx_count = 0;
        break;
    case 81:
        if (eep->addr_bits == 0) eep->addr_bits = 14;
        eep->state = EEPROM_RX_CMD;
        eep->rx_count = 0;
        break;
    case 68:
        /* Read-data phase. We should already be in TX_DATA after the 9/17
         * command transfer set up the shift register. If something went
         * wrong upstream, fall back to producing zeros. */
        break;
    case 1:
        /* Ready poll. eeprom_read_bit returns 1 in IDLE/READY. */
        break;
    default:
        /* Unknown count: leave state alone so we don't desync mid-transfer. */
        break;
    }
    eep->expected_dma = (uint16_t)count;
}

static void eeprom_load_shift_for_read(EEPROMChip* eep) {
    uint32_t mask = (eep->addr_bits == 6) ? 0x3F : 0x3FFF;
    eep->addr &= (uint16_t)mask;
    size_t base = (size_t)eep->addr * 8;
    uint64_t v = 0;
    if (base + 8 <= sizeof(eep->data)) {
        for (int i = 0; i < 8; i++) {
            v = (v << 8) | eep->data[base + i];
        }
    }
    eep->shift = v;
}

static void eeprom_commit_write(EEPROMChip* eep) {
    uint32_t mask = (eep->addr_bits == 6) ? 0x3F : 0x3FFF;
    eep->addr &= (uint16_t)mask;
    size_t base = (size_t)eep->addr * 8;
    if (base + 8 > sizeof(eep->data)) return;
    for (int i = 0; i < 8; i++) {
        eep->data[base + i] = (uint8_t)(eep->shift >> ((7 - i) * 8));
    }
    eep->dirty = true;
}

void eeprom_write_bit(EEPROMChip* eep, uint8_t bit) {
    bit &= 1;
    /* Defensive default: if no DMA has set up state yet, treat as IDLE. */
    if (eep->addr_bits == 0) return;

    switch (eep->state) {
    case EEPROM_RX_CMD:
        if (eep->rx_count == 0) {
            /* First command bit must be 1; ignore stray leading zeros. */
            if (bit == 0) return;
            eep->rx_count = 1;
        } else {
            /* Second command bit: 1 = read, 0 = write. */
            eep->state = (bit == 1) ? EEPROM_RX_ADDR_R : EEPROM_RX_ADDR_W;
            eep->rx_count = 0;
            eep->addr = 0;
        }
        return;

    case EEPROM_RX_ADDR_R:
        if (eep->rx_count < eep->addr_bits) {
            eep->addr = (uint16_t)((eep->addr << 1) | bit);
        }
        eep->rx_count++;
        /* After addr_bits address bits + 1 terminator bit, the read command
         * is complete and we hand control to TX_DATA. */
        if (eep->rx_count > eep->addr_bits) {
            eeprom_load_shift_for_read(eep);
            eep->state = EEPROM_TX_DATA;
            eep->tx_count = 0;
        }
        return;

    case EEPROM_RX_ADDR_W:
        eep->addr = (uint16_t)((eep->addr << 1) | bit);
        eep->rx_count++;
        if (eep->rx_count == eep->addr_bits) {
            eep->state = EEPROM_RX_DATA;
            eep->rx_count = 0;
            eep->shift = 0;
        }
        return;

    case EEPROM_RX_DATA:
        if (eep->rx_count < 64) {
            eep->shift = (eep->shift << 1) | bit;
        }
        eep->rx_count++;
        /* 64 data bits + 1 terminator → commit. */
        if (eep->rx_count > 64) {
            eeprom_commit_write(eep);
            eep->state = EEPROM_READY;
        }
        return;

    case EEPROM_TX_DATA:
    case EEPROM_READY:
    case EEPROM_IDLE:
    default:
        /* Stray writes during read/ready phases are ignored. */
        return;
    }
}

uint8_t eeprom_read_bit(EEPROMChip* eep) {
    if (eep->state == EEPROM_TX_DATA) {
        uint8_t bit;
        if (eep->tx_count < 4) {
            /* Four leading dummy zeros per GBATEK. */
            bit = 0;
        } else {
            int idx = 63 - (int)(eep->tx_count - 4);
            bit = (uint8_t)((eep->shift >> idx) & 1);
        }
        eep->tx_count++;
        if (eep->tx_count >= 68) {
            eep->state = EEPROM_IDLE;
        }
        return bit;
    }
    /* IDLE or READY: report ready. The host polls with count=1 reads after
     * a write command and looks for a 1 bit. */
    return 1;
}
