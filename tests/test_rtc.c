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

void run_rtc_tests(void) {
    printf("\nRTC tests:\n");
    RUN_TEST(gpio_power_on_defaults);
}
