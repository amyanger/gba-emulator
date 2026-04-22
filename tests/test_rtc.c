#include "test_harness.h"
#include "cartridge/cartridge.h"
#include "cartridge/gpio.h"

static Cartridge make_cart(void) {
    Cartridge c;
    memset(&c, 0, sizeof(c));
    gpio_init(&c);
    return c;
}

TEST(gpio_power_on_defaults) {
    Cartridge c = make_cart();
    ASSERT_EQ(gpio_read(&c, 0xC4), 0);
    ASSERT_EQ(gpio_read(&c, 0xC6), 0);
    ASSERT_EQ(gpio_read(&c, 0xC8), 0);
}

TEST(gpio_read_disabled_returns_rom) {
    Cartridge c = make_cart();
    /* Fake a 256-byte ROM with recognizable bytes at 0xC4..0xC9 */
    uint8_t rom[0x100];
    memset(rom, 0xFF, sizeof(rom));
    rom[0xC4] = 0xAB;
    rom[0xC5] = 0xCD;
    c.rom = rom;
    c.rom_size = sizeof(rom);
    /* control bit 0 = 0 (default) -> reads pass through to ROM */
    ASSERT_EQ_HEX(cartridge_read8(&c, 0x080000C4), 0xAB);
    ASSERT_EQ_HEX(cartridge_read8(&c, 0x080000C5), 0xCD);
    c.rom = NULL; /* don't free stack buffer in destroy */
}

TEST(gpio_read_enabled_returns_register) {
    Cartridge c = make_cart();
    uint8_t rom[0x100];
    memset(rom, 0xFF, sizeof(rom));
    c.rom = rom;
    c.rom_size = sizeof(rom);
    /* Enable read-back, set direction to all-output so we can write-then-read data. */
    cartridge_write8(&c, 0x080000C8, 0x01);
    cartridge_write8(&c, 0x080000C6, 0x0F);
    cartridge_write8(&c, 0x080000C4, 0x05); /* SCK=1, SIO=0, CS=1 */
    ASSERT_EQ_HEX(cartridge_read8(&c, 0x080000C4), 0x05);
    ASSERT_EQ_HEX(cartridge_read8(&c, 0x080000C8), 0x01);
    c.rom = NULL;
}

TEST(gpio_write_always_routes) {
    /* Writes are never gated by read_enable - game can poke control without
     * having first enabled reads. */
    Cartridge c = make_cart();
    cartridge_write8(&c, 0x080000C8, 0x01);
    ASSERT_EQ(c.gpio.control & 1, 1);
}

void run_rtc_tests(void) {
    printf("\nRTC tests:\n");
    RUN_TEST(gpio_power_on_defaults);
    RUN_TEST(gpio_read_disabled_returns_rom);
    RUN_TEST(gpio_read_enabled_returns_register);
    RUN_TEST(gpio_write_always_routes);
}
