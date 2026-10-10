#include "xray.h"
#include "apu/apu.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"

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

#define WAVE_W 300.0f
#define WAVE_H 46.0f

/* Oscilloscope trace of one stereo side of the snapshot. */
static void waveform(UiCanvas* c, float x, float y, const XRayState* s, int offset, uint32_t color) {
    ui_fill_round_rect(c, (UiRect){x, y, WAVE_W, WAVE_H}, 4.0f, UI_BG);
    ui_hline(c, x + 4.0f, y + WAVE_H / 2, WAVE_W - 8.0f, UI_DIVIDER);
    uint32_t n = s->audio_snapshot_count;
    if (n == 0) return;
    const int steps = 150;
    float px = 0, py = 0;
    for (int i = 0; i <= steps; i++) {
        uint32_t idx = (uint32_t)i * (n - 1) / (uint32_t)steps;
        float v = (float)s->audio_snapshot[idx * 2 + (uint32_t)offset] / 32768.0f;
        float cx = x + 3.0f + (WAVE_W - 6.0f) * (float)i / (float)steps;
        float cy = y + WAVE_H / 2 - v * (WAVE_H / 2 - 3.0f);
        if (i > 0) ui_capsule(c, px, py, cx, cy, 1.25f, color);
        px = cx;
        py = cy;
    }
}

static void fifo(UiCanvas* c, float x, float y, float w, const char* name, uint8_t count,
                 int8_t latch, uint8_t timer, uint32_t color) {
    ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_TEXT, x, y, w, UI_ALIGN_LEFT, UI_TEXT, name);
    xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_HEAD, x, y + 16.0f, w, UI_ALIGN_LEFT, UI_MUTED,
               "%u of 32 samples queued", (unsigned)count);
    ui_bar(c, (UiRect){x, y + 33.0f, w, 6.0f}, (float)count / FIFO_SIZE, color);
    xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_HEAD, x, y + 44.0f, w, UI_ALIGN_LEFT, UI_DIM,
               "Level %d \xC2\xB7 timer %u", (int)latch, (unsigned)timer);
}

static void head(UiCanvas* c, float x, float y, UiAlign a, const char* t) {
    ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_HEAD, x, y, 0, a, UI_DIM, t);
}

void xray_render_audio(UiCanvas* c, APU* apu, XRayState* s) {
    bool on = BIT(apu->soundcnt_x, 7);
    float top = xray_card(c, XRAY_CARD_HEAR, on ? "Sound on" : "Sound off");
    UiRect r = xray_cards[XRAY_CARD_HEAR];
    float x0 = r.x + XRAY_PAD;

    ui_section_label(c, x0, top, "LEFT SPEAKER");
    waveform(c, x0, top + 14.0f, s, 0, UI_GOOD);
    ui_section_label(c, x0, top + 66.0f, "RIGHT SPEAKER");
    waveform(c, x0, top + 80.0f, s, 1, UI_MALE);

    float fx = x0 + WAVE_W + 20.0f, fw = r.x + r.w - XRAY_PAD - fx;
    fifo(c, fx, top, fw, "Sample channel A", apu->fifo_a.count, apu->fifo_a_latch,
         apu->fifo_a.timer_id, UI_ACCENT);
    fifo(c, fx, top + 66.0f, fw, "Sample channel B", apu->fifo_b.count, apu->fifo_b_latch,
         apu->fifo_b.timer_id, UI_FEMALE);

    /* Retro (PSG) channels */
    float y = top + 140.0f;
    float cv = x0 + 170.0f, cf = x0 + 250.0f, cd = x0 + 270.0f, dw = r.x + r.w - XRAY_PAD - cd;
    ui_section_label(c, x0, y, "RETRO CHANNEL");
    head(c, cv, y, UI_ALIGN_RIGHT, "VOLUME");
    head(c, cf, y, UI_ALIGN_RIGHT, "PITCH");
    head(c, cd, y, UI_ALIGN_LEFT, "DETAIL");
    y += XRAY_ROW;

    static const char* duty[4] = {"12.5%", "25%", "50%", "75%"};
    static const char* wave_vol[4] = {"0%", "100%", "50%", "25%"};
    static const char* names[4] = {"Tone 1 (sweep)", "Tone 2", "Wave", "Noise"};
    bool ch_on[4] = {apu->ch1.enabled, apu->ch2.enabled, apu->ch3.enabled, apu->ch4.enabled};
    for (int i = 0; i < 4; i++, y += XRAY_ROW) {
        uint32_t col = ch_on[i] ? UI_TEXT : UI_FAINT;
        ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x0, y, 150.0f, UI_ALIGN_LEFT, col, names[i]);
        switch (i) {
        case 0:
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cv, y, 0, UI_ALIGN_RIGHT, col, "%u",
                       (unsigned)apu->ch1.volume);
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cf, y, 0, UI_ALIGN_RIGHT, col, "%u",
                       (unsigned)apu->ch1.frequency);
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cd, y, dw, UI_ALIGN_LEFT, col, "Duty %s",
                       duty[apu->ch1.duty_cycle & 3]);
            break;
        case 1:
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cv, y, 0, UI_ALIGN_RIGHT, col, "%u",
                       (unsigned)apu->ch2.volume);
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cf, y, 0, UI_ALIGN_RIGHT, col, "%u",
                       (unsigned)apu->ch2.frequency);
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cd, y, dw, UI_ALIGN_LEFT, col, "Duty %s",
                       duty[apu->ch2.duty_cycle & 3]);
            break;
        case 2:
            ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cv, y, 0, UI_ALIGN_RIGHT, col,
                    wave_vol[apu->ch3.volume_code & 3]);
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cf, y, 0, UI_ALIGN_RIGHT, col, "%u",
                       (unsigned)apu->ch3.frequency);
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cd, y, dw, UI_ALIGN_LEFT, col,
                       "Position %u", (unsigned)apu->ch3.wave_pos);
            break;
        default:
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cv, y, 0, UI_ALIGN_RIGHT, col, "%u",
                       (unsigned)apu->ch4.volume);
            ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cf, y, 0, UI_ALIGN_RIGHT, col, "noise");
            xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, cd, y, dw, UI_ALIGN_LEFT, col,
                       "LFSR %04X \xC2\xB7 %s", (unsigned)apu->ch4.lfsr,
                       apu->ch4.width_mode ? "7-bit" : "15-bit");
            break;
        }
    }
}
