#include "gpio.h"
#include "rtc.h"

#define GPIO_DATA      0x0C4
#define GPIO_DIRECTION 0x0C6
#define GPIO_CONTROL   0x0C8

#define BIT_SCK 0x01
#define BIT_SIO 0x02
#define BIT_CS  0x04

void gpio_init(Cartridge* cart) {
    cart->gpio.data = 0;
    cart->gpio.direction = 0;
    cart->gpio.control = 0;
}

uint16_t gpio_read(Cartridge* cart, uint32_t offset) {
    switch (offset) {
    case GPIO_DATA: {
        /* Output bits (direction == 1) read back from cart->gpio.data.
         * SIO as GBA-input reads back whatever the RTC is driving (sio_out).
         * SCK/CS as inputs just echo last-written bits (unused in practice). */
        uint16_t out_bits = cart->gpio.data & cart->gpio.direction & 0x000F;
        uint16_t in_bits  = 0;
        if ((cart->gpio.direction & BIT_SIO) == 0) {
            in_bits |= (uint16_t)((cart->rtc.sio_out & 1) << 1);
        }
        uint16_t misc_in = cart->gpio.data & ~cart->gpio.direction & (BIT_SCK | BIT_CS);
        return out_bits | in_bits | misc_in;
    }
    case GPIO_DIRECTION: return cart->gpio.direction & 0x000F;
    case GPIO_CONTROL:   return cart->gpio.control & 0x0001;
    default:             return 0;
    }
}

void gpio_write(Cartridge* cart, uint32_t offset, uint16_t val) {
    switch (offset) {
    case GPIO_DATA: {
        /* Only bits configured as GBA-output are updated from `val`;
         * cart-driven bits are preserved. */
        uint8_t mask = (uint8_t)(cart->gpio.direction & 0x000F);
        uint8_t prev = (uint8_t)(cart->gpio.data & 0x000F);
        uint8_t next = (uint8_t)((prev & ~mask) | (val & mask));
        cart->gpio.data = next;

        uint8_t cs     = (next & BIT_CS)  ? 1 : 0;
        uint8_t sck    = (next & BIT_SCK) ? 1 : 0;
        uint8_t sio_in = (next & BIT_SIO) ? 1 : 0;

        uint8_t cs_rising  = (!cart->rtc.prev_cs  && cs);
        uint8_t cs_falling = ( cart->rtc.prev_cs  && !cs);
        uint8_t sck_rising = (!cart->rtc.prev_sck && sck);

        cart->rtc.prev_cs  = cs;
        cart->rtc.prev_sck = sck;

        rtc_gpio_exchange(&cart->rtc, cs, sck, sio_in, cs_rising, cs_falling, sck_rising);
        break;
    }
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
