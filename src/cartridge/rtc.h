#ifndef RTC_H
#define RTC_H

#include "cartridge.h"

void rtc_init(RTCState* rtc);
uint8_t rtc_read(RTCState* rtc);
void rtc_write(RTCState* rtc, uint8_t val);

#endif // RTC_H
