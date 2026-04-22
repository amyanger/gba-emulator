#include "test_harness.h"
#include "cartridge/cartridge.h"
#include "cartridge/gpio.h"
#include "cartridge/rtc.h"

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

/* Bit-bang helpers for tests: clock one bit into the RTC. */
static uint8_t rtc_shift_bit(RTCState* r, uint8_t sio_in) __attribute__((unused));
static void rtc_cs_high(RTCState* r) __attribute__((unused));
static void rtc_cs_low(RTCState* r) __attribute__((unused));
static void rtc_send_cmd(RTCState* r, uint8_t cmd) __attribute__((unused));

static uint8_t rtc_shift_bit(RTCState* r, uint8_t sio_in) {
    /* CS held high, SCK falling then rising. */
    uint8_t out = rtc_gpio_exchange(r, /*cs=*/1, /*sck=*/0, sio_in,
                                    /*cs_r=*/0, /*cs_f=*/0, /*sck_r=*/0);
    out = rtc_gpio_exchange(r, /*cs=*/1, /*sck=*/1, sio_in,
                            /*cs_r=*/0, /*cs_f=*/0, /*sck_r=*/1);
    return out;
}

static void rtc_cs_high(RTCState* r) {
    rtc_gpio_exchange(r, 1, 0, 0, /*cs_r=*/1, 0, 0);
}

static void rtc_cs_low(RTCState* r) {
    rtc_gpio_exchange(r, 0, 0, 0, 0, /*cs_f=*/1, 0);
}

/* Send an 8-bit command MSB-first. */
static void rtc_send_cmd(RTCState* r, uint8_t cmd) {
    rtc_cs_high(r);
    for (int i = 7; i >= 0; i--) rtc_shift_bit(r, (cmd >> i) & 1);
}

TEST(rtc_power_on_state) {
    RTCState r;
    rtc_init(&r);
    ASSERT_EQ(r.phase, RTC_PHASE_IDLE);
    ASSERT_EQ_HEX(r.status_reg, 0x80);
}

TEST(rtc_bad_command_nibble_ignored) {
    RTCState r;
    rtc_init(&r);
    rtc_send_cmd(&r, 0xE1); /* top nibble 0xE — not 0x6 */
    ASSERT_EQ(r.phase, RTC_PHASE_STALL);
    rtc_cs_low(&r);
    ASSERT_EQ(r.phase, RTC_PHASE_IDLE);
}

void run_rtc_tests(void) {
    printf("\nRTC tests:\n");
    RUN_TEST(gpio_power_on_defaults);
    RUN_TEST(gpio_read_disabled_returns_rom);
    RUN_TEST(gpio_read_enabled_returns_register);
    RUN_TEST(gpio_write_always_routes);
    RUN_TEST(rtc_power_on_state);
    RUN_TEST(rtc_bad_command_nibble_ignored);
}
