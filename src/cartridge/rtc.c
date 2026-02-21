#include "rtc.h"
#include <time.h>

void rtc_init(RTCState* rtc) {
    memset(rtc, 0, sizeof(RTCState));
}

// Convert a decimal value to BCD
static uint8_t to_bcd(int val) {
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

uint8_t rtc_read(RTCState* rtc) {
    // TODO: Implement GPIO serial protocol
    // Return current bit from data buffer based on bit_index
    return rtc->data_pin;
}

void rtc_write(RTCState* rtc, uint8_t val) {
    // TODO: Implement full GPIO serial protocol state machine
    // For now, stub the time reading

    // When a time read command completes, fill buffer with host time
    time_t now = time(NULL);
    struct tm* t = localtime(&now);

    rtc->data_buffer[0] = to_bcd(t->tm_year - 100); // Year (00-99)
    rtc->data_buffer[1] = to_bcd(t->tm_mon + 1);    // Month (1-12)
    rtc->data_buffer[2] = to_bcd(t->tm_mday);        // Day (1-31)
    rtc->data_buffer[3] = to_bcd(t->tm_wday);        // Day of week (0-6)
    rtc->data_buffer[4] = to_bcd(t->tm_hour);         // Hour (0-23)
    rtc->data_buffer[5] = to_bcd(t->tm_min);          // Minute (0-59)
    rtc->data_buffer[6] = to_bcd(t->tm_sec);          // Second (0-59)
}
