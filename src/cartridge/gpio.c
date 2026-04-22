#include "gpio.h"

#define GPIO_DATA      0x0C4
#define GPIO_DIRECTION 0x0C6
#define GPIO_CONTROL   0x0C8

void gpio_init(Cartridge* cart) {
    cart->gpio.data = 0;
    cart->gpio.direction = 0;
    cart->gpio.control = 0;
}

uint16_t gpio_read(Cartridge* cart, uint32_t offset) {
    switch (offset) {
    case GPIO_DATA:      return cart->gpio.data & 0x000F;
    case GPIO_DIRECTION: return cart->gpio.direction & 0x000F;
    case GPIO_CONTROL:   return cart->gpio.control & 0x0001;
    default:             return 0;
    }
}

void gpio_write(Cartridge* cart, uint32_t offset, uint16_t val) {
    switch (offset) {
    case GPIO_DATA:
        /* Only bits the GBA owns (direction == 1) are updated from `val`.
         * Bits the cart owns are preserved — the RTC drives them via Task 5. */
        cart->gpio.data = (uint16_t)((cart->gpio.data & ~cart->gpio.direction & 0x000F)
                        | (val & cart->gpio.direction & 0x000F));
        break;
    case GPIO_DIRECTION:
        cart->gpio.direction = val & 0x000F;
        break;
    case GPIO_CONTROL:
        cart->gpio.control = val & 0x0001;
        break;
    default:
        break;
    }
}
