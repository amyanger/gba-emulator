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

    /* A write that doesn't fit is dropped (matches mGBA).  Resetting the
     * FIFO here would discard ~32 queued samples — an audible dropout
     * whenever the game briefly outruns the DMA drain. */
    if (fifo->count > FIFO_SIZE - 4) {
        return;
    }

    for (int i = 0; i < 4; i++) {
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

/* Append one stereo sample pair to the ring buffer (drops when full;
 * a 1-slot gap distinguishes full from empty). */
static void apu_push_sample(APU* apu, int16_t left, int16_t right) {
    /* Counted before the full check: every generated sample advances the
     * APU clock, including any the ring drops. Both push paths go here. */
    apu->samples_produced++;

    uint32_t pos = apu->write_pos;
    uint32_t next_pos = (pos + 1) % SAMPLE_BUFFER_SIZE;
    if (next_pos == apu->read_pos) {
        return; /* Buffer full, drop this sample */
    }
    apu->sample_buffer[pos * 2] = left;
    apu->sample_buffer[pos * 2 + 1] = right;
    apu->write_pos = next_pos;
}

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

    /* GBATEK: each PSG spans a quarter of the output range (+/-80h) and
     * the FIFOs the full range. Scaled like mGBA: x8, times master volume
     * (0-7) + 1, then SOUNDCNT_H bits 0-1 (0=25%, 1=50%, 2=100%, 3 is
     * prohibited, treated as 100%). One channel at volume 15 reaches 240. */
    uint8_t vol_left = BITS(cnt_l, 6, 4);
    uint8_t vol_right = BITS(cnt_l, 2, 0);
    uint8_t legacy_ratio = cnt_h & 3;
    int psg_shift = 4 - (legacy_ratio == 3 ? 2 : legacy_ratio);
    legacy_left = (legacy_left * 8 * (vol_left + 1)) >> psg_shift;
    legacy_right = (legacy_right * 8 * (vol_right + 1)) >> psg_shift;

    left += legacy_left;
    right += legacy_right;

    /* --- FIFO mixing --- */
    int32_t fifo_a = (int32_t)apu->fifo_a_latch;
    int32_t fifo_b = (int32_t)apu->fifo_b_latch;

    /* FIFO volume: bit 2 = FIFO A (0=50%, 1=100%), bit 3 = FIFO B.
     * Scale int8 samples to signed 10-bit range to match PSG output:
     * 100% = x4, 50% = x2. */
    fifo_a *= BIT(cnt_h, 2) ? 4 : 2;
    fifo_b *= BIT(cnt_h, 3) ? 4 : 2;

    /* FIFO routing */
    if (!apu->fifo_omit) {
        if (BIT(cnt_h, 9))  left += fifo_a;   /* FIFO A to left */
        if (BIT(cnt_h, 8))  right += fifo_a;  /* FIFO A to right */
        if (BIT(cnt_h, 13)) left += fifo_b;   /* FIFO B to left */
        if (BIT(cnt_h, 12)) right += fifo_b;  /* FIFO B to right */
    }

    /* --- Apply SOUNDBIAS and clamp ---
     * GBATEK: bits 1-9 hold the bias in place (bit 0 reads as 0), so the
     * default 200h is added to the signed mix unshifted. */
    uint16_t bias = apu->soundbias & 0x3FE;

    left += (int32_t)bias;
    right += (int32_t)bias;

    /* Clamp to 10-bit unsigned range */
    if (left < 0) left = 0;
    if (left > 0x3FF) left = 0x3FF;
    if (right < 0) right = 0;
    if (right > 0x3FF) right = 0x3FF;

    /* Convert 10-bit unsigned (centered at bias) to int16_t for SDL.
     * Signed range is [-bias, 0x3FF-bias]. With default bias 0x200,
     * that's [-512, +511]. Scale by 32, leaving headroom in int16_t. */
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

    apu_push_sample(apu, left_s16, right_s16);
}

/* --- Main APU tick --- */

void apu_tick(APU* apu, int cycles) {
    /* Master enable check.  Even while disabled the APU must keep
     * producing (silent) samples at the normal rate — the SDL audio
     * queue is paced by this stream and would underrun otherwise. */
    if (!BIT(apu->soundcnt_x, 7)) {
        apu->sample_timer += (uint32_t)cycles;
        while (apu->sample_timer >= apu->sample_period) {
            apu->sample_timer -= apu->sample_period;
            apu_push_sample(apu, 0, 0);
        }
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
