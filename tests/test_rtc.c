#include "test_harness.h"
#include "cartridge/cartridge.h"
#include "cartridge/gpio.h"
#include "cartridge/rtc.h"
#include <time.h>

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

/* Fixed test time source — returned by rtc_set_time_source. */
static time_t g_fake_now = 0;
static time_t fake_time(time_t* out) { if (out) *out = g_fake_now; return g_fake_now; }

/* Read N bytes from the chip (chip is pre-loaded for reads). */
static void rtc_read_payload(RTCState* r, uint8_t* dst, int n) {
    for (int i = 0; i < n; i++) {
        dst[i] = 0;
        for (int b = 0; b < 8; b++) {
            uint8_t bit = rtc_shift_bit(r, 0);
            dst[i] |= (uint8_t)(bit << b);
        }
    }
}

static void rtc_write_payload(RTCState* r, const uint8_t* src, int n) {
    for (int i = 0; i < n; i++) {
        for (int b = 0; b < 8; b++) rtc_shift_bit(r, (src[i] >> b) & 1);
    }
}

static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int from_bcd(uint8_t b) { return ((b >> 4) * 10) + (b & 0x0F); }

TEST(rtc_datetime_read_returns_host_time) {
    RTCState r; rtc_init(&r);
    rtc_set_time_source(fake_time);
    /* 2026-04-22 14:30:15 local — use mktime of a known struct tm. */
    struct tm t = {0};
    t.tm_year = 126; t.tm_mon = 3; t.tm_mday = 22;
    t.tm_hour = 14;  t.tm_min = 30; t.tm_sec = 15;
    t.tm_isdst = -1;
    g_fake_now = mktime(&t);

    /* Enable 24h mode by writing status=0x40. */
    r.status_reg = 0x40;

    rtc_send_cmd(&r, 0x65); /* reg=2 (DateTime), read=1 */
    uint8_t buf[7];
    rtc_read_payload(&r, buf, 7);

    ASSERT_EQ(from_bcd(buf[0]), 26);           /* year */
    ASSERT_EQ(from_bcd(buf[1]), 4);            /* month */
    ASSERT_EQ(from_bcd(buf[2]), 22);           /* day */
    ASSERT_EQ(from_bcd(buf[4] & 0x7F), 14);    /* hour (bit 7 is PM in 12h mode) */
    ASSERT_EQ(from_bcd(buf[5]), 30);
    ASSERT_EQ(from_bcd(buf[6]), 15);

    rtc_cs_low(&r);
    rtc_set_time_source(NULL);
}

TEST(rtc_datetime_write_sets_offset) {
    RTCState r; rtc_init(&r);
    rtc_set_time_source(fake_time);
    struct tm t = {0};
    t.tm_year = 126; t.tm_mon = 3; t.tm_mday = 22;
    t.tm_hour = 10;  t.tm_min = 0;  t.tm_sec = 0;
    t.tm_isdst = -1;
    g_fake_now = mktime(&t);

    r.status_reg = 0x40; /* 24h */

    /* Game sets RTC to 2026-04-25 10:00:00 (3 days ahead). */
    uint8_t payload[7];
    payload[0] = to_bcd(26);
    payload[1] = to_bcd(4);
    payload[2] = to_bcd(25);
    payload[3] = 0; /* wday — ignored on write */
    payload[4] = to_bcd(10);
    payload[5] = 0;
    payload[6] = 0;

    rtc_send_cmd(&r, 0x64); /* reg=2, write=0 */
    rtc_write_payload(&r, payload, 7);

    /* offset should be roughly +3 days in seconds. */
    int64_t three_days = 3LL * 24 * 3600;
    ASSERT_TRUE(r.offset_secs >= three_days - 5 && r.offset_secs <= three_days + 5);

    rtc_cs_low(&r);
    rtc_set_time_source(NULL);
}

TEST(rtc_force_reset_clears_offset_and_status) {
    RTCState r; rtc_init(&r);
    r.status_reg = 0x40;
    r.offset_secs = 12345;
    rtc_send_cmd(&r, 0x60); /* reg=0 write (Force Reset, payload_len=0 → commits in decode) */
    rtc_cs_low(&r);
    ASSERT_EQ_HEX(r.status_reg, 0x80);
    ASSERT_EQ(r.offset_secs, 0);
}

TEST(rtc_status_roundtrip) {
    RTCState r; rtc_init(&r);
    rtc_send_cmd(&r, 0x62); /* reg=1 write */
    uint8_t val = 0x40;
    rtc_write_payload(&r, &val, 1);
    rtc_cs_low(&r);

    rtc_send_cmd(&r, 0x63); /* reg=1 read */
    uint8_t got;
    rtc_read_payload(&r, &got, 1);
    ASSERT_EQ_HEX(got, 0x40);
    rtc_cs_low(&r);
}

/* End-to-end: drive a DateTime read through cartridge_read8/write8. */
TEST(rtc_e2e_datetime_read_via_cartridge) {
    Cartridge c = make_cart();
    uint8_t rom[0x100]; memset(rom, 0xFF, sizeof(rom));
    c.rom = rom; c.rom_size = sizeof(rom);
    rtc_init(&c.rtc);

    rtc_set_time_source(fake_time);
    struct tm t = {0};
    t.tm_year = 126; t.tm_mon = 3; t.tm_mday = 22;
    t.tm_hour = 9;   t.tm_min = 5;  t.tm_sec = 0;
    t.tm_isdst = -1;
    g_fake_now = mktime(&t);
    c.rtc.status_reg = 0x40;

    /* read_enable = 1, direction = all output (SCK/SIO/CS driven by GBA) */
    cartridge_write8(&c, 0x080000C8, 0x01);
    cartridge_write8(&c, 0x080000C6, 0x07); /* SCK+SIO+CS outputs */

    /* Helper: one pin update per call — each cartridge_write8 triggers one
     * edge-detect pass inside gpio_write. */
    #define PIN(cs, sck, sio) cartridge_write8(&c, 0x080000C4, \
        (uint8_t)(((cs) << 2) | ((sio) << 1) | (sck)))

    /* CS rising edge while SCK=0. */
    PIN(1, 0, 0);

    /* Send 0x65 MSB-first (DateTime read). */
    uint8_t cmd = 0x65;
    for (int i = 7; i >= 0; i--) {
        uint8_t bit = (cmd >> i) & 1;
        PIN(1, 0, bit);
        PIN(1, 1, bit);
    }

    /* Now read 7 bytes. SIO must be switched to input so the RTC can drive it. */
    cartridge_write8(&c, 0x080000C6, 0x05); /* SCK+CS output, SIO input */
    uint8_t buf[7];
    for (int i = 0; i < 7; i++) {
        buf[i] = 0;
        for (int b = 0; b < 8; b++) {
            PIN(1, 0, 0);
            PIN(1, 1, 0);
            uint8_t data = cartridge_read8(&c, 0x080000C4);
            buf[i] |= (uint8_t)(((data >> 1) & 1) << b);
        }
    }

    /* CS falling */
    PIN(0, 0, 0);
    #undef PIN

    ASSERT_EQ(from_bcd(buf[0]), 26);
    ASSERT_EQ(from_bcd(buf[1]), 4);
    ASSERT_EQ(from_bcd(buf[2]), 22);
    ASSERT_EQ(from_bcd(buf[4] & 0x7F), 9);

    rtc_set_time_source(NULL);
    c.rom = NULL;
}

void run_rtc_tests(void) {
    printf("\nRTC tests:\n");
    RUN_TEST(gpio_power_on_defaults);
    RUN_TEST(gpio_read_disabled_returns_rom);
    RUN_TEST(gpio_read_enabled_returns_register);
    RUN_TEST(gpio_write_always_routes);
    RUN_TEST(rtc_power_on_state);
    RUN_TEST(rtc_bad_command_nibble_ignored);
    RUN_TEST(rtc_datetime_read_returns_host_time);
    RUN_TEST(rtc_datetime_write_sets_offset);
    RUN_TEST(rtc_force_reset_clears_offset_and_status);
    RUN_TEST(rtc_status_roundtrip);
    RUN_TEST(rtc_e2e_datetime_read_via_cartridge);
}
