#include "savestate/savestate.h"
#include "gba.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* -------------------------------------------------------------------------- */
/*  Header layout constants                                                   */
/* -------------------------------------------------------------------------- */
#define HEADER_SIZE      48
#define HEADER_OFF_MAGIC  0x00
#define HEADER_OFF_VER    0x04
#define HEADER_OFF_FSIZE  0x08
#define HEADER_OFF_CRC    0x0C
#define HEADER_OFF_ROMCRC 0x10
#define HEADER_OFF_ROMSZ  0x14
#define HEADER_OFF_TITLE  0x18
#define HEADER_OFF_TIME   0x24
#define HEADER_OFF_RSVD   0x2C

/* Chunk IDs (4 bytes each, stored as uint32_t LE) */
#define CHUNK_GBA  0x00414247  /* "GBA\0" */
#define CHUNK_CPU  0x00555043  /* "CPU\0" */
#define CHUNK_BRAM 0x4D415242  /* "BRAM"  */
#define CHUNK_DMA  0x00414D44  /* "DMA\0" */
#define CHUNK_PPU  0x00555050  /* "PPU\0" */
#define CHUNK_APU  0x00555041  /* "APU\0" */
#define CHUNK_TMR  0x00524D54  /* "TMR\0" */
#define CHUNK_IRQ  0x00515249  /* "IRQ\0" */
#define CHUNK_CART 0x54524143  /* "CART"  */
#define CHUNK_INPT 0x54504E49  /* "INPT"  */
#define CHUNK_SIO  0x004F4953  /* "SIO\0" */

/* -------------------------------------------------------------------------- */
/*  CRC32 (standard polynomial 0xEDB88320)                                    */
/* -------------------------------------------------------------------------- */
static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1)
                c = 0xEDB88320u ^ (c >> 1);
            else
                c >>= 1;
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = true;
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    if (!crc32_table_ready) crc32_init_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* -------------------------------------------------------------------------- */
/*  Dynamic write buffer                                                      */
/* -------------------------------------------------------------------------- */
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
    bool oom;
} WriteBuffer;

static bool wbuf_init(WriteBuffer* wb, size_t initial) {
    wb->data = (uint8_t*)malloc(initial);
    if (!wb->data) return false;
    wb->size = 0;
    wb->capacity = initial;
    wb->oom = false;
    return true;
}

static void wbuf_free(WriteBuffer* wb) {
    free(wb->data);
    wb->data = NULL;
    wb->size = 0;
    wb->capacity = 0;
}

static bool wbuf_ensure(WriteBuffer* wb, size_t extra) {
    size_t needed = wb->size + extra;
    if (needed <= wb->capacity) return true;
    size_t new_cap = wb->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    uint8_t* p = (uint8_t*)realloc(wb->data, new_cap);
    if (!p) return false;
    wb->data = p;
    wb->capacity = new_cap;
    return true;
}

/* -------------------------------------------------------------------------- */
/*  Write helpers (little-endian, append to buffer)                           */
/* -------------------------------------------------------------------------- */
static void write_u8(WriteBuffer* wb, uint8_t v) {
    if (wb->oom || !wbuf_ensure(wb, 1)) { wb->oom = true; return; }
    wb->data[wb->size++] = v;
}

static void write_u16(WriteBuffer* wb, uint16_t v) {
    if (wb->oom || !wbuf_ensure(wb, 2)) { wb->oom = true; return; }
    wb->data[wb->size++] = (uint8_t)(v & 0xFF);
    wb->data[wb->size++] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_u32(WriteBuffer* wb, uint32_t v) {
    if (wb->oom || !wbuf_ensure(wb, 4)) { wb->oom = true; return; }
    wb->data[wb->size++] = (uint8_t)(v & 0xFF);
    wb->data[wb->size++] = (uint8_t)((v >> 8) & 0xFF);
    wb->data[wb->size++] = (uint8_t)((v >> 16) & 0xFF);
    wb->data[wb->size++] = (uint8_t)((v >> 24) & 0xFF);
}

static void write_u64(WriteBuffer* wb, uint64_t v) {
    write_u32(wb, (uint32_t)(v & 0xFFFFFFFFu));
    write_u32(wb, (uint32_t)(v >> 32));
}

static void write_bytes(WriteBuffer* wb, const void* src, size_t len) {
    if (wb->oom || !wbuf_ensure(wb, len)) { wb->oom = true; return; }
    memcpy(wb->data + wb->size, src, len);
    wb->size += len;
}

/* -------------------------------------------------------------------------- */
/*  Read helpers (little-endian, from cursor into buffer)                     */
/* -------------------------------------------------------------------------- */
static uint8_t read_u8(const uint8_t** cur) {
    uint8_t v = (*cur)[0];
    *cur += 1;
    return v;
}

static uint16_t read_u16(const uint8_t** cur) {
    uint16_t v = (uint16_t)((*cur)[0] | ((*cur)[1] << 8));
    *cur += 2;
    return v;
}

static uint32_t read_u32(const uint8_t** cur) {
    uint32_t v = (uint32_t)(*cur)[0]
               | ((uint32_t)(*cur)[1] << 8)
               | ((uint32_t)(*cur)[2] << 16)
               | ((uint32_t)(*cur)[3] << 24);
    *cur += 4;
    return v;
}

static uint64_t read_u64(const uint8_t** cur) {
    uint32_t lo = read_u32(cur);
    uint32_t hi = read_u32(cur);
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static void read_bytes(const uint8_t** cur, void* dst, size_t len) {
    memcpy(dst, *cur, len);
    *cur += len;
}

/* -------------------------------------------------------------------------- */
/*  Chunk writers — each writes its chunk header + payload into the buffer    */
/* -------------------------------------------------------------------------- */
static void write_chunk_header(WriteBuffer* wb, uint32_t id, uint32_t data_size) {
    write_u32(wb, id);
    write_u32(wb, data_size);
}

static void save_gba_chunk(WriteBuffer* wb, GBA* gba) {
    uint32_t payload_size = 8 + 1; /* u64 total_cycles + u8 frame_complete */
    write_chunk_header(wb, CHUNK_GBA, payload_size);
    write_u64(wb, gba->total_cycles);
    write_u8(wb, gba->frame_complete ? 1 : 0);
}

static void save_cpu_chunk(WriteBuffer* wb, ARM7TDMI* cpu) {
    /* 16*4 + 4 + 5*4 + 22*4 + 2*4 + 1 + 1 + 4 = 184 bytes */
    uint32_t payload_size = 64 + 4 + 20 + 88 + 8 + 1 + 1 + 4;
    write_chunk_header(wb, CHUNK_CPU, payload_size);

    for (int i = 0; i < 16; i++) write_u32(wb, cpu->regs[i]);
    write_u32(wb, cpu->cpsr);
    for (int i = 0; i < 5; i++) write_u32(wb, cpu->spsr[i]);
    for (int i = 0; i < 22; i++) write_u32(wb, cpu->banked[i]);
    for (int i = 0; i < 2; i++) write_u32(wb, cpu->pipeline[i]);
    write_u8(wb, cpu->pipeline_valid ? 1 : 0);
    write_u8(wb, cpu->halted ? 1 : 0);
    write_u32(wb, (uint32_t)cpu->cycles_executed);
}

static void save_bram_chunk(WriteBuffer* wb, Bus* bus) {
    uint32_t payload_size = EWRAM_SIZE + IWRAM_SIZE + IO_SIZE
                          + PALETTE_SIZE + VRAM_SIZE + OAM_SIZE
                          + 4 + 1 + 4;
    write_chunk_header(wb, CHUNK_BRAM, payload_size);

    write_bytes(wb, bus->ewram, EWRAM_SIZE);
    write_bytes(wb, bus->iwram, IWRAM_SIZE);
    write_bytes(wb, bus->io_regs, IO_SIZE);
    write_bytes(wb, bus->palette_ram, PALETTE_SIZE);
    write_bytes(wb, bus->vram, VRAM_SIZE);
    write_bytes(wb, bus->oam, OAM_SIZE);
    write_u32(wb, bus->open_bus);
    write_u8(wb, bus->bios_readable ? 1 : 0);
    write_u32(wb, bus->last_bios_read);
}

static void save_dma_chunk(WriteBuffer* wb, DMAController* dma) {
    /* per channel: 4*4 + 2*2 + 2*1 + 4*1 + 1 = 27 bytes, x4 = 108, + 1 = 109 */
    uint32_t payload_size = 4 * (4 * 4 + 2 * 2 + 2 + 4 + 1) + 1;
    write_chunk_header(wb, CHUNK_DMA, payload_size);

    for (int ch = 0; ch < 4; ch++) {
        DMAChannel* c = &dma->channels[ch];
        write_u32(wb, c->source);
        write_u32(wb, c->dest);
        write_u32(wb, c->source_latch);
        write_u32(wb, c->dest_latch);
        write_u16(wb, c->count);
        write_u16(wb, c->control);
        write_u8(wb, (uint8_t)c->dest_adjust);
        write_u8(wb, (uint8_t)c->src_adjust);
        write_u8(wb, c->repeat ? 1 : 0);
        write_u8(wb, c->transfer_32 ? 1 : 0);
        write_u8(wb, c->timing);
        write_u8(wb, c->irq_on_done ? 1 : 0);
        write_u8(wb, c->enabled ? 1 : 0);
    }
    write_u8(wb, (uint8_t)dma->active_channel);
}

static void save_ppu_chunk(WriteBuffer* wb, PPU* ppu) {
    /* 3*u16 (dispXX) + 3*4*u16 (bg_cnt/hofs/vofs) + 4*2*u16 (bg_pa..pd)
     * + 4*2*u32 (bg_ref_xy + latches) + 2*2*u16 (win_h/v) + 2*u16 (winin/out)
     * + 3*u16 (bld) + u16 (mosaic) + u32 (cycle_counter)
     * = 6 + 24 + 16 + 32 + 8 + 4 + 6 + 2 + 4 = 102 */
    uint32_t payload_size = 6 + 24 + 16 + 32 + 8 + 4 + 6 + 2 + 4;
    write_chunk_header(wb, CHUNK_PPU, payload_size);

    write_u16(wb, ppu->dispcnt);
    write_u16(wb, ppu->dispstat);
    write_u16(wb, ppu->vcount);
    for (int i = 0; i < 4; i++) write_u16(wb, ppu->bg_cnt[i]);
    for (int i = 0; i < 4; i++) write_u16(wb, ppu->bg_hofs[i]);
    for (int i = 0; i < 4; i++) write_u16(wb, ppu->bg_vofs[i]);
    for (int i = 0; i < 2; i++) write_u16(wb, (uint16_t)ppu->bg_pa[i]);
    for (int i = 0; i < 2; i++) write_u16(wb, (uint16_t)ppu->bg_pb[i]);
    for (int i = 0; i < 2; i++) write_u16(wb, (uint16_t)ppu->bg_pc[i]);
    for (int i = 0; i < 2; i++) write_u16(wb, (uint16_t)ppu->bg_pd[i]);
    for (int i = 0; i < 2; i++) write_u32(wb, (uint32_t)ppu->bg_ref_x[i]);
    for (int i = 0; i < 2; i++) write_u32(wb, (uint32_t)ppu->bg_ref_y[i]);
    for (int i = 0; i < 2; i++) write_u32(wb, (uint32_t)ppu->bg_ref_x_latch[i]);
    for (int i = 0; i < 2; i++) write_u32(wb, (uint32_t)ppu->bg_ref_y_latch[i]);
    for (int i = 0; i < 2; i++) write_u16(wb, ppu->win_h[i]);
    for (int i = 0; i < 2; i++) write_u16(wb, ppu->win_v[i]);
    write_u16(wb, ppu->winin);
    write_u16(wb, ppu->winout);
    write_u16(wb, ppu->bldcnt);
    write_u16(wb, ppu->bldalpha);
    write_u16(wb, ppu->bldy);
    write_u16(wb, ppu->mosaic);
    write_u32(wb, ppu->cycle_counter);
}

static void save_square_channel(WriteBuffer* wb, SquareChannel* ch) {
    write_u8(wb, ch->enabled ? 1 : 0);
    write_u16(wb, ch->length_counter);
    write_u8(wb, ch->length_enable ? 1 : 0);
    write_u16(wb, ch->frequency);
    write_u32(wb, ch->freq_timer);
    write_u8(wb, ch->duty_cycle);
    write_u8(wb, ch->duty_pos);
    write_u8(wb, ch->volume);
    write_u8(wb, ch->vol_period);
    write_u8(wb, ch->vol_dir ? 1 : 0);
    write_u8(wb, ch->vol_timer);
    write_u8(wb, ch->sweep_period);
    write_u8(wb, ch->sweep_dir ? 1 : 0);
    write_u8(wb, ch->sweep_shift);
    write_u8(wb, ch->sweep_timer);
    write_u16(wb, ch->sweep_freq);
    write_u8(wb, ch->sweep_enabled ? 1 : 0);
}

static void save_wave_channel(WriteBuffer* wb, WaveChannel* ch) {
    write_u8(wb, ch->enabled ? 1 : 0);
    write_u16(wb, ch->length_counter);
    write_u8(wb, ch->length_enable ? 1 : 0);
    write_u16(wb, ch->frequency);
    write_u32(wb, ch->freq_timer);
    write_bytes(wb, ch->wave_ram, 16);
    write_u8(wb, ch->wave_pos);
    write_u8(wb, ch->volume_code);
    write_u8(wb, ch->bank_mode ? 1 : 0);
    write_u8(wb, ch->bank_select);
    write_u8(wb, ch->force_volume ? 1 : 0);
}

static void save_noise_channel(WriteBuffer* wb, NoiseChannel* ch) {
    write_u8(wb, ch->enabled ? 1 : 0);
    write_u16(wb, ch->length_counter);
    write_u8(wb, ch->length_enable ? 1 : 0);
    write_u8(wb, ch->volume);
    write_u8(wb, ch->vol_period);
    write_u8(wb, ch->vol_dir ? 1 : 0);
    write_u8(wb, ch->vol_timer);
    write_u16(wb, ch->lfsr);
    write_u8(wb, ch->width_mode ? 1 : 0);
    write_u8(wb, ch->divisor_code);
    write_u8(wb, ch->shift);
    write_u32(wb, ch->freq_timer);
}

static void save_fifo(WriteBuffer* wb, FIFO* fifo) {
    write_bytes(wb, fifo->buffer, FIFO_SIZE);
    write_u8(wb, fifo->read_idx);
    write_u8(wb, fifo->write_idx);
    write_u8(wb, fifo->count);
    write_u8(wb, fifo->timer_id);
    write_u8(wb, (uint8_t)fifo->last_sample);
}

/* Square channel payload: 1+2+1+2+4+1+1+1+1+1+1+1+1+1+1+2+1 = 23 bytes */
#define SQUARE_CH_SIZE 23
/* Wave channel payload: 1+2+1+2+4+16+1+1+1+1+1 = 31 bytes */
#define WAVE_CH_SIZE   31
/* Noise channel payload: 1+2+1+1+1+1+1+2+1+1+1+4 = 17 bytes */
#define NOISE_CH_SIZE  17
/* FIFO payload: 32+1+1+1+1+1 = 37 bytes */
#define FIFO_CH_SIZE   37

static void save_apu_chunk(WriteBuffer* wb, APU* apu) {
    uint32_t payload_size = 2 * SQUARE_CH_SIZE + WAVE_CH_SIZE + NOISE_CH_SIZE
                          + 2 * FIFO_CH_SIZE
                          + 2 + 4 * 2 + 1 + 4 + 4 + 4 + 2 * 2;
    write_chunk_header(wb, CHUNK_APU, payload_size);

    save_square_channel(wb, &apu->ch1);
    save_square_channel(wb, &apu->ch2);
    save_wave_channel(wb, &apu->ch3);
    save_noise_channel(wb, &apu->ch4);
    save_fifo(wb, &apu->fifo_a);
    save_fifo(wb, &apu->fifo_b);

    write_u8(wb, (uint8_t)apu->fifo_a_latch);
    write_u8(wb, (uint8_t)apu->fifo_b_latch);
    write_u16(wb, apu->soundcnt_l);
    write_u16(wb, apu->soundcnt_h);
    write_u16(wb, apu->soundcnt_x);
    write_u16(wb, apu->soundbias);
    write_u8(wb, apu->frame_seq_step);
    write_u32(wb, apu->frame_seq_timer);
    write_u32(wb, apu->sample_timer);
    write_u32(wb, apu->sample_period);
    write_u16(wb, (uint16_t)apu->prev_left);
    write_u16(wb, (uint16_t)apu->prev_right);
}

static void save_tmr_chunk(WriteBuffer* wb, Timer timers[4]) {
    /* per timer: 4*2 + 3*1 + 4 = 15 bytes, x4 = 60 */
    uint32_t payload_size = 4 * (4 * 2 + 3 + 4);
    write_chunk_header(wb, CHUNK_TMR, payload_size);

    for (int i = 0; i < 4; i++) {
        Timer* t = &timers[i];
        write_u16(wb, t->counter);
        write_u16(wb, t->reload);
        write_u16(wb, t->control);
        write_u16(wb, t->prescaler);
        write_u8(wb, t->cascade ? 1 : 0);
        write_u8(wb, t->irq_enable ? 1 : 0);
        write_u8(wb, t->enabled ? 1 : 0);
        write_u32(wb, t->prescaler_counter);
    }
}

static void save_irq_chunk(WriteBuffer* wb, InterruptController* ic) {
    uint32_t payload_size = 1 + 2 + 2; /* 5 bytes */
    write_chunk_header(wb, CHUNK_IRQ, payload_size);

    write_u8(wb, ic->ime ? 1 : 0);
    write_u16(wb, ic->ie);
    write_u16(wb, ic->irf);
}

static void save_cart_chunk(WriteBuffer* wb, Cartridge* cart) {
    /* save_type + flash + flash meta + sram + GPIO (6) + RTC (23) + EEPROM (8211) */
    uint32_t eeprom_size = (uint32_t)EEPROM_MAX_SIZE + 1 + 1 + 2 + 2 + 2 + 8 + 2 + 1;
    uint32_t payload_size = 1 + 0x20000 + 4 + 0x8000 + 6 + 23 + eeprom_size;
    write_chunk_header(wb, CHUNK_CART, payload_size);

    write_u8(wb, (uint8_t)cart->save_type);
    write_bytes(wb, cart->flash.data, 0x20000);
    write_u8(wb, (uint8_t)cart->flash.state);
    write_u8(wb, cart->flash.bank);
    write_u8(wb, cart->flash.manufacturer);
    write_u8(wb, cart->flash.device);
    write_bytes(wb, cart->sram, 0x8000);

    /* GPIO registers (6 bytes) */
    write_u16(wb, cart->gpio.data);
    write_u16(wb, cart->gpio.direction);
    write_u16(wb, cart->gpio.control);

    /* RTC protocol state (23 bytes). Transient pin-tracking fields
     * (prev_cs, prev_sck, sio_out) are NOT persisted — they're
     * reconstructed from pin edges after load. */
    write_u8(wb, (uint8_t)cart->rtc.phase);
    write_u8(wb, cart->rtc.cmd_byte);
    write_u8(wb, cart->rtc.cmd_bits);
    write_bytes(wb, cart->rtc.payload, 8);
    write_u8(wb, cart->rtc.payload_len);
    write_u8(wb, cart->rtc.payload_byte);
    write_u8(wb, cart->rtc.payload_bit);
    write_u8(wb, cart->rtc.status_reg);
    write_u64(wb, (uint64_t)cart->rtc.offset_secs);

    /* EEPROM data + protocol state. Detected size + protocol phase need to
     * roundtrip so a state captured mid-transfer resumes coherently. */
    write_bytes(wb, cart->eeprom.data, EEPROM_MAX_SIZE);
    write_u8(wb, cart->eeprom.addr_bits);
    write_u8(wb, (uint8_t)cart->eeprom.state);
    write_u16(wb, cart->eeprom.addr);
    write_u16(wb, cart->eeprom.rx_count);
    write_u16(wb, cart->eeprom.expected_dma);
    write_u64(wb, cart->eeprom.shift);
    write_u16(wb, cart->eeprom.tx_count);
    write_u8(wb, cart->eeprom.dirty ? 1 : 0);
}

static void save_inpt_chunk(WriteBuffer* wb, InputState* input) {
    uint32_t payload_size = 2 + 2; /* 4 bytes */
    write_chunk_header(wb, CHUNK_INPT, payload_size);

    write_u16(wb, input->keyinput);
    write_u16(wb, input->keycnt);
}

static void save_sio_chunk(WriteBuffer* wb, SIO* sio) {
    /* 4*u16 (siomulti) + u16 (siocnt) + u16 (siomlt_send) + u16 (rcnt)
     * + u32 (siodata32) + u8 (mode) + u8 (serial_mode_enabled)
     * + u8 (transfer_active) + u32 (transfer_cycles_remaining)
     * = 8 + 2 + 2 + 2 + 4 + 1 + 1 + 1 + 4 = 25 bytes
     * NOTE: the InterruptController* and LinkPeer* pointer fields are
     * deliberately NOT serialized — they're meaningless across processes
     * and are re-attached after load by savestate_load_from_buffer. */
    uint32_t payload_size = 8 + 2 + 2 + 2 + 4 + 1 + 1 + 1 + 4;
    write_chunk_header(wb, CHUNK_SIO, payload_size);

    for (int i = 0; i < 4; i++) write_u16(wb, sio->siomulti[i]);
    write_u16(wb, sio->siocnt);
    write_u16(wb, sio->siomlt_send);
    write_u16(wb, sio->rcnt);
    write_u32(wb, sio->siodata32);
    write_u8(wb, (uint8_t)sio->mode);
    write_u8(wb, sio->serial_mode_enabled ? 1 : 0);
    write_u8(wb, sio->transfer_active ? 1 : 0);
    write_u32(wb, (uint32_t)sio->transfer_cycles_remaining);
}

/* -------------------------------------------------------------------------- */
/*  Chunk loaders — read payload from cursor into subsystem structs           */
/* -------------------------------------------------------------------------- */
static void load_gba_chunk(const uint8_t** cur, GBA* gba) {
    gba->total_cycles = read_u64(cur);
    gba->frame_complete = read_u8(cur) != 0;
}

static void load_cpu_chunk(const uint8_t** cur, ARM7TDMI* cpu) {
    for (int i = 0; i < 16; i++) cpu->regs[i] = read_u32(cur);
    cpu->cpsr = read_u32(cur);
    for (int i = 0; i < 5; i++) cpu->spsr[i] = read_u32(cur);
    for (int i = 0; i < 22; i++) cpu->banked[i] = read_u32(cur);
    for (int i = 0; i < 2; i++) cpu->pipeline[i] = read_u32(cur);
    cpu->pipeline_valid = read_u8(cur) != 0;
    cpu->halted = read_u8(cur) != 0;
    cpu->cycles_executed = (int)read_u32(cur);
}

static void load_bram_chunk(const uint8_t** cur, Bus* bus) {
    read_bytes(cur, bus->ewram, EWRAM_SIZE);
    read_bytes(cur, bus->iwram, IWRAM_SIZE);
    read_bytes(cur, bus->io_regs, IO_SIZE);
    read_bytes(cur, bus->palette_ram, PALETTE_SIZE);
    read_bytes(cur, bus->vram, VRAM_SIZE);
    read_bytes(cur, bus->oam, OAM_SIZE);
    bus->open_bus = read_u32(cur);
    bus->bios_readable = read_u8(cur) != 0;
    bus->last_bios_read = read_u32(cur);
    bus_post_load(bus);
}

static void load_dma_chunk(const uint8_t** cur, DMAController* dma) {
    for (int ch = 0; ch < 4; ch++) {
        DMAChannel* c = &dma->channels[ch];
        c->source = read_u32(cur);
        c->dest = read_u32(cur);
        c->source_latch = read_u32(cur);
        c->dest_latch = read_u32(cur);
        c->count = read_u16(cur);
        c->control = read_u16(cur);
        c->dest_adjust = (int8_t)read_u8(cur);
        c->src_adjust = (int8_t)read_u8(cur);
        c->repeat = read_u8(cur) != 0;
        c->transfer_32 = read_u8(cur) != 0;
        c->timing = read_u8(cur);
        c->irq_on_done = read_u8(cur) != 0;
        c->enabled = read_u8(cur) != 0;
    }
    dma->active_channel = (int8_t)read_u8(cur);
}

static void load_ppu_chunk(const uint8_t** cur, PPU* ppu) {
    ppu->dispcnt = read_u16(cur);
    ppu->dispstat = read_u16(cur);
    ppu->vcount = read_u16(cur);
    for (int i = 0; i < 4; i++) ppu->bg_cnt[i] = read_u16(cur);
    for (int i = 0; i < 4; i++) ppu->bg_hofs[i] = read_u16(cur);
    for (int i = 0; i < 4; i++) ppu->bg_vofs[i] = read_u16(cur);
    for (int i = 0; i < 2; i++) ppu->bg_pa[i] = (int16_t)read_u16(cur);
    for (int i = 0; i < 2; i++) ppu->bg_pb[i] = (int16_t)read_u16(cur);
    for (int i = 0; i < 2; i++) ppu->bg_pc[i] = (int16_t)read_u16(cur);
    for (int i = 0; i < 2; i++) ppu->bg_pd[i] = (int16_t)read_u16(cur);
    for (int i = 0; i < 2; i++) ppu->bg_ref_x[i] = (int32_t)read_u32(cur);
    for (int i = 0; i < 2; i++) ppu->bg_ref_y[i] = (int32_t)read_u32(cur);
    for (int i = 0; i < 2; i++) ppu->bg_ref_x_latch[i] = (int32_t)read_u32(cur);
    for (int i = 0; i < 2; i++) ppu->bg_ref_y_latch[i] = (int32_t)read_u32(cur);
    for (int i = 0; i < 2; i++) ppu->win_h[i] = read_u16(cur);
    for (int i = 0; i < 2; i++) ppu->win_v[i] = read_u16(cur);
    ppu->winin = read_u16(cur);
    ppu->winout = read_u16(cur);
    ppu->bldcnt = read_u16(cur);
    ppu->bldalpha = read_u16(cur);
    ppu->bldy = read_u16(cur);
    ppu->mosaic = read_u16(cur);
    ppu->cycle_counter = read_u32(cur);
}

static void load_square_channel(const uint8_t** cur, SquareChannel* ch) {
    ch->enabled = read_u8(cur) != 0;
    ch->length_counter = read_u16(cur);
    ch->length_enable = read_u8(cur) != 0;
    ch->frequency = read_u16(cur);
    ch->freq_timer = read_u32(cur);
    ch->duty_cycle = read_u8(cur);
    ch->duty_pos = read_u8(cur);
    ch->volume = read_u8(cur);
    ch->vol_period = read_u8(cur);
    ch->vol_dir = read_u8(cur) != 0;
    ch->vol_timer = read_u8(cur);
    ch->sweep_period = read_u8(cur);
    ch->sweep_dir = read_u8(cur) != 0;
    ch->sweep_shift = read_u8(cur);
    ch->sweep_timer = read_u8(cur);
    ch->sweep_freq = read_u16(cur);
    ch->sweep_enabled = read_u8(cur) != 0;
}

static void load_wave_channel(const uint8_t** cur, WaveChannel* ch) {
    ch->enabled = read_u8(cur) != 0;
    ch->length_counter = read_u16(cur);
    ch->length_enable = read_u8(cur) != 0;
    ch->frequency = read_u16(cur);
    ch->freq_timer = read_u32(cur);
    read_bytes(cur, ch->wave_ram, 16);
    ch->wave_pos = read_u8(cur);
    ch->volume_code = read_u8(cur);
    ch->bank_mode = read_u8(cur) != 0;
    ch->bank_select = read_u8(cur);
    ch->force_volume = read_u8(cur) != 0;
}

static void load_noise_channel(const uint8_t** cur, NoiseChannel* ch) {
    ch->enabled = read_u8(cur) != 0;
    ch->length_counter = read_u16(cur);
    ch->length_enable = read_u8(cur) != 0;
    ch->volume = read_u8(cur);
    ch->vol_period = read_u8(cur);
    ch->vol_dir = read_u8(cur) != 0;
    ch->vol_timer = read_u8(cur);
    ch->lfsr = read_u16(cur);
    ch->width_mode = read_u8(cur) != 0;
    ch->divisor_code = read_u8(cur);
    ch->shift = read_u8(cur);
    ch->freq_timer = read_u32(cur);
}

static void load_fifo(const uint8_t** cur, FIFO* fifo) {
    read_bytes(cur, fifo->buffer, FIFO_SIZE);
    fifo->read_idx = read_u8(cur);
    fifo->write_idx = read_u8(cur);
    fifo->count = read_u8(cur);
    fifo->timer_id = read_u8(cur);
    fifo->last_sample = (int8_t)read_u8(cur);
}

static void load_apu_chunk(const uint8_t** cur, APU* apu) {
    load_square_channel(cur, &apu->ch1);
    load_square_channel(cur, &apu->ch2);
    load_wave_channel(cur, &apu->ch3);
    load_noise_channel(cur, &apu->ch4);
    load_fifo(cur, &apu->fifo_a);
    load_fifo(cur, &apu->fifo_b);

    apu->fifo_a_latch = (int8_t)read_u8(cur);
    apu->fifo_b_latch = (int8_t)read_u8(cur);
    apu->soundcnt_l = read_u16(cur);
    apu->soundcnt_h = read_u16(cur);
    apu->soundcnt_x = read_u16(cur);
    apu->soundbias = read_u16(cur);
    apu->frame_seq_step = read_u8(cur);
    apu->frame_seq_timer = read_u32(cur);
    apu->sample_timer = read_u32(cur);
    apu->sample_period = read_u32(cur);
    apu->prev_left = (int16_t)read_u16(cur);
    apu->prev_right = (int16_t)read_u16(cur);
}

static void load_tmr_chunk(const uint8_t** cur, Timer timers[4]) {
    for (int i = 0; i < 4; i++) {
        Timer* t = &timers[i];
        t->counter = read_u16(cur);
        t->reload = read_u16(cur);
        t->control = read_u16(cur);
        t->prescaler = read_u16(cur);
        t->cascade = read_u8(cur) != 0;
        t->irq_enable = read_u8(cur) != 0;
        t->enabled = read_u8(cur) != 0;
        t->prescaler_counter = read_u32(cur);
    }
}

static void load_irq_chunk(const uint8_t** cur, InterruptController* ic) {
    ic->ime = read_u8(cur) != 0;
    ic->ie = read_u16(cur);
    ic->irf = read_u16(cur);
}

static void load_cart_chunk(const uint8_t** cur, Cartridge* cart) {
    cart->save_type = (SaveType)read_u8(cur);
    read_bytes(cur, cart->flash.data, 0x20000);
    cart->flash.state = (FlashState)read_u8(cur);
    cart->flash.bank = read_u8(cur);
    cart->flash.manufacturer = read_u8(cur);
    cart->flash.device = read_u8(cur);
    read_bytes(cur, cart->sram, 0x8000);

    /* GPIO registers (6 bytes) */
    cart->gpio.data = read_u16(cur);
    cart->gpio.direction = read_u16(cur);
    cart->gpio.control = read_u16(cur);

    /* RTC protocol state (22 bytes) */
    cart->rtc.phase = (RTCPhase)read_u8(cur);
    cart->rtc.cmd_byte = read_u8(cur);
    cart->rtc.cmd_bits = read_u8(cur);
    read_bytes(cur, cart->rtc.payload, 8);
    cart->rtc.payload_len = read_u8(cur);
    cart->rtc.payload_byte = read_u8(cur);
    cart->rtc.payload_bit = read_u8(cur);
    cart->rtc.status_reg = read_u8(cur);
    cart->rtc.offset_secs = (int64_t)read_u64(cur);

    /* Re-seed edge-detection shadow from restored pin state so the next gpio_write
     * doesn't fabricate a spurious CS/SCK rising edge. bit 0 = SCK, bit 2 = CS. */
    cart->rtc.prev_cs = (cart->gpio.data >> 2) & 1;
    cart->rtc.prev_sck = cart->gpio.data & 1;
    cart->rtc.sio_out = 0;

    /* EEPROM data + protocol state. */
    read_bytes(cur, cart->eeprom.data, EEPROM_MAX_SIZE);
    cart->eeprom.addr_bits = read_u8(cur);
    cart->eeprom.state = (EEPROMState)read_u8(cur);
    cart->eeprom.addr = read_u16(cur);
    cart->eeprom.rx_count = read_u16(cur);
    cart->eeprom.expected_dma = read_u16(cur);
    cart->eeprom.shift = read_u64(cur);
    cart->eeprom.tx_count = read_u16(cur);
    cart->eeprom.dirty = read_u8(cur) != 0;
}

static void load_inpt_chunk(const uint8_t** cur, InputState* input) {
    input->keyinput = read_u16(cur);
    input->keycnt = read_u16(cur);
}

static void load_sio_chunk(const uint8_t** cur, SIO* sio) {
    /* Pointer fields (interrupts, peer) are NOT in the byte stream — they
     * are re-attached by the caller after this function returns. */
    for (int i = 0; i < 4; i++) sio->siomulti[i] = read_u16(cur);
    sio->siocnt = read_u16(cur);
    sio->siomlt_send = read_u16(cur);
    sio->rcnt = read_u16(cur);
    sio->siodata32 = read_u32(cur);
    sio->mode = (SioMode)read_u8(cur);
    sio->serial_mode_enabled = read_u8(cur) != 0;
    sio->transfer_active = read_u8(cur) != 0;
    sio->transfer_cycles_remaining = (int32_t)read_u32(cur);
}

/* -------------------------------------------------------------------------- */
/*  Cached ROM CRC32 (computed once, reused across save/load calls)           */
/* -------------------------------------------------------------------------- */
static uint32_t cached_rom_crc = 0;
static const uint8_t* cached_rom_ptr = NULL;
static uint32_t cached_rom_size = 0;

static uint32_t get_rom_crc(const Cartridge* cart) {
    if (cart->rom && cart->rom_size > 0 &&
        cart->rom == cached_rom_ptr && cart->rom_size == cached_rom_size) {
        return cached_rom_crc;
    }
    if (!cart->rom || cart->rom_size == 0) return 0;
    cached_rom_crc = crc32(cart->rom, cart->rom_size);
    cached_rom_ptr = cart->rom;
    cached_rom_size = cart->rom_size;
    return cached_rom_crc;
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                */
/* -------------------------------------------------------------------------- */
SaveStateResult savestate_save_to_buffer(GBA* gba, uint8_t** out, size_t* out_size) {
    if (!gba || !out || !out_size) return SS_ERR_FILE_OPEN;
    *out = NULL;
    *out_size = 0;

    WriteBuffer wb;
    if (!wbuf_init(&wb, 512 * 1024)) {
        LOG_ERROR("Save state: allocation failed");
        return SS_ERR_FILE_WRITE;
    }

    /* Reserve space for the header — we'll fill it in at the end */
    wb.size = HEADER_SIZE;
    memset(wb.data, 0, HEADER_SIZE);

    /* Write all chunks */
    save_gba_chunk(&wb, gba);
    save_cpu_chunk(&wb, &gba->cpu);
    save_bram_chunk(&wb, &gba->bus);
    save_dma_chunk(&wb, &gba->dma);
    save_ppu_chunk(&wb, &gba->ppu);
    save_apu_chunk(&wb, &gba->apu);
    save_tmr_chunk(&wb, gba->timers);
    save_irq_chunk(&wb, &gba->interrupts);
    save_cart_chunk(&wb, &gba->cart);
    save_inpt_chunk(&wb, &gba->input);
    save_sio_chunk(&wb, &gba->sio);

    /* Check for OOM during serialization */
    if (wb.oom) {
        LOG_ERROR("Save state: out of memory during serialization");
        wbuf_free(&wb);
        return SS_ERR_FILE_WRITE;
    }

    /* Compute CRC32 of all data after the header */
    uint32_t data_crc = crc32(wb.data + HEADER_SIZE, wb.size - HEADER_SIZE);

    uint32_t rom_crc = get_rom_crc(&gba->cart);

    /* Fill in the 48-byte header */
    uint8_t* h = wb.data;
    h[0] = (uint8_t)(SAVESTATE_MAGIC & 0xFF);
    h[1] = (uint8_t)((SAVESTATE_MAGIC >> 8) & 0xFF);
    h[2] = (uint8_t)((SAVESTATE_MAGIC >> 16) & 0xFF);
    h[3] = (uint8_t)((SAVESTATE_MAGIC >> 24) & 0xFF);
    h[4] = (uint8_t)(SAVESTATE_VERSION & 0xFF);
    h[5] = (uint8_t)((SAVESTATE_VERSION >> 8) & 0xFF);
    h[6] = (uint8_t)((SAVESTATE_VERSION >> 16) & 0xFF);
    h[7] = (uint8_t)((SAVESTATE_VERSION >> 24) & 0xFF);
    uint32_t total_size = (uint32_t)wb.size;
    h[8]  = (uint8_t)(total_size & 0xFF);
    h[9]  = (uint8_t)((total_size >> 8) & 0xFF);
    h[10] = (uint8_t)((total_size >> 16) & 0xFF);
    h[11] = (uint8_t)((total_size >> 24) & 0xFF);
    h[12] = (uint8_t)(data_crc & 0xFF);
    h[13] = (uint8_t)((data_crc >> 8) & 0xFF);
    h[14] = (uint8_t)((data_crc >> 16) & 0xFF);
    h[15] = (uint8_t)((data_crc >> 24) & 0xFF);
    h[16] = (uint8_t)(rom_crc & 0xFF);
    h[17] = (uint8_t)((rom_crc >> 8) & 0xFF);
    h[18] = (uint8_t)((rom_crc >> 16) & 0xFF);
    h[19] = (uint8_t)((rom_crc >> 24) & 0xFF);
    h[20] = (uint8_t)(gba->cart.rom_size & 0xFF);
    h[21] = (uint8_t)((gba->cart.rom_size >> 8) & 0xFF);
    h[22] = (uint8_t)((gba->cart.rom_size >> 16) & 0xFF);
    h[23] = (uint8_t)((gba->cart.rom_size >> 24) & 0xFF);
    memset(h + HEADER_OFF_TITLE, 0, 12);
    strncpy((char*)(h + HEADER_OFF_TITLE), gba->cart.title, 12);
    uint64_t ts = (uint64_t)time(NULL);
    h[36] = (uint8_t)(ts & 0xFF);
    h[37] = (uint8_t)((ts >> 8) & 0xFF);
    h[38] = (uint8_t)((ts >> 16) & 0xFF);
    h[39] = (uint8_t)((ts >> 24) & 0xFF);
    h[40] = (uint8_t)((ts >> 32) & 0xFF);
    h[41] = (uint8_t)((ts >> 40) & 0xFF);
    h[42] = (uint8_t)((ts >> 48) & 0xFF);
    h[43] = (uint8_t)((ts >> 56) & 0xFF);
    /* Reserved */
    memset(h + HEADER_OFF_RSVD, 0, 4);

    /* Hand ownership of the buffer to the caller — caller frees with free(). */
    *out = wb.data;
    *out_size = wb.size;
    return SS_OK;
}

SaveStateResult savestate_load_from_buffer(GBA* gba, const uint8_t* buf, size_t size) {
    if (!gba || !buf) return SS_ERR_FILE_READ;

    if (size < HEADER_SIZE) {
        LOG_ERROR("Save state: buffer too small (%zu bytes)", size);
        return SS_ERR_TRUNCATED;
    }

    /* Parse header */
    const uint8_t* hdr = buf;
    uint32_t magic = (uint32_t)hdr[0]
                   | ((uint32_t)hdr[1] << 8)
                   | ((uint32_t)hdr[2] << 16)
                   | ((uint32_t)hdr[3] << 24);
    if (magic != SAVESTATE_MAGIC) {
        LOG_ERROR("Save state: bad magic 0x%08X (expected 0x%08X)", magic, SAVESTATE_MAGIC);
        return SS_ERR_BAD_MAGIC;
    }

    uint32_t version = (uint32_t)hdr[4]
                     | ((uint32_t)hdr[5] << 8)
                     | ((uint32_t)hdr[6] << 16)
                     | ((uint32_t)hdr[7] << 24);
    if (version != SAVESTATE_VERSION) {
        LOG_ERROR("Save state: version %u not supported (expected %u)", version, SAVESTATE_VERSION);
        return SS_ERR_BAD_VERSION;
    }

    uint32_t total_size = (uint32_t)hdr[8]
                        | ((uint32_t)hdr[9] << 8)
                        | ((uint32_t)hdr[10] << 16)
                        | ((uint32_t)hdr[11] << 24);
    if (total_size != (uint32_t)size) {
        LOG_ERROR("Save state: size mismatch (header=%u, buffer=%zu)", total_size, size);
        return SS_ERR_CORRUPT;
    }

    uint32_t stored_crc = (uint32_t)hdr[12]
                        | ((uint32_t)hdr[13] << 8)
                        | ((uint32_t)hdr[14] << 16)
                        | ((uint32_t)hdr[15] << 24);

    uint32_t stored_rom_crc = (uint32_t)hdr[16]
                            | ((uint32_t)hdr[17] << 8)
                            | ((uint32_t)hdr[18] << 16)
                            | ((uint32_t)hdr[19] << 24);

    uint32_t rom_crc = get_rom_crc(&gba->cart);
    if (rom_crc != stored_rom_crc) {
        LOG_ERROR("Save state: ROM mismatch (state=0x%08X, loaded=0x%08X)",
                  stored_rom_crc, rom_crc);
        return SS_ERR_ROM_MISMATCH;
    }

    /* Validate data CRC */
    uint32_t computed_crc = crc32(buf + HEADER_SIZE, size - HEADER_SIZE);
    if (computed_crc != stored_crc) {
        LOG_ERROR("Save state: CRC mismatch (stored=0x%08X, computed=0x%08X)",
                  stored_crc, computed_crc);
        return SS_ERR_CORRUPT;
    }

    /* Parse chunks */
    const uint8_t* cur = buf + HEADER_SIZE;
    const uint8_t* end = buf + size;

    while (cur + 8 <= end) {
        uint32_t chunk_id = (uint32_t)cur[0]
                          | ((uint32_t)cur[1] << 8)
                          | ((uint32_t)cur[2] << 16)
                          | ((uint32_t)cur[3] << 24);
        uint32_t chunk_size = (uint32_t)cur[4]
                            | ((uint32_t)cur[5] << 8)
                            | ((uint32_t)cur[6] << 16)
                            | ((uint32_t)cur[7] << 24);
        cur += 8;

        if (cur + chunk_size > end) {
            LOG_ERROR("Save state: chunk 0x%08X truncated (need %u, have %td)",
                      chunk_id, chunk_size, end - cur);
            return SS_ERR_TRUNCATED;
        }

        const uint8_t* chunk_data = cur;

        switch (chunk_id) {
        case CHUNK_GBA:  load_gba_chunk(&chunk_data, gba);              break;
        case CHUNK_CPU:  load_cpu_chunk(&chunk_data, &gba->cpu);        break;
        case CHUNK_BRAM: load_bram_chunk(&chunk_data, &gba->bus);       break;
        case CHUNK_DMA:  load_dma_chunk(&chunk_data, &gba->dma);        break;
        case CHUNK_PPU:  load_ppu_chunk(&chunk_data, &gba->ppu);        break;
        case CHUNK_APU:  load_apu_chunk(&chunk_data, &gba->apu);        break;
        case CHUNK_TMR:  load_tmr_chunk(&chunk_data, gba->timers);      break;
        case CHUNK_IRQ:  load_irq_chunk(&chunk_data, &gba->interrupts); break;
        case CHUNK_CART: load_cart_chunk(&chunk_data, &gba->cart);       break;
        case CHUNK_INPT: load_inpt_chunk(&chunk_data, &gba->input);     break;
        case CHUNK_SIO:  load_sio_chunk(&chunk_data, &gba->sio);        break;
        default:
            LOG_WARN("Save state: skipping unknown chunk 0x%08X (%u bytes)",
                     chunk_id, chunk_size);
            break;
        }

        /* Advance past chunk data regardless of how much the loader consumed */
        cur += chunk_size;
    }

    /* Reset APU ring buffer positions to avoid stale audio data */
    gba->apu.write_pos = 0;
    gba->apu.read_pos = 0;
    memset(gba->apu.sample_buffer, 0, sizeof(gba->apu.sample_buffer));

    /* Re-attach SIO cross-pointers. The deserialized SIO struct's pointer
     * fields contain whatever was in memory before load_sio_chunk ran (or
     * nothing if no SIO chunk was present); either way, restore them now.
     * - interrupts: always points at the host GBA's IRQ controller.
     * - peer: cleared. A live link transport that was attached pre-save is
     *   not restored — savestate-while-link-active is a degenerate case,
     *   and the user must re-issue gba_set_link_peer if they want to
     *   reconnect after loading. */
    gba->sio.interrupts = &gba->interrupts;
    gba->sio.peer = NULL;

    return SS_OK;
}

SaveStateResult savestate_save(GBA* gba, const char* path) {
    uint8_t* buf = NULL;
    size_t   buf_size = 0;
    SaveStateResult r = savestate_save_to_buffer(gba, &buf, &buf_size);
    if (r != SS_OK) return r;

    /* Ensure saves directory exists (legacy path; harmless for ROM-adjacent paths) */
    mkdir("saves", 0755);

    FILE* f = fopen(path, "wb");
    if (!f) {
        LOG_ERROR("Save state: cannot open '%s' for writing", path);
        free(buf);
        return SS_ERR_FILE_OPEN;
    }

    size_t written = fwrite(buf, 1, buf_size, f);
    fclose(f);

    if (written != buf_size) {
        LOG_ERROR("Save state: write error (wrote %zu of %zu bytes)", written, buf_size);
        free(buf);
        return SS_ERR_FILE_WRITE;
    }

    LOG_INFO("Save state written to '%s' (%zu bytes)", path, buf_size);
    free(buf);
    return SS_OK;
}

SaveStateResult savestate_load(GBA* gba, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("Save state: cannot open '%s' for reading", path);
        return SS_ERR_FILE_OPEN;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_len < HEADER_SIZE) {
        LOG_ERROR("Save state: file too small (%ld bytes)", file_len);
        fclose(f);
        return SS_ERR_TRUNCATED;
    }

    /* Read entire file */
    uint8_t* buf = (uint8_t*)malloc((size_t)file_len);
    if (!buf) {
        LOG_ERROR("Save state: allocation failed for %ld bytes", file_len);
        fclose(f);
        return SS_ERR_FILE_READ;
    }

    size_t bytes_read = fread(buf, 1, (size_t)file_len, f);
    fclose(f);

    if (bytes_read != (size_t)file_len) {
        LOG_ERROR("Save state: read error (got %zu of %ld bytes)", bytes_read, file_len);
        free(buf);
        return SS_ERR_FILE_READ;
    }

    SaveStateResult r = savestate_load_from_buffer(gba, buf, (size_t)file_len);
    free(buf);
    if (r == SS_OK) LOG_INFO("Save state loaded from '%s'", path);
    return r;
}

void savestate_slot_path(const char* rom_path, int32_t slot, char* out, size_t out_size) {
    /* Savestate sits next to the ROM (append .ss<slot>). Works from any CWD. */
    snprintf(out, out_size, "%s.ss%d", rom_path, slot);
}
