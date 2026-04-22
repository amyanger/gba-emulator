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

/* --- BCD + time helpers --------------------------------------------------- */

static uint8_t to_bcd(int v) {
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static int from_bcd(uint8_t b) {
    return ((b >> 4) * 10) + (b & 0x0F);
}

/* Decode an hour byte per the current 24H/12H mode flag (status bit 6).
 * 24H mode: plain BCD 0..23 (upper bits masked).
 * 12H mode: BCD 1..12 with bit 7 = PM. */
static int decode_hour(const RTCState* rtc, uint8_t hb) {
    if (rtc->status_reg & 0x40) {
        return from_bcd((uint8_t)(hb & 0x3F));
    }
    int h12 = from_bcd((uint8_t)(hb & 0x1F));
    if (hb & 0x80) {
        if (h12 != 12) h12 += 12;
    } else {
        if (h12 == 12) h12 = 0;
    }
    return h12;
}

/* Fill payload[0..6] with the 7-byte DateTime record for the given broken-down
 * time, honoring the 24H/12H status bit. */
static void rtc_write_datetime_payload(RTCState* rtc, const struct tm* t) {
    int hour = t->tm_hour;
    uint8_t hour_byte;
    if (rtc->status_reg & 0x40) {
        hour_byte = to_bcd(hour);
    } else {
        int h12 = hour % 12;
        if (h12 == 0) h12 = 12;
        hour_byte = to_bcd(h12);
        if (hour >= 12) hour_byte |= 0x80;
    }
    rtc->payload[0] = to_bcd(t->tm_year - 100);
    rtc->payload[1] = to_bcd(t->tm_mon + 1);
    rtc->payload[2] = to_bcd(t->tm_mday);
    rtc->payload[3] = to_bcd(t->tm_wday);
    rtc->payload[4] = hour_byte;
    rtc->payload[5] = to_bcd(t->tm_min);
    rtc->payload[6] = to_bcd(t->tm_sec);
}

/* Pre-load payload bytes for a READ command (reg 1/2/4/5/6). */
static void rtc_fill_payload_for_read(RTCState* rtc, uint8_t reg) {
    time_t now = g_time_source(NULL);
    time_t effective = now + (time_t)rtc->offset_secs;
    struct tm t = *localtime(&effective);

    switch (reg) {
    case 1: /* Status */
        rtc->payload[0] = rtc->status_reg;
        break;
    case 2: /* DateTime (7 bytes) */
        rtc_write_datetime_payload(rtc, &t);
        break;
    case 4: /* Time only (3 bytes: hour, min, sec) */
        if (rtc->status_reg & 0x40) {
            rtc->payload[0] = to_bcd(t.tm_hour);
        } else {
            int h12 = t.tm_hour % 12;
            if (h12 == 0) h12 = 12;
            rtc->payload[0] = (uint8_t)(to_bcd(h12) | (t.tm_hour >= 12 ? 0x80 : 0));
        }
        rtc->payload[1] = to_bcd(t.tm_min);
        rtc->payload[2] = to_bcd(t.tm_sec);
        break;
    case 5: /* Alarm 1 — stubbed: return zeros */
    case 6: /* Alarm 2 — stubbed: return zeros */
        memset(rtc->payload, 0, 3);
        break;
    default:
        break;
    }
}

/* Commit the assembled payload (or no-payload command) back into RTC state. */
static void rtc_commit_write(RTCState* rtc, uint8_t reg) {
    switch (reg) {
    case 0: /* Force Reset: clears status to POWER-on and zeroes the offset. */
        rtc->status_reg = 0x80;
        rtc->offset_secs = 0;
        break;

    case 1: /* Status: bit 7 (POWER) is sticky-on and cleared by the write. */
        rtc->status_reg = (uint8_t)(rtc->payload[0] & 0x7F);
        break;

    case 2: { /* DateTime: rebuild struct tm, compute signed offset vs host. */
        struct tm t = {0};
        t.tm_year = from_bcd(rtc->payload[0]) + 100;
        t.tm_mon  = from_bcd(rtc->payload[1]) - 1;
        t.tm_mday = from_bcd(rtc->payload[2]);
        t.tm_hour = decode_hour(rtc, rtc->payload[4]);
        t.tm_min  = from_bcd(rtc->payload[5]);
        t.tm_sec  = from_bcd(rtc->payload[6]);
        t.tm_isdst = -1;
        time_t written = mktime(&t);
        time_t now = g_time_source(NULL);
        rtc->offset_secs = (int64_t)written - (int64_t)now;
        break;
    }

    case 3: /* Force IRQ: acknowledged, no state change. */
        break;

    case 4: { /* Time only: preserve current effective date, slide time-of-day. */
        time_t now = g_time_source(NULL);
        time_t eff = now + (time_t)rtc->offset_secs;
        struct tm t = *localtime(&eff);
        t.tm_hour = decode_hour(rtc, rtc->payload[0]);
        t.tm_min  = from_bcd(rtc->payload[1]);
        t.tm_sec  = from_bcd(rtc->payload[2]);
        t.tm_isdst = -1;
        time_t written = mktime(&t);
        rtc->offset_secs = (int64_t)written - (int64_t)now;
        break;
    }

    case 5:
    case 6: /* Alarms — stubbed: accept write but ignore. */
        break;

    default:
        break;
    }
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
