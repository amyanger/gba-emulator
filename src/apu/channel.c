#include "apu.h"

/* ===== Square Channel (Ch1 and Ch2) ===== */

void square_channel_tick(SquareChannel* ch, int cycles) {
    if (!ch->enabled) return;

    ch->freq_timer += (uint32_t)cycles;
    uint32_t period = (2048 - ch->frequency) * 16;
    if (period == 0) period = 16;

    while (ch->freq_timer >= period) {
        ch->freq_timer -= period;
        ch->duty_pos = (ch->duty_pos + 1) & 7;
    }
}

void square_channel_sweep(SquareChannel* ch) {
    if (!ch->sweep_enabled) return;
    if (ch->sweep_period == 0) return;

    if (ch->sweep_timer > 0) {
        ch->sweep_timer--;
    }

    if (ch->sweep_timer == 0) {
        ch->sweep_timer = ch->sweep_period;

        if (ch->sweep_shift > 0) {
            uint16_t delta = ch->sweep_freq >> ch->sweep_shift;
            uint16_t new_freq;

            if (ch->sweep_dir) {
                /* Decrease -- per GBATEK, overflow check is NOT performed */
                if (delta > ch->sweep_freq) {
                    new_freq = 0;
                } else {
                    new_freq = ch->sweep_freq - delta;
                }
            } else {
                /* Increase */
                new_freq = ch->sweep_freq + delta;
                if (new_freq > 2047) {
                    ch->enabled = false;
                    return;
                }
            }

            ch->sweep_freq = new_freq;
            ch->frequency = new_freq;

            /* Secondary overflow check (increase only, per GBATEK) */
            if (!ch->sweep_dir) {
                uint16_t check_delta = new_freq >> ch->sweep_shift;
                uint16_t check_freq = new_freq + check_delta;
                if (check_freq > 2047) {
                    ch->enabled = false;
                }
            }
        }
    }
}

void square_channel_envelope(SquareChannel* ch) {
    if (ch->vol_period == 0) return;

    if (ch->vol_timer > 0) {
        ch->vol_timer--;
    }

    if (ch->vol_timer == 0) {
        ch->vol_timer = ch->vol_period;

        if (ch->vol_dir && ch->volume < 15) {
            ch->volume++;
        } else if (!ch->vol_dir && ch->volume > 0) {
            ch->volume--;
        }
    }
}

void square_channel_length_tick(SquareChannel* ch) {
    if (ch->length_enable && ch->length_counter > 0) {
        ch->length_counter--;
        if (ch->length_counter == 0) {
            ch->enabled = false;
        }
    }
}

void square_channel_trigger(SquareChannel* ch, bool has_sweep) {
    ch->enabled = true;

    /* Reload length counter if zero */
    if (ch->length_counter == 0) {
        ch->length_counter = 64;
    }

    /* Reload frequency timer */
    ch->freq_timer = 0;

    /* Reload envelope */
    ch->vol_timer = ch->vol_period;
    /* Volume is set from the envelope initial volume on trigger */

    /* Sweep (Ch1 only) */
    if (has_sweep) {
        ch->sweep_freq = ch->frequency;
        ch->sweep_timer = ch->sweep_period;
        ch->sweep_enabled = (ch->sweep_period > 0 || ch->sweep_shift > 0);

        /* Overflow check on trigger */
        if (ch->sweep_shift > 0) {
            uint16_t delta = ch->sweep_freq >> ch->sweep_shift;
            uint16_t new_freq;
            if (ch->sweep_dir) {
                new_freq = ch->sweep_freq - delta;
            } else {
                new_freq = ch->sweep_freq + delta;
            }
            if (new_freq > 2047) {
                ch->enabled = false;
            }
        }
    }

    /* DAC check: if envelope initial volume is 0 and direction is decrease,
     * the DAC is effectively off. */
    if (ch->volume == 0 && !ch->vol_dir) {
        ch->enabled = false;
    }
}

/* ===== Wave Channel (Ch3) ===== */

void wave_channel_tick(WaveChannel* ch, int cycles) {
    if (!ch->enabled) return;

    ch->freq_timer += (uint32_t)cycles;
    uint32_t period = (2048 - ch->frequency) * 8;
    if (period == 0) period = 8;

    while (ch->freq_timer >= period) {
        ch->freq_timer -= period;
        ch->wave_pos = (ch->wave_pos + 1) & 31;
    }
}

void wave_channel_trigger(WaveChannel* ch) {
    ch->enabled = true;

    if (ch->length_counter == 0) {
        ch->length_counter = 256;
    }

    ch->freq_timer = 0;
    ch->wave_pos = 0;
}

void wave_channel_length_tick(WaveChannel* ch) {
    if (ch->length_enable && ch->length_counter > 0) {
        ch->length_counter--;
        if (ch->length_counter == 0) {
            ch->enabled = false;
        }
    }
}

/* ===== Noise Channel (Ch4) ===== */

void noise_channel_tick(NoiseChannel* ch, int cycles) {
    if (!ch->enabled) return;

    ch->freq_timer += (uint32_t)cycles;

    /* Calculate period from divisor code and shift */
    static const uint32_t divisors[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };
    uint32_t period = divisors[ch->divisor_code & 7] << ch->shift;
    if (period == 0) period = 8;

    while (ch->freq_timer >= period) {
        ch->freq_timer -= period;

        /* Advance LFSR */
        uint16_t bit = (ch->lfsr ^ (ch->lfsr >> 1)) & 1;
        ch->lfsr >>= 1;
        if (ch->width_mode) {
            /* 7-bit mode: set bit 6 */
            ch->lfsr = (ch->lfsr & ~(1u << 6)) | (bit << 6);
        } else {
            /* 15-bit mode: set bit 14 */
            ch->lfsr = (ch->lfsr & ~(1u << 14)) | (bit << 14);
        }
    }
}

void noise_channel_envelope(NoiseChannel* ch) {
    if (ch->vol_period == 0) return;

    if (ch->vol_timer > 0) {
        ch->vol_timer--;
    }

    if (ch->vol_timer == 0) {
        ch->vol_timer = ch->vol_period;

        if (ch->vol_dir && ch->volume < 15) {
            ch->volume++;
        } else if (!ch->vol_dir && ch->volume > 0) {
            ch->volume--;
        }
    }
}

void noise_channel_trigger(NoiseChannel* ch) {
    ch->enabled = true;

    if (ch->length_counter == 0) {
        ch->length_counter = 64;
    }

    ch->freq_timer = 0;
    ch->vol_timer = ch->vol_period;

    /* Initialize LFSR */
    ch->lfsr = ch->width_mode ? 0x7F : 0x7FFF;

    /* DAC check */
    if (ch->volume == 0 && !ch->vol_dir) {
        ch->enabled = false;
    }
}

void noise_channel_length_tick(NoiseChannel* ch) {
    if (ch->length_enable && ch->length_counter > 0) {
        ch->length_counter--;
        if (ch->length_counter == 0) {
            ch->enabled = false;
        }
    }
}
