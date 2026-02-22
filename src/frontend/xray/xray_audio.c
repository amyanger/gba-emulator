#include "xray.h"
#include "xray_draw.h"
#include "apu/apu.h"

void xray_capture_audio(APU* apu, XRayState* state) {
    if (!state || !state->active) return;

    /* Snapshot the last XRAY_AUDIO_SNAP samples from the ring buffer.
     * The ring buffer is stereo interleaved (L, R, L, R...). */
    uint32_t wp = apu->write_pos;
    uint32_t count = XRAY_AUDIO_SNAP;
    if (count > SAMPLE_BUFFER_SIZE) count = SAMPLE_BUFFER_SIZE;

    state->audio_snapshot_count = count;

    for (uint32_t i = 0; i < count; i++) {
        /* Walk backwards from write position */
        uint32_t idx = (wp + SAMPLE_BUFFER_SIZE - count + i) % SAMPLE_BUFFER_SIZE;
        state->audio_snapshot[i * 2] = apu->sample_buffer[idx * 2];
        state->audio_snapshot[i * 2 + 1] = apu->sample_buffer[idx * 2 + 1];
    }
}

/* Draw a waveform oscilloscope trace */
static void draw_waveform(uint32_t* buf, int buf_w, int buf_h,
                          int x, int y, int w, int h,
                          const int16_t* samples, uint32_t count,
                          int stride, int offset, uint32_t color) {
    /* Background */
    xray_draw_rect(buf, buf_w, buf_h, x, y, w, h, 0xFF0A0A1E);
    xray_draw_rect_outline(buf, buf_w, buf_h, x, y, w, h, XRAY_COL_BORDER);

    /* Center line */
    xray_draw_hline(buf, buf_w, buf_h, x, y + h / 2, w, 0xFF222244);

    if (count == 0) return;

    /* Map samples to pixel coordinates */
    int prev_py = -1;
    for (int px = 0; px < w; px++) {
        uint32_t sample_idx = (uint32_t)px * count / (uint32_t)w;
        if (sample_idx >= count) sample_idx = count - 1;

        int16_t sample = samples[sample_idx * stride + offset];

        /* Map [-32768, 32767] to [0, h-1] */
        int sy = h / 2 - (int)((int32_t)sample * (h / 2) / 32768);
        if (sy < 0) sy = 0;
        if (sy >= h) sy = h - 1;

        int draw_y = y + sy;
        if (draw_y >= 0 && draw_y < buf_h && (x + px) >= 0 &&
            (x + px) < buf_w) {
            buf[draw_y * buf_w + (x + px)] = color;

            /* Connect to previous point for smooth line */
            if (prev_py >= 0) {
                int y0 = prev_py < draw_y ? prev_py : draw_y;
                int y1 = prev_py > draw_y ? prev_py : draw_y;
                for (int cy = y0; cy <= y1; cy++) {
                    if (cy >= 0 && cy < buf_h) {
                        buf[cy * buf_w + (x + px)] = color;
                    }
                }
            }
            prev_py = draw_y;
        }
    }
}

/* Draw a duty cycle pattern for square channels */
static void draw_duty_indicator(uint32_t* buf, int buf_w, int buf_h,
                                int x, int y, uint8_t duty, uint32_t color) {
    /* Duty patterns: 12.5%, 25%, 50%, 75% */
    static const uint8_t patterns[4][8] = {
        {0, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 1, 1, 1},
        {0, 1, 1, 1, 1, 1, 1, 0},
    };
    if (duty > 3) duty = 0;

    int pw = 4; /* pixels per step */
    int ph = 10;
    for (int i = 0; i < 8; i++) {
        int level = patterns[duty][i] ? 0 : ph - 2;
        xray_draw_rect(buf, buf_w, buf_h, x + i * pw, y + level, pw, 2,
                       color);
        /* Vertical edge between transitions */
        if (i > 0 && patterns[duty][i] != patterns[duty][i - 1]) {
            xray_draw_vline(buf, buf_w, buf_h, x + i * pw, y, ph, color);
        }
    }
}

void xray_render_audio(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                       int pw, int ph, APU* apu, XRayState* state) {
    (void)pw;
    (void)ph;

    int x0 = px + 8;
    int y = py + 18;

    bool master_on = BIT(apu->soundcnt_x, 7);

    /* === Master Output Waveform === */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "Master Output", XRAY_COL_HEADER);
    xray_draw_text(buf, buf_w, buf_h, x0 + 120, y,
                   master_on ? "ON" : "OFF",
                   master_on ? XRAY_COL_VALUE : XRAY_COL_DIM);
    y += 12;

    int wave_w = 300;
    int wave_h = 40;

    /* Left channel */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "L", XRAY_COL_LABEL);
    draw_waveform(buf, buf_w, buf_h, x0 + 12, y, wave_w, wave_h,
                  state->audio_snapshot, state->audio_snapshot_count,
                  2, 0, 0xFF44FF44);
    y += wave_h + 4;

    /* Right channel */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "R", XRAY_COL_LABEL);
    draw_waveform(buf, buf_w, buf_h, x0 + 12, y, wave_w, wave_h,
                  state->audio_snapshot, state->audio_snapshot_count,
                  2, 1, 0xFF4488FF);
    y += wave_h + 8;

    /* === FIFO Status === */
    int fifo_x = x0 + 340;
    int fifo_y = py + 18;

    xray_draw_text(buf, buf_w, buf_h, fifo_x, fifo_y, "FIFO A",
                   XRAY_COL_HEADER);
    fifo_y += 12;
    xray_draw_textf(buf, buf_w, buf_h, fifo_x, fifo_y, XRAY_COL_LABEL,
                    "Count: %d/32  Timer: %d", apu->fifo_a.count,
                    apu->fifo_a.timer_id);
    fifo_y += 11;
    xray_draw_textf(buf, buf_w, buf_h, fifo_x, fifo_y, XRAY_COL_LABEL,
                    "Latch: %d", (int)apu->fifo_a_latch);
    fifo_y += 11;
    xray_draw_fill_bar(buf, buf_w, buf_h, fifo_x, fifo_y, 120, 10,
                       (float)apu->fifo_a.count / FIFO_SIZE,
                       0xFF44AAFF, 0xFF0A0A2E);
    fifo_y += 16;

    xray_draw_text(buf, buf_w, buf_h, fifo_x, fifo_y, "FIFO B",
                   XRAY_COL_HEADER);
    fifo_y += 12;
    xray_draw_textf(buf, buf_w, buf_h, fifo_x, fifo_y, XRAY_COL_LABEL,
                    "Count: %d/32  Timer: %d", apu->fifo_b.count,
                    apu->fifo_b.timer_id);
    fifo_y += 11;
    xray_draw_textf(buf, buf_w, buf_h, fifo_x, fifo_y, XRAY_COL_LABEL,
                    "Latch: %d", (int)apu->fifo_b_latch);
    fifo_y += 11;
    xray_draw_fill_bar(buf, buf_w, buf_h, fifo_x, fifo_y, 120, 10,
                       (float)apu->fifo_b.count / FIFO_SIZE,
                       0xFFFF88AA, 0xFF0A0A2E);

    /* === Legacy Channels === */
    xray_draw_text(buf, buf_w, buf_h, x0, y, "Channels", XRAY_COL_HEADER);
    y += 12;

    /* Channel 1 (Square + Sweep) */
    uint32_t ch1_col = apu->ch1.enabled ? XRAY_COL_VALUE : XRAY_COL_DIM;
    xray_draw_textf(buf, buf_w, buf_h, x0, y, ch1_col,
                    "CH1 Sq+Sw  Vol:%2d  Freq:%4d",
                    apu->ch1.volume, apu->ch1.frequency);
    draw_duty_indicator(buf, buf_w, buf_h, x0 + 260, y, apu->ch1.duty_cycle,
                        ch1_col);
    y += 12;

    /* Channel 2 (Square) */
    uint32_t ch2_col = apu->ch2.enabled ? XRAY_COL_VALUE : XRAY_COL_DIM;
    xray_draw_textf(buf, buf_w, buf_h, x0, y, ch2_col,
                    "CH2 Square Vol:%2d  Freq:%4d",
                    apu->ch2.volume, apu->ch2.frequency);
    draw_duty_indicator(buf, buf_w, buf_h, x0 + 260, y, apu->ch2.duty_cycle,
                        ch2_col);
    y += 12;

    /* Channel 3 (Wave) */
    uint32_t ch3_col = apu->ch3.enabled ? XRAY_COL_VALUE : XRAY_COL_DIM;
    const char* vol_str[] = {"0%", "100%", "50%", "25%"};
    uint8_t vc = apu->ch3.volume_code;
    if (vc > 3) vc = 0;
    xray_draw_textf(buf, buf_w, buf_h, x0, y, ch3_col,
                    "CH3 Wave   Vol:%s  Freq:%4d  Pos:%2d",
                    vol_str[vc], apu->ch3.frequency, apu->ch3.wave_pos);
    y += 12;

    /* Channel 4 (Noise) */
    uint32_t ch4_col = apu->ch4.enabled ? XRAY_COL_VALUE : XRAY_COL_DIM;
    xray_draw_textf(buf, buf_w, buf_h, x0, y, ch4_col,
                    "CH4 Noise  Vol:%2d  LFSR:%04X  %s",
                    apu->ch4.volume, apu->ch4.lfsr,
                    apu->ch4.width_mode ? "7-bit" : "15-bit");
}
