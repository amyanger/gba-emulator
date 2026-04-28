#ifndef EEPROM_H
#define EEPROM_H

#include "cartridge.h"

/* Returns true when an EEPROM access should be routed through this module.
 * For ROMs > 16MB the EEPROM only lives in 0x0DFFFF00-0x0DFFFFFF. For
 * smaller ROMs the entire 0x0D000000-0x0DFFFFFF range maps to EEPROM. */
bool eeprom_addr_in_range(const Cartridge* cart, uint32_t addr);

void eeprom_init(EEPROMChip* eep);

/* Called by the DMA controller when it begins a transfer that hits the
 * EEPROM region. The transfer count disambiguates 4Kbit vs 64Kbit:
 *   9  / 73 → 6-bit address  (4Kbit, 512 bytes)
 *   17 / 81 → 14-bit address (64Kbit, 8KB)
 *   68      → data read (size-independent)
 *   1       → ready poll (after a write)
 * Once the size is locked in by the first command transfer, later
 * mismatches are tolerated (some games re-detect by sending dummy commands). */
void eeprom_dma_begin(EEPROMChip* eep, uint32_t count);

/* Receive one bit from the host (LSB of a halfword DMA write). */
void eeprom_write_bit(EEPROMChip* eep, uint8_t bit);

/* Produce one bit for the host (LSB of a halfword DMA read). */
uint8_t eeprom_read_bit(EEPROMChip* eep);

#endif // EEPROM_H
