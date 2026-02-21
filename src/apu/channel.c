#include "apu.h"

// Legacy GB sound channels

void square_channel_tick(SquareChannel* ch) {
    // TODO: Advance frequency timer, output duty cycle waveform
}

void square_channel_sweep(SquareChannel* ch) {
    // TODO: Channel 1 only — frequency sweep
}

void square_channel_envelope(SquareChannel* ch) {
    // TODO: Volume envelope tick
}

void wave_channel_tick(WaveChannel* ch) {
    // TODO: Read from wave RAM, output sample
}

void noise_channel_tick(NoiseChannel* ch) {
    // TODO: Advance LFSR, output noise
}

void noise_channel_envelope(NoiseChannel* ch) {
    // TODO: Volume envelope tick
}
