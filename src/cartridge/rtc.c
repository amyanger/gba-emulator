#include "rtc.h"
#include "common.h"

/* Fixed top nibble of every valid command byte. */
#define RTC_CMD_PREFIX 0x60

static time_t default_time_source(time_t* out) { return time(out); }
static time_t (*g_time_source)(time_t*) = default_time_source;

void rtc_set_time_source(time_t (*source)(time_t*)) {
    g_time_source = source ? source : default_time_source;
}

void rtc_init(RTCState* rtc) {
    memset(rtc, 0, sizeof(*rtc));
    rtc->phase = RTC_PHASE_IDLE;
    rtc->status_reg = 0x80; /* POWER bit set until the game writes status */
}

/* Helpers we'll flesh out in Task 4 — stub for now so the state machine can link. */
static void rtc_fill_payload_for_read(RTCState* rtc, uint8_t reg) {
    (void)rtc; (void)reg;
    /* filled in Task 4 */
}

static void rtc_commit_write(RTCState* rtc, uint8_t reg) {
    (void)rtc; (void)reg;
    /* filled in Task 4 */
}

static void rtc_decode_command(RTCState* rtc) {
    uint8_t cmd = rtc->cmd_byte;
    if ((cmd & 0xF0) != RTC_CMD_PREFIX) {
        rtc->phase = RTC_PHASE_STALL;
        return;
    }
    uint8_t reg = BITS(cmd, 3, 1);
    uint8_t is_read = BIT(cmd, 0);

    /* Payload lengths by register. */
    static const uint8_t len_table[8] = { 0, 1, 7, 0, 3, 3, 3, 0 };
    rtc->payload_len  = len_table[reg];
    rtc->payload_byte = 0;
    rtc->payload_bit  = 0;
    memset(rtc->payload, 0, sizeof(rtc->payload));

    if (rtc->payload_len == 0) {
        /* Commands with no payload (Reset, Force IRQ, Reserved): commit immediately. */
        if (!is_read) rtc_commit_write(rtc, reg);
        rtc->phase = RTC_PHASE_STALL;
        return;
    }

    if (is_read) {
        rtc_fill_payload_for_read(rtc, reg);
        rtc->phase = RTC_PHASE_DATA_OUT;
    } else {
        rtc->phase = RTC_PHASE_DATA_IN;
    }
}

uint8_t rtc_gpio_exchange(RTCState* rtc,
                          uint8_t cs, uint8_t sck, uint8_t sio_in,
                          uint8_t cs_rising, uint8_t cs_falling,
                          uint8_t sck_rising) {
    (void)sck; (void)cs;

    if (cs_falling) {
        rtc->phase = RTC_PHASE_IDLE;
        rtc->cmd_byte = 0;
        rtc->cmd_bits = 0;
        rtc->sio_out = 0;
        return rtc->sio_out;
    }

    if (cs_rising) {
        rtc->phase = RTC_PHASE_CMD;
        rtc->cmd_byte = 0;
        rtc->cmd_bits = 0;
        return rtc->sio_out;
    }

    if (!sck_rising || rtc->phase == RTC_PHASE_IDLE || rtc->phase == RTC_PHASE_STALL) {
        return rtc->sio_out;
    }

    switch (rtc->phase) {
    case RTC_PHASE_CMD:
        /* Shift MSB-first. */
        rtc->cmd_byte = (uint8_t)((rtc->cmd_byte << 1) | (sio_in & 1));
        rtc->cmd_bits++;
        if (rtc->cmd_bits == 8) rtc_decode_command(rtc);
        break;

    case RTC_PHASE_DATA_OUT:
        /* Drive next payload bit LSB-first within each byte. */
        if (rtc->payload_byte < rtc->payload_len) {
            rtc->sio_out = (rtc->payload[rtc->payload_byte] >> rtc->payload_bit) & 1;
            rtc->payload_bit++;
            if (rtc->payload_bit == 8) {
                rtc->payload_bit = 0;
                rtc->payload_byte++;
            }
        } else {
            rtc->sio_out = 0;
        }
        break;

    case RTC_PHASE_DATA_IN:
        if (rtc->payload_byte < rtc->payload_len) {
            rtc->payload[rtc->payload_byte] |= (uint8_t)((sio_in & 1) << rtc->payload_bit);
            rtc->payload_bit++;
            if (rtc->payload_bit == 8) {
                rtc->payload_bit = 0;
                rtc->payload_byte++;
                if (rtc->payload_byte == rtc->payload_len) {
                    uint8_t reg = BITS(rtc->cmd_byte, 3, 1);
                    rtc_commit_write(rtc, reg);
                    rtc->phase = RTC_PHASE_STALL;
                }
            }
        }
        break;

    default:
        break;
    }

    return rtc->sio_out;
}
