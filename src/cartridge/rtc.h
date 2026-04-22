#ifndef RTC_H
#define RTC_H

#include "cartridge.h"
#include <time.h>

void rtc_init(RTCState* rtc);

/* Drive the RTC with pin state from GPIO. Returns the SIO bit the RTC is
 * asserting (0 or 1). `sio_in` is only used when writing to the chip. */
uint8_t rtc_gpio_exchange(RTCState* rtc,
                          uint8_t cs, uint8_t sck, uint8_t sio_in,
                          uint8_t cs_rising, uint8_t cs_falling,
                          uint8_t sck_rising);

/* Test seam. Default source is `time`. Pass NULL to reset. */
void rtc_set_time_source(time_t (*source)(time_t*));

#endif // RTC_H
