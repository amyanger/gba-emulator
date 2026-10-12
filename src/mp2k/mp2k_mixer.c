#include "mp2k/mp2k_mixer.h"
#include "common.h"
#include "gba.h"

#include <math.h>
#include <string.h>

#define ST_START 0x80
#define ST_STOP  0x40
#define ST_LOOP  0x10
#define ST_IEC   0x04
#define ST_PHASE 0x03
#define ST_ON    0xC7

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static Mp2kEnv echo_or_off(Mp2kEnv e, uint8_t echo_vol) {
    if (echo_vol == 0) {
        e.flags = 0;
    } else {
        e.env = echo_vol;
        e.flags |= ST_IEC;
    }
    return e;
}

static Mp2kEnv attack_step(Mp2kEnv e, uint8_t attack) {
    uint32_t v = (uint32_t)e.env + attack;
    if (v >= 255) {
        v = 255;
        e.flags = (uint8_t)((e.flags & ~ST_PHASE) | 2);
    }
    e.env = (uint8_t)v;
    return e;
}

Mp2kEnv mp2k_env_step(Mp2kEnv e, uint8_t attack, uint8_t decay, uint8_t sustain, uint8_t release,
                      uint8_t echo_vol, bool wav_loops) {
    if (!(e.flags & ST_ON)) return e;
    if (e.flags & ST_START) {
        if (e.flags & ST_STOP) {
            e.flags = 0;
            return e;
        }
        e.flags = (uint8_t)(3 | (wav_loops ? ST_LOOP : 0));
        e.env = 0;
        return attack_step(e, attack);
    }
    if (e.flags & ST_IEC) {
        uint8_t old = e.echo_len;
        e.echo_len = (uint8_t)(old - 1);
        if (old <= 1) e.flags = 0;
        return e;
    }
    if (e.flags & ST_STOP) {
        e.env = (uint8_t)(((uint32_t)e.env * release) >> 8);
        if (e.env <= echo_vol) e = echo_or_off(e, echo_vol);
        return e;
    }
    switch (e.flags & ST_PHASE) {
    case 3:
        return attack_step(e, attack);
    case 2:
        e.env = (uint8_t)(((uint32_t)e.env * decay) >> 8);
        if (e.env <= sustain) {
            e.env = sustain;
            if (sustain == 0) e = echo_or_off(e, echo_vol);
            else e.flags = (uint8_t)((e.flags & ~ST_PHASE) | 1);
        }
        return e;
    default:
        return e;
    }
}

bool mp2k_wave_read(const GameMem* mem, uint32_t wav, Mp2kWave* w) {
    uint8_t h[16];
    if (!game_mem_copy(mem, wav, h, sizeof(h))) return false;
    w->data = wav + 16u;
    w->dpcm = (h[0] | h[1]) != 0;
    w->loops = (h[3] & 0xC0) != 0;
    w->loop_start = rd32(h + 8);
    w->size = rd32(h + 12);
    if (w->size == 0) return false;
    if (w->loop_start >= w->size) w->loops = false;
    return true;
}

static const int8_t k_delta[16] = {0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1};

/* 64 samples from 33 bytes: raw first sample, low nibble of byte 1, then
 * high and low nibbles of bytes 2-32, each a delta with 8-bit wraparound. */
static void dpcm_decode(const GameMem* mem, uint32_t addr, int8_t out[64]) {
    uint8_t b[33];
    if (!game_mem_copy(mem, addr, b, sizeof(b))) {
        memset(out, 0, 64);
        return;
    }
    uint8_t acc = b[0];
    out[0] = (int8_t)acc;
    acc = (uint8_t)(acc + k_delta[b[1] & 0x0F]);
    out[1] = (int8_t)acc;
    for (int i = 2; i < 33; i++) {
        acc = (uint8_t)(acc + k_delta[b[i] >> 4]);
        out[(i - 1) * 2] = (int8_t)acc;
        acc = (uint8_t)(acc + k_delta[b[i] & 0x0F]);
        out[(i - 1) * 2 + 1] = (int8_t)acc;
    }
}

float mp2k_wave_at(const GameMem* mem, Mp2kVoice* v, const Mp2kWave* w, bool reverse, int64_t i) {
    if (i < 0) i = 0;
    if (i >= (int64_t)w->size) {
        if (!w->loops || reverse) return 0.0f;
        int64_t len = (int64_t)w->size - (int64_t)w->loop_start;
        i = (int64_t)w->loop_start + (i - (int64_t)w->loop_start) % len;
    }
    uint32_t idx = reverse ? w->size - 1u - (uint32_t)i : (uint32_t)i;
    if (w->dpcm) {
        int32_t blk = (int32_t)(idx / 64u);
        if (v->dpcm_block != blk) {
            dpcm_decode(mem, w->data + (uint32_t)blk * 33u, v->dpcm);
            v->dpcm_block = blk;
        }
        return (float)v->dpcm[idx % 64u];
    }
    uint8_t b;
    return game_mem_read8(mem, w->data + idx, &b) ? (float)(int8_t)b : 0.0f;
}

/* --- frame rendering --- */

#define INFO_CHANS   0x50u
#define CH_SIZE      0x40u
#define TYPE_FIXED   0x08
#define TYPE_REVERSE 0x10
#define TYPE_DPCM    0x20
#define TYPE_CGB     0x07
#define FIRST_FRAME  549u   /* 32768 Hz / 59.73 Hz, before a frame has been measured */
#define PRIME_FRAMES MP2K_PRIME_FRAMES   /* silence queued on reset, the same delay the game's own output has */
#define RING_MASK    (MP2K_RING_FRAMES - 1u)
#define HIST_MASK    (MP2K_HIST_FRAMES - 1u)

static int16_t to_s16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

static void ring_push(Mp2kMixer* m, int16_t l, int16_t r) {
    if (m->ring_write - m->ring_read >= MP2K_RING_FRAMES) m->ring_read++;   /* drop the oldest */
    uint32_t j = m->ring_write & RING_MASK;
    m->ring[j * 2] = l;
    m->ring[j * 2 + 1] = r;
    m->ring_write++;
}

void mp2k_mixer_reset(Mp2kMixer* m) {
    memset(m->voice, 0, sizeof(m->voice));
    for (int i = 0; i < MP2K_MAX_CHANS; i++) m->voice[i].dpcm_block = -1;
    memset(m->hist_l, 0, sizeof(m->hist_l));
    memset(m->hist_r, 0, sizeof(m->hist_r));
    m->hist_pos = 0;
    m->ring_read = m->ring_write = 0;
    for (uint32_t k = 0; k < PRIME_FRAMES; k++) ring_push(m, 0, 0);
    m->have_last = false;
    m->adopt = true;
}

void mp2k_mixer_init(Mp2kMixer* m, const Mp2kDriver* drv) {
    memset(m, 0, sizeof(*m));
    if (drv) {
        m->detected = true;
        m->drv = *drv;
    }
    mp2k_mixer_reset(m);
}

uint32_t mp2k_mixer_pop(Mp2kMixer* m, int16_t* out, uint32_t frames) {
    uint32_t avail = m->ring_write - m->ring_read;
    uint32_t got = avail < frames ? avail : frames;
    for (uint32_t i = 0; i < got; i++) {
        uint32_t j = (m->ring_read + i) & RING_MASK;
        out[i * 2] = m->ring[j * 2];
        out[i * 2 + 1] = m->ring[j * 2 + 1];
    }
    m->ring_read += got;
    if (got < frames) memset(out + got * 2, 0, (frames - got) * 2 * sizeof(int16_t));
    return got;
}

void mp2k_mixer_skip(Mp2kMixer* m, uint32_t frames) {
    uint32_t avail = m->ring_write - m->ring_read;
    m->ring_read += avail < frames ? avail : frames;
}

/* Catmull-Rom through four neighbouring samples, t in [0, 1). */
static float cubic(float a, float b, float c, float d, float t) {
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}

static void render_voice(Mp2kMixer* m, const GameMem* mem, Mp2kVoice* v, const Mp2kWave* w, bool reverse,
                         double step, float gl, float gr, uint32_t n) {
    /* Gains ramp across the frame instead of jumping like the game's. */
    float dl = (gl - v->gain_l) / (float)n, dr = (gr - v->gain_r) / (float)n;
    float cl = v->gain_l, cr = v->gain_r;
    for (uint32_t k = 0; k < n && v->active; k++) {
        cl += dl;
        cr += dr;
        double fl = floor(v->pos);
        int64_t i = (int64_t)fl;
        float t = (float)(v->pos - fl);
        float s = cubic(mp2k_wave_at(mem, v, w, reverse, i - 1), mp2k_wave_at(mem, v, w, reverse, i),
                        mp2k_wave_at(mem, v, w, reverse, i + 1), mp2k_wave_at(mem, v, w, reverse, i + 2), t);
        m->mix_l[k] += s * cl;
        m->mix_r[k] += s * cr;
        v->pos += step;
        if (v->pos >= (double)w->size) {
            if (w->loops && !reverse)
                v->pos = w->loop_start + fmod(v->pos - w->loop_start, (double)(w->size - w->loop_start));
            else
                v->active = false;
        }
    }
}

static void process_channel(Mp2kMixer* m, const GameMem* mem, uint32_t addr, uint8_t master, uint32_t pcm_freq,
                            uint32_t n, Mp2kVoice* v) {
    uint8_t ch[CH_SIZE];
    Mp2kWave w;
    if (!game_mem_copy(mem, addr, ch, sizeof(ch)) || !(ch[0] & ST_ON) || (ch[1] & TYPE_CGB) ||
        !mp2k_wave_read(mem, rd32(ch + 0x24), &w)) {
        v->active = false;
        return;
    }
    uint8_t type = ch[1];
    uint32_t wav = rd32(ch + 0x24);
    bool reverse = (type & TYPE_REVERSE) != 0;
    /* Only the compressed/reverse path decodes DPCM; it never loops in reverse. */
    w.dpcm = w.dpcm && (type & (TYPE_DPCM | TYPE_REVERSE));
    if ((type & TYPE_DPCM) && !w.dpcm && !reverse) {
        v->active = false;   /* the game mixes nothing for this combination */
        return;
    }
    if (reverse) w.loops = false;

    bool fresh = false;
    if (ch[0] & ST_START) {
        v->pos = (double)rd32(ch + 0x18);   /* count holds the start offset */
        fresh = true;
    } else if (m->adopt || !v->active || v->wav != wav) {
        uint32_t count = rd32(ch + 0x18);   /* samples remaining */
        v->pos = count <= w.size ? (double)(w.size - count) : 0.0;
        fresh = true;
    }
    if (fresh) {
        v->active = true;
        v->wav = wav;
        v->dpcm_block = -1;
    }

    Mp2kEnv e = { ch[0], ch[9], ch[0x0D] };
    e = mp2k_env_step(e, ch[4], ch[5], ch[6], ch[7], ch[0x0C], w.loops);
    float gl = 0.0f, gr = 0.0f;
    if (e.flags & ST_ON) {
        uint32_t lvl = ((uint32_t)e.env * ((master & 0x0Fu) + 1u)) >> 4;
        gr = (float)((ch[2] * lvl) >> 8) / 256.0f;
        gl = (float)((ch[3] * lvl) >> 8) / 256.0f;
    }
    if (fresh) {
        v->gain_l = gl;   /* new or adopted note: start at the target gain, keep the attack transient */
        v->gain_r = gr;
    }

    double step = (type & TYPE_FIXED) ? (double)pcm_freq / MP2K_RATE : (double)rd32(ch + 0x20) / MP2K_RATE;
    render_voice(m, mem, v, &w, reverse, step, gl, gr, n);
    v->gain_l = gl;
    v->gain_r = gr;
    if (!(e.flags & ST_ON)) v->active = false;   /* faded to 0 over this frame */
}

/* The game's reverb: before channels are mixed, each sample gets
 * rev/512 * (L+R from period frames ago + L+R from period-1 frames ago). */
static void mix_reverb(Mp2kMixer* m, const uint8_t* si, uint32_t n) {
    uint32_t rev = si[5], period = si[0x0B], spv = rd32(si + 0x10), freq = rd32(si + 0x14);
    if (rev == 0 || period < 2 || spv == 0 || freq == 0) return;
    uint32_t d_old = (uint32_t)((uint64_t)period * spv * MP2K_RATE / freq);
    uint32_t d_next = (uint32_t)((uint64_t)(period - 1) * spv * MP2K_RATE / freq);
    if (d_old >= MP2K_HIST_FRAMES || d_next < n) return;   /* would read samples not mixed yet */
    float g = (float)rev / 512.0f;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t h = m->hist_pos + k;
        uint32_t a = (h - d_old) & HIST_MASK, b = (h - d_next) & HIST_MASK;
        float v = g * (m->hist_l[a] + m->hist_r[a] + m->hist_l[b] + m->hist_r[b]);
        m->mix_l[k] += v;
        m->mix_r[k] += v;
    }
}

void mp2k_mixer_on_hook(const GBA* gba, void* ctx) {
    Mp2kMixer* m = (Mp2kMixer*)ctx;
    uint32_t produced = gba->apu.samples_produced;
    uint32_t n = m->have_last ? produced - m->last_samples : FIRST_FRAME;
    m->have_last = true;
    m->last_samples = produced;
    if (n == 0) return;
    if (n > MP2K_FRAME_MAX) n = MP2K_FRAME_MAX;
    memset(m->mix_l, 0, n * sizeof(float));
    memset(m->mix_r, 0, n * sizeof(float));

    GameMem mem = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    uint32_t info = 0;
    uint8_t si[INFO_CHANS];
    if (game_mem_read32(&mem, m->drv.sound_info_ptr, &info) && game_mem_copy(&mem, info, si, sizeof(si)) &&
        rd32(si) == MP2K_ID_NUMBER + 1u && si[6] <= MP2K_MAX_CHANS) {
        mix_reverb(m, si, n);
        uint32_t chans = si[6];
        for (uint32_t i = 0; i < chans; i++)
            process_channel(m, &mem, info + INFO_CHANS + i * CH_SIZE, si[7], rd32(si + 0x14), n, &m->voice[i]);
        for (uint32_t i = chans; i < MP2K_MAX_CHANS; i++) m->voice[i].active = false;
        m->adopt = false;
        m->hooks++;
    }

    for (uint32_t k = 0; k < n; k++) {
        m->hist_l[(m->hist_pos + k) & HIST_MASK] = m->mix_l[k];
        m->hist_r[(m->hist_pos + k) & HIST_MASK] = m->mix_r[k];
    }
    m->hist_pos += n;

    /* Same scale and routing as the APU's FIFO path: FIFO A carries the
     * right mix, FIFO B the left; x4 at 100% or x2 at 50%, then x32. */
    uint16_t cnt = gba->apu.soundcnt_h;
    bool on = BIT(gba->apu.soundcnt_x, 7);
    float va = BIT(cnt, 2) ? 4.0f : 2.0f, vb = BIT(cnt, 3) ? 4.0f : 2.0f;
    /* Keep the cushion that matches the game's output latency ahead of this
     * render; anything more is stale catch-up after a gap and would leave HQ
     * music late against the APU. */
    /* The refill below pairs with this trim: together they hold the cushion at
     * the game's output latency after any gap, with a one-frame tolerance so
     * normal jitter never inserts zeros mid-waveform. */
    uint32_t q = m->ring_write - m->ring_read;
    if (q + FIRST_FRAME < PRIME_FRAMES)
        for (; q < PRIME_FRAMES; q++) ring_push(m, 0, 0);
    if (m->ring_write - m->ring_read > PRIME_FRAMES) m->ring_read = m->ring_write - PRIME_FRAMES;
    for (uint32_t k = 0; k < n; k++) {
        float a = m->mix_r[k] * va, b = m->mix_l[k] * vb;
        float l = (BIT(cnt, 9) ? a : 0.0f) + (BIT(cnt, 13) ? b : 0.0f);
        float r = (BIT(cnt, 8) ? a : 0.0f) + (BIT(cnt, 12) ? b : 0.0f);
        ring_push(m, on ? to_s16(l * 32.0f) : 0, on ? to_s16(r * 32.0f) : 0);
    }
}
