#ifndef GPIO_H
#define GPIO_H

#include "cartridge.h"

/* Power-on defaults: data=0, direction=0 (all input), control=0 (read_enable off). */
void gpio_init(Cartridge* cart);

/* `offset` is the low-ROM-offset halfword address: one of 0xC4, 0xC6, 0xC8.
 * Returns 16-bit register value (caller masks to 8 bits for byte reads). */
uint16_t gpio_read(Cartridge* cart, uint32_t offset);

/* `offset` in the same set. `val` is a 16-bit write; low nibbles are the only
 * meaningful bits for data/direction, bit 0 for control. */
void gpio_write(Cartridge* cart, uint32_t offset, uint16_t val);

#endif /* GPIO_H */
