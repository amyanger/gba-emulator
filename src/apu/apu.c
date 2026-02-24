#include "apu.h"
#include "memory/dma.h"

void apu_init(APU* apu) {
    memset(apu, 0, sizeof(APU));
    apu->soundbias = 0x200;
    apu->sample_period = DEFAULT_SAMPLE_PERIOD;
}

/* --- FIFO operations --- */

void apu_fifo_write(APU* apu, int fifo_id, uint32_t data) {
    FIFO* fifo = (fifo_id == 0) ? &apu->fifo_a : &apu->fifo_b;

    for (int i = 0; i < 4; i++) {
        /* Per hardware: writing to a full FIFO resets it to empty */
        if (fifo->count >= FIFO_SIZE) {
            fifo_reset(fifo);
        }
        fifo->buffer[fifo->write_idx] = (int8_t)(data >> (i * 8));
        fifo->write_idx = (fifo->write_idx + 1) % FIFO_SIZE;
        fifo->count++;
    }
}

int8_t apu_fifo_pop(APU* apu, int fifo_id) {
    FIFO* fifo = (fifo_id == 0) ? &apu->fifo_a : &apu->fifo_b;

    if (fifo->count == 0) return fifo->last_sample;

    int8_t sample = fifo->buffer[fifo->read_idx];
    fifo->read_idx = (fifo->read_idx + 1) % FIFO_SIZE;
    fifo->count--;
    fifo->last_sample = sample;
    return sample;
}

/* --- Timer overflow callback (drives FIFO playback) --- */

void apu_on_timer_overflow(APU* apu, int timer_id) {
    /* FIFO A */
    if (apu->fifo_a.timer_id == (uint8_t)timer_id) {
        apu->fifo_a_latch = apu_fifo_pop(apu, 0);
        if (apu->fifo_a.count <= 16 && apu->dma) {
            dma_on_fifo(apu->dma, 0);
        }
    }

    /* FIFO B */
    if (apu->fifo_b.timer_id == (uint8_t)timer_id) {
        apu->fifo_b_latch = apu_fifo_pop(apu, 1);
        if (apu->fifo_b.count <= 16 && apu->dma) {
            dma_on_fifo(apu->dma, 1);
        }
    }
}

/* --- Sample mixing --- */

static void apu_mix_sample(APU* apu) {
    int32_t left = 0;
    int32_t right = 0;

    uint16_t cnt_l = apu->soundcnt_l;
    uint16_t cnt_h = apu->soundcnt_h;

    /* --- Legacy channel mixing --- */
    int32_t ch_out[4] = { 0, 0, 0, 0 };

    if (apu->ch1.enabled) {
        static const uint8_t duty_table[4][8] = {
            { 0, 0, 0, 0, 0, 0, 0, 1 },  /* 12.5% */
            { 1, 0, 0, 0, 0, 0, 0, 1 },  /* 25% */
            { 1, 0, 0, 0, 0, 1, 1, 1 },  /* 50% */
            { 0, 1, 1, 1, 1, 1, 1, 0 },  /* 75% */
        };
        ch_out[0] = duty_table[apu->ch1.duty_cycle][apu->ch1.duty_pos]
                  ? apu->ch1.volume : 0;
    }
    if (apu->ch2.enabled) {
        static const uint8_t duty_table[4][8] = {
            { 0, 0, 0, 0, 0, 0, 0, 1 },
            { 1, 0, 0, 0, 0, 0, 0, 1 },
            { 1, 0, 0, 0, 0, 1, 1, 1 },
            { 0, 1, 1, 1, 1, 1, 1, 0 },
        };
        ch_out[1] = duty_table[apu->ch2.duty_cycle][apu->ch2.duty_pos]
                  ? apu->ch2.volume : 0;
    }
    if (apu->ch3.enabled) {
        uint8_t byte_idx = apu->ch3.wave_pos / 2;
        uint8_t sample;
        if (apu->ch3.wave_pos & 1) {
            sample = apu->ch3.wave_ram[byte_idx] & 0x0F;
        } else {
            sample = apu->ch3.wave_ram[byte_idx] >> 4;
        }
        /* Volume shift: 0=mute, 1=100%, 2=50%, 3=25% */
        switch (apu->ch3.volume_code) {
        case 0: sample = 0; break;
        case 1: break;
        case 2: sample >>= 1; break;
        case 3: sample >>= 2; break;
        }
        if (apu->ch3.force_volume) {
            sample = (sample * 3) >> 2;
        }
        ch_out[2] = sample;
    }
    if (apu->ch4.enabled) {
        ch_out[3] = (apu->ch4.lfsr & 1) ? 0 : apu->ch4.volume;
    }

    /* Mix legacy channels to L/R per SOUNDCNT_L enable bits */
    int32_t legacy_left = 0;
    int32_t legacy_right = 0;
    for (int i = 0; i < 4; i++) {
        if (BIT(cnt_l, 12 + i)) legacy_left += ch_out[i];   /* Left enable */
        if (BIT(cnt_l, 8 + i))  legacy_right += ch_out[i];  /* Right enable */
    }

    /* Apply master volume (0-7) */
    uint8_t vol_left = BITS(cnt_l, 6, 4);
    uint8_t vol_right = BITS(cnt_l, 2, 0);
    legacy_left = (legacy_left * (vol_left + 1)) / 8;
    legacy_right = (legacy_right * (vol_right + 1)) / 8;

    /* Apply legacy volume ratio from SOUNDCNT_H bits 0-1 */
    uint8_t legacy_ratio = cnt_h & 3;
    switch (legacy_ratio) {
    case 0: legacy_left >>= 2; legacy_right >>= 2; break; /* 25% */
    case 1: legacy_left >>= 1; legacy_right >>= 1; break; /* 50% */
    case 2: break;                                          /* 100% */
    default: break;                                         /* prohibited, treat as 100% */
    }

    left += legacy_left;
    right += legacy_right;

    /* --- FIFO mixing --- */
    int32_t fifo_a = (int32_t)apu->fifo_a_latch;
    int32_t fifo_b = (int32_t)apu->fifo_b_latch;

    /* FIFO volume: bit 2 = FIFO A (0=50%, 1=100%), bit 3 = FIFO B.
     * Scale int8 samples to signed 10-bit range to match PSG output:
     * 100% = shift left 2, 50% = shift left 1. */
    fifo_a <<= BIT(cnt_h, 2) ? 2 : 1;
    fifo_b <<= BIT(cnt_h, 3) ? 2 : 1;

    /* FIFO routing */
    if (BIT(cnt_h, 9))  left += fifo_a;   /* FIFO A to left */
    if (BIT(cnt_h, 8))  right += fifo_a;  /* FIFO A to right */
    if (BIT(cnt_h, 13)) left += fifo_b;   /* FIFO B to left */
    if (BIT(cnt_h, 12)) right += fifo_b;  /* FIFO B to right */

    /* --- Apply SOUNDBIAS and clamp --- */
    uint16_t bias = (apu->soundbias >> 1) & 0x1FF;

    left += (int32_t)bias;
    right += (int32_t)bias;

    /* Clamp to 10-bit unsigned range */
    if (left < 0) left = 0;
    if (left > 0x3FF) left = 0x3FF;
    if (right < 0) right = 0;
    if (right > 0x3FF) right = 0x3FF;

    /* Convert 10-bit unsigned (centered at bias) to int16_t for SDL.
     * Signed range is [-bias, 0x3FF-bias]. With default bias 0x100,
     * that's [-256, +767]. Scale by 32 to fill int16_t without overflow. */
    int32_t left_signed = (left - (int32_t)bias) * 32;
    int32_t right_signed = (right - (int32_t)bias) * 32;
    if (left_signed > 32767) left_signed = 32767;
    if (left_signed < -32768) left_signed = -32768;
    if (right_signed > 32767) right_signed = 32767;
    if (right_signed < -32768) right_signed = -32768;
    int16_t left_s16 = (int16_t)left_signed;
    int16_t right_s16 = (int16_t)right_signed;

    /* Single-pole IIR low-pass filter (alpha=0.75, cutoff ~10 kHz @ 32768 Hz).
     * Smooths the staircase from FIFO's ~13 kHz update rate being sampled at
     * 32 kHz output rate — mimics the hardware DAC's analog RC filtering. */
    left_s16 = (int16_t)((apu->prev_left + 3 * (int32_t)left_s16) / 4);
    right_s16 = (int16_t)((apu->prev_right + 3 * (int32_t)right_s16) / 4);
    apu->prev_left = left_s16;
    apu->prev_right = right_s16;

    /* Write to ring buffer (leave 1-slot gap to distinguish full from empty) */
    uint32_t pos = apu->write_pos;
    uint32_t next_pos = (pos + 1) % SAMPLE_BUFFER_SIZE;
    if (next_pos == apu->read_pos) {
        return; /* Buffer full, drop this sample */
    }
    apu->sample_buffer[pos * 2] = left_s16;
    apu->sample_buffer[pos * 2 + 1] = right_s16;
    apu->write_pos = next_pos;
}

/* --- Main APU tick --- */

void apu_tick(APU* apu, int cycles) {
    /* Master enable check */
    if (!BIT(apu->soundcnt_x, 7)) {
        return;
    }

    /* Sample generation always at 32768 Hz (period=512) to match SDL output.
     * SOUNDBIAS bits 14-15 control the hardware DAC rate on real hardware,
     * but for emulation we fix the rate to avoid sample rate mismatches. */

    /* Advance frame sequencer (512 Hz, drives legacy channel modulation) */
    apu->frame_seq_timer += (uint32_t)cycles;
    while (apu->frame_seq_timer >= FRAME_SEQ_PERIOD) {
        apu->frame_seq_timer -= FRAME_SEQ_PERIOD;

        uint8_t step = apu->frame_seq_step;

        /* Length counters: steps 0, 2, 4, 6 (256 Hz) */
        if ((step & 1) == 0) {
            square_channel_length_tick(&apu->ch1);
            square_channel_length_tick(&apu->ch2);
            wave_channel_length_tick(&apu->ch3);
            noise_channel_length_tick(&apu->ch4);
        }

        /* Sweep (Ch1 only): steps 2, 6 (128 Hz) */
        if (step == 2 || step == 6) {
            square_channel_sweep(&apu->ch1);
        }

        /* Envelope: step 7 (64 Hz) */
        if (step == 7) {
            square_channel_envelope(&apu->ch1);
            square_channel_envelope(&apu->ch2);
            noise_channel_envelope(&apu->ch4);
        }

        apu->frame_seq_step = (step + 1) & 7;
    }

    /* Tick legacy channel frequency timers */
    square_channel_tick(&apu->ch1, cycles);
    square_channel_tick(&apu->ch2, cycles);
    wave_channel_tick(&apu->ch3, cycles);
    noise_channel_tick(&apu->ch4, cycles);

    /* Generate output samples at rate determined by SOUNDBIAS */
    apu->sample_timer += (uint32_t)cycles;
    while (apu->sample_timer >= apu->sample_period) {
        apu->sample_timer -= apu->sample_period;
        apu_mix_sample(apu);
    }
}
