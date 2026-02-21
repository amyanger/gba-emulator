#ifndef APU_H
#define APU_H

#include "common.h"

#define SAMPLE_BUFFER_SIZE 4096
#define FIFO_SIZE 32

typedef struct {
    int8_t buffer[FIFO_SIZE];
    uint8_t read_idx;
    uint8_t write_idx;
    uint8_t count;
    uint8_t timer_id; // Which timer drives playback (0 or 1)
} FIFO;

typedef struct {
    bool enabled;
    uint16_t length_counter;
    bool length_enable;
    uint16_t frequency;
    uint32_t freq_timer;
    uint8_t duty_cycle;   // 0-3
    uint8_t duty_pos;     // 0-7
    uint8_t volume;
    uint8_t vol_period;
    bool vol_dir;         // 0=decrease, 1=increase
    uint8_t vol_timer;
    // Channel 1 sweep
    uint8_t sweep_period;
    bool sweep_dir;
    uint8_t sweep_shift;
    uint8_t sweep_timer;
    uint16_t sweep_freq;
    bool sweep_enabled;
} SquareChannel;

typedef struct {
    bool enabled;
    uint16_t length_counter;
    bool length_enable;
    uint16_t frequency;
    uint32_t freq_timer;
    uint8_t wave_ram[16]; // 32 4-bit samples
    uint8_t wave_pos;
    uint8_t volume_code;  // 0=0%, 1=100%, 2=50%, 3=25%
    bool bank_mode;
    uint8_t bank_select;
    bool force_volume;
} WaveChannel;

typedef struct {
    bool enabled;
    uint16_t length_counter;
    bool length_enable;
    uint8_t volume;
    uint8_t vol_period;
    bool vol_dir;
    uint8_t vol_timer;
    uint16_t lfsr;        // Linear feedback shift register
    bool width_mode;      // false=15-bit, true=7-bit
    uint8_t divisor_code;
    uint8_t shift;
    uint32_t freq_timer;
} NoiseChannel;

struct APU {
    // Legacy GB channels
    SquareChannel ch1;
    SquareChannel ch2;
    WaveChannel ch3;
    NoiseChannel ch4;

    // DirectSound FIFOs
    FIFO fifo_a;
    FIFO fifo_b;

    // Control registers
    uint16_t soundcnt_l;
    uint16_t soundcnt_h;
    uint16_t soundcnt_x;
    uint16_t soundbias;

    // Frame sequencer
    uint8_t frame_seq_step;
    uint32_t frame_seq_timer;

    // Sample ring buffer (stereo interleaved: L, R, L, R...)
    int16_t sample_buffer[SAMPLE_BUFFER_SIZE * 2];
    uint32_t write_pos;
    uint32_t read_pos;
};
typedef struct APU APU;

void apu_init(APU* apu);
void apu_tick(APU* apu, int cycles);
void apu_fifo_write(APU* apu, int fifo_id, uint32_t data);
int8_t apu_fifo_pop(APU* apu, int fifo_id);
void apu_on_timer_overflow(APU* apu, int timer_id);

#endif // APU_H
