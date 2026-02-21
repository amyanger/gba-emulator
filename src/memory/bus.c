#include "bus.h"
#include "io_regs.h"
#include "dma.h"
#include "ppu/ppu.h"
#include "cartridge/cartridge.h"
#include "interrupt/interrupt.h"
#include "timer/timer.h"
#include "input/input.h"
#include "cpu/arm7tdmi.h"

/* ===== I/O Register Dispatch =====
 *
 * The GBA maps hardware subsystem registers into the 0x04000000-0x040003FF
 * address range. Reads and writes to these addresses must be dispatched to the
 * owning subsystem rather than hitting a raw byte array, because subsystem
 * state can change between writes (e.g. timer counters tick independently).
 *
 * Registers not yet dispatched to a subsystem fall through to the raw
 * io_regs[] backing array. This allows incremental subsystem bring-up.
 */

/* ----- I/O read (byte-level) ----- */
static uint8_t io_read8(Bus* bus, uint32_t addr) {
    uint32_t offset = addr & 0x3FF;

    switch (offset) {

    /* --- PPU Registers (0x00-0x07) --- */
    case 0x00:  /* REG_DISPCNT low byte */
        if (bus->ppu) return (uint8_t)(bus->ppu->dispcnt);
        return bus->io_regs[offset];
    case 0x01:  /* REG_DISPCNT high byte */
        if (bus->ppu) return (uint8_t)(bus->ppu->dispcnt >> 8);
        return bus->io_regs[offset];
    case 0x04:  /* REG_DISPSTAT low byte */
        if (bus->ppu) return (uint8_t)(bus->ppu->dispstat);
        return bus->io_regs[offset];
    case 0x05:  /* REG_DISPSTAT high byte */
        if (bus->ppu) return (uint8_t)(bus->ppu->dispstat >> 8);
        return bus->io_regs[offset];
    case 0x06:  /* REG_VCOUNT low byte */
        if (bus->ppu) return (uint8_t)(bus->ppu->vcount);
        return bus->io_regs[offset];
    case 0x07:  /* REG_VCOUNT high byte */
        if (bus->ppu) return (uint8_t)(bus->ppu->vcount >> 8);
        return bus->io_regs[offset];

    /* --- BG Control Registers (0x08-0x0F) --- */
    case 0x08: case 0x09:  /* BG0CNT */
    case 0x0A: case 0x0B:  /* BG1CNT */
    case 0x0C: case 0x0D:  /* BG2CNT */
    case 0x0E: case 0x0F:  /* BG3CNT */
    {
        if (!bus->ppu) return bus->io_regs[offset];
        /* bg_cnt index: each register is 2 bytes starting at 0x08 */
        uint32_t bg_idx = (offset - 0x08) >> 1;
        if (offset & 1) {
            return (uint8_t)(bus->ppu->bg_cnt[bg_idx] >> 8);
        }
        return (uint8_t)(bus->ppu->bg_cnt[bg_idx]);
    }

    /* HOFS/VOFS (0x10-0x1F) are write-only — reads fall through to default */

    /* --- Interrupt Controller (0x200-0x209) --- */
    case 0x200:  /* REG_IE low byte */
        if (bus->interrupts) {
            return (uint8_t)(bus->interrupts->ie);
        }
        return bus->io_regs[offset];
    case 0x201:  /* REG_IE high byte */
        if (bus->interrupts) {
            return (uint8_t)(bus->interrupts->ie >> 8);
        }
        return bus->io_regs[offset];
    case 0x202:  /* REG_IF low byte */
        if (bus->interrupts) {
            return (uint8_t)(bus->interrupts->irf);
        }
        return bus->io_regs[offset];
    case 0x203:  /* REG_IF high byte */
        if (bus->interrupts) {
            return (uint8_t)(bus->interrupts->irf >> 8);
        }
        return bus->io_regs[offset];
    case 0x204:  /* REG_WAITCNT low byte (stub — read from io_regs) */
    case 0x205:  /* REG_WAITCNT high byte */
        return bus->io_regs[offset];
    case 0x208:  /* REG_IME low byte (only bit 0 matters) */
        if (bus->interrupts) {
            return (uint8_t)(bus->interrupts->ime ? 1 : 0);
        }
        return bus->io_regs[offset];
    case 0x209:  /* REG_IME high byte (always 0) */
        return 0;

    /* --- Timer Counters (0x100-0x10F) --- */
    case 0x100: case 0x101:  /* TM0CNT_L */
    case 0x104: case 0x105:  /* TM1CNT_L */
    case 0x108: case 0x109:  /* TM2CNT_L */
    case 0x10C: case 0x10D:  /* TM3CNT_L */
    {
        if (!bus->timers) {
            return bus->io_regs[offset];
        }
        /* Timer index: (offset - 0x100) / 4 */
        uint32_t timer_idx = (offset - 0x100) / 4;
        uint16_t counter = timer_read_counter(&bus->timers[timer_idx]);
        /* Even offset = low byte, odd = high byte */
        if (offset & 1) {
            return (uint8_t)(counter >> 8);
        }
        return (uint8_t)(counter);
    }
    case 0x102: case 0x103:  /* TM0CNT_H */
    case 0x106: case 0x107:  /* TM1CNT_H */
    case 0x10A: case 0x10B:  /* TM2CNT_H */
    case 0x10E: case 0x10F:  /* TM3CNT_H */
    {
        if (!bus->timers) {
            return bus->io_regs[offset];
        }
        /* Timer index from control register offset */
        uint32_t timer_idx = (offset - 0x102) / 4;
        uint16_t control = bus->timers[timer_idx].control;
        if (offset & 1) {
            return (uint8_t)(control >> 8);
        }
        return (uint8_t)(control);
    }

    /* --- Input (0x130-0x133) --- */
    case 0x130:  /* REG_KEYINPUT low byte */
        if (bus->input) {
            return (uint8_t)(bus->input->keyinput);
        }
        return bus->io_regs[offset];
    case 0x131:  /* REG_KEYINPUT high byte */
        if (bus->input) {
            return (uint8_t)(bus->input->keyinput >> 8);
        }
        return bus->io_regs[offset];
    case 0x132:  /* REG_KEYCNT low byte */
        if (bus->input) {
            return (uint8_t)(bus->input->keycnt);
        }
        return bus->io_regs[offset];
    case 0x133:  /* REG_KEYCNT high byte */
        if (bus->input) {
            return (uint8_t)(bus->input->keycnt >> 8);
        }
        return bus->io_regs[offset];

    /* --- All other I/O: raw backing array --- */
    default:
        return bus->io_regs[offset];
    }
}

/* ----- I/O write (byte-level) ----- */
static void io_write8(Bus* bus, uint32_t addr, uint8_t val) {
    uint32_t offset = addr & 0x3FF;

    switch (offset) {

    /* --- PPU Registers (0x00-0x07) --- */
    case 0x00:  /* REG_DISPCNT low byte */
        bus->io_regs[offset] = val;
        if (bus->ppu) {
            bus->ppu->dispcnt = (bus->ppu->dispcnt & 0xFF00) | (uint16_t)val;
        }
        return;
    case 0x01:  /* REG_DISPCNT high byte */
        bus->io_regs[offset] = val;
        if (bus->ppu) {
            bus->ppu->dispcnt = (bus->ppu->dispcnt & 0x00FF)
                              | ((uint16_t)val << 8);
        }
        return;
    case 0x04:  /* REG_DISPSTAT low byte — bits 0-2 are read-only */
        if (bus->ppu) {
            /* Preserve read-only flags (VBlank, HBlank, VCount match) */
            bus->ppu->dispstat = (bus->ppu->dispstat & 0xFF07)
                               | ((uint16_t)(val & 0xF8));
            bus->io_regs[offset] = (uint8_t)(bus->ppu->dispstat & 0xFF);
        } else {
            bus->io_regs[offset] = val;
        }
        return;
    case 0x05:  /* REG_DISPSTAT high byte (VCount target + upper bits) */
        bus->io_regs[offset] = val;
        if (bus->ppu) {
            bus->ppu->dispstat = (bus->ppu->dispstat & 0x00FF)
                               | ((uint16_t)val << 8);
        }
        return;
    case 0x06:  /* REG_VCOUNT low byte — read-only, ignore writes */
    case 0x07:  /* REG_VCOUNT high byte — read-only, ignore writes */
        return;

    /* --- BG Control Registers (0x08-0x0F) --- */
    case 0x08: case 0x09:  /* BG0CNT */
    case 0x0A: case 0x0B:  /* BG1CNT */
    case 0x0C: case 0x0D:  /* BG2CNT */
    case 0x0E: case 0x0F:  /* BG3CNT */
    {
        bus->io_regs[offset] = val;
        if (!bus->ppu) return;
        uint32_t bg_idx = (offset - 0x08) >> 1;
        if (offset & 1) {
            bus->ppu->bg_cnt[bg_idx] = (bus->ppu->bg_cnt[bg_idx] & 0x00FF)
                                     | ((uint16_t)val << 8);
        } else {
            bus->ppu->bg_cnt[bg_idx] = (bus->ppu->bg_cnt[bg_idx] & 0xFF00)
                                     | (uint16_t)val;
        }
        return;
    }

    /* --- BG Scroll Registers (0x10-0x1F) — write-only --- */
    case 0x10: case 0x11:  /* BG0HOFS */
    case 0x12: case 0x13:  /* BG0VOFS */
    case 0x14: case 0x15:  /* BG1HOFS */
    case 0x16: case 0x17:  /* BG1VOFS */
    case 0x18: case 0x19:  /* BG2HOFS */
    case 0x1A: case 0x1B:  /* BG2VOFS */
    case 0x1C: case 0x1D:  /* BG3HOFS */
    case 0x1E: case 0x1F:  /* BG3VOFS */
    {
        bus->io_regs[offset] = val;
        if (!bus->ppu) return;
        /*
         * Scroll register layout: 4 BGs x (HOFS, VOFS) = 8 registers, 2 bytes each.
         * Offset 0x10 = BG0HOFS, 0x12 = BG0VOFS, 0x14 = BG1HOFS, ...
         * BG index: (offset - 0x10) / 4
         * HOFS vs VOFS: bit 1 of (offset - 0x10) selects VOFS
         */
        uint32_t rel = offset - 0x10;
        uint32_t bg_idx = rel >> 2;
        bool is_vofs = (rel >> 1) & 1;
        uint16_t* reg = is_vofs ? &bus->ppu->bg_vofs[bg_idx]
                                : &bus->ppu->bg_hofs[bg_idx];
        if (offset & 1) {
            *reg = (*reg & 0x00FF) | ((uint16_t)val << 8);
        } else {
            *reg = (*reg & 0xFF00) | (uint16_t)val;
        }
        return;
    }

    /* --- Interrupt Controller (0x200-0x209) --- */
    case 0x200:  /* REG_IE low byte */
        bus->io_regs[offset] = val;
        if (bus->interrupts) {
            bus->interrupts->ie = (bus->interrupts->ie & 0xFF00) | (uint16_t)val;
        }
        return;
    case 0x201:  /* REG_IE high byte */
        bus->io_regs[offset] = val;
        if (bus->interrupts) {
            bus->interrupts->ie = (bus->interrupts->ie & 0x00FF)
                                | ((uint16_t)val << 8);
        }
        return;
    case 0x202:  /* REG_IF low byte — writing 1 clears */
        /* Do NOT update io_regs; IF is read from subsystem state */
        if (bus->interrupts) {
            interrupt_acknowledge(bus->interrupts, (uint16_t)val);
        }
        return;
    case 0x203:  /* REG_IF high byte — writing 1 clears */
        if (bus->interrupts) {
            interrupt_acknowledge(bus->interrupts, (uint16_t)val << 8);
        }
        return;
    case 0x204:  /* REG_WAITCNT low byte (stub) */
    case 0x205:  /* REG_WAITCNT high byte (stub) */
        bus->io_regs[offset] = val;
        return;
    case 0x208:  /* REG_IME low byte */
        bus->io_regs[offset] = val & 1;
        if (bus->interrupts) {
            bus->interrupts->ime = (val & 1) != 0;
        }
        return;
    case 0x209:  /* REG_IME high byte — ignored */
        bus->io_regs[offset] = 0;
        return;

    /* --- Timer Reload / Control (0x100-0x10F) --- */
    case 0x100: case 0x101:  /* TM0CNT_L */
    case 0x104: case 0x105:  /* TM1CNT_L */
    case 0x108: case 0x109:  /* TM2CNT_L */
    case 0x10C: case 0x10D:  /* TM3CNT_L */
    {
        bus->io_regs[offset] = val;
        if (!bus->timers) {
            return;
        }
        /* Only act when the high byte is written (completes the 16-bit value) */
        if (offset & 1) {
            uint32_t lo_offset = offset & ~1u;
            uint16_t reload = (uint16_t)bus->io_regs[lo_offset]
                            | ((uint16_t)bus->io_regs[lo_offset + 1] << 8);
            uint32_t timer_idx = (lo_offset - 0x100) / 4;
            timer_write_reload(&bus->timers[timer_idx], reload);
        }
        return;
    }
    case 0x102: case 0x103:  /* TM0CNT_H */
    case 0x106: case 0x107:  /* TM1CNT_H */
    case 0x10A: case 0x10B:  /* TM2CNT_H */
    case 0x10E: case 0x10F:  /* TM3CNT_H */
    {
        bus->io_regs[offset] = val;
        if (!bus->timers) {
            return;
        }
        /* Only act when the high byte is written (completes the 16-bit value) */
        if (offset & 1) {
            uint32_t lo_offset = offset & ~1u;
            uint16_t control = (uint16_t)bus->io_regs[lo_offset]
                             | ((uint16_t)bus->io_regs[lo_offset + 1] << 8);
            uint32_t timer_idx = (lo_offset - 0x102) / 4;
            timer_write_control(&bus->timers[timer_idx], control);
        }
        return;
    }

    /* --- DMA Registers (0xB0-0xDF) --- */
    case 0xBB:  /* DMA0 CNT_H high byte */
    case 0xC7:  /* DMA1 CNT_H high byte */
    case 0xD3:  /* DMA2 CNT_H high byte */
    case 0xDF:  /* DMA3 CNT_H high byte */
    {
        bus->io_regs[offset] = val;
        if (!bus->dma) {
            return;
        }
        /*
         * Determine the DMA channel and reconstruct the full 16-bit CNT_H.
         * DMA register blocks are 12 bytes each starting at 0xB0:
         *   DMA0: 0xB0-0xBB, CNT_H at 0xBA-0xBB
         *   DMA1: 0xBC-0xC7, CNT_H at 0xC6-0xC7
         *   DMA2: 0xC8-0xD3, CNT_H at 0xD2-0xD3
         *   DMA3: 0xD4-0xDF, CNT_H at 0xDE-0xDF
         */
        uint32_t cnt_h_lo = offset - 1;  /* Low byte of CNT_H */
        uint16_t control = (uint16_t)bus->io_regs[cnt_h_lo]
                         | ((uint16_t)val << 8);

        int ch;
        if (offset == 0xBB) {
            ch = 0;
        } else if (offset == 0xC7) {
            ch = 1;
        } else if (offset == 0xD3) {
            ch = 2;
        } else {
            ch = 3;
        }

        /* Latch SAD, DAD, and count from io_regs before writing control.
         * dma_write_control will latch source/dest on the rising edge of
         * the enable bit.
         */
        uint32_t base;
        if (ch == 0) { base = 0xB0; }
        else if (ch == 1) { base = 0xBC; }
        else if (ch == 2) { base = 0xC8; }
        else { base = 0xD4; }

        DMAChannel* dc = &bus->dma->channels[ch];
        dc->source_latch =
              (uint32_t)bus->io_regs[base]
            | ((uint32_t)bus->io_regs[base + 1] << 8)
            | ((uint32_t)bus->io_regs[base + 2] << 16)
            | ((uint32_t)bus->io_regs[base + 3] << 24);
        dc->dest_latch =
              (uint32_t)bus->io_regs[base + 4]
            | ((uint32_t)bus->io_regs[base + 5] << 8)
            | ((uint32_t)bus->io_regs[base + 6] << 16)
            | ((uint32_t)bus->io_regs[base + 7] << 24);
        dc->count =
              (uint16_t)bus->io_regs[base + 8]
            | ((uint16_t)bus->io_regs[base + 9] << 8);

        dma_write_control(bus->dma, ch, control);
        return;
    }
    /* DMA registers (not CNT_H high byte) — just store in io_regs */
    case 0xB0: case 0xB1: case 0xB2: case 0xB3:  /* DMA0 SAD */
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:  /* DMA0 DAD */
    case 0xB8: case 0xB9: case 0xBA:              /* DMA0 CNT_L + CNT_H lo */
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:  /* DMA1 SAD */
    case 0xC0: case 0xC1: case 0xC2: case 0xC3:  /* DMA1 DAD */
    case 0xC4: case 0xC5: case 0xC6:              /* DMA1 CNT_L + CNT_H lo */
    case 0xC8: case 0xC9: case 0xCA: case 0xCB:  /* DMA2 SAD */
    case 0xCC: case 0xCD: case 0xCE: case 0xCF:  /* DMA2 DAD */
    case 0xD0: case 0xD1: case 0xD2:              /* DMA2 CNT_L + CNT_H lo */
    case 0xD4: case 0xD5: case 0xD6: case 0xD7:  /* DMA3 SAD */
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:  /* DMA3 DAD */
    case 0xDC: case 0xDD: case 0xDE:              /* DMA3 CNT_L + CNT_H lo */
        bus->io_regs[offset] = val;
        return;

    /* --- Input (0x130-0x131) — read-only, writes ignored --- */
    case 0x130:
    case 0x131:
        return;
    /* KEYCNT is writable */
    case 0x132:
        bus->io_regs[offset] = val;
        if (bus->input) {
            bus->input->keycnt = (bus->input->keycnt & 0xFF00) | (uint16_t)val;
        }
        return;
    case 0x133:
        bus->io_regs[offset] = val;
        if (bus->input) {
            bus->input->keycnt = (bus->input->keycnt & 0x00FF)
                               | ((uint16_t)val << 8);
        }
        return;

    /* --- HALTCNT (0x301) — halts CPU until interrupt --- */
    case 0x301:
        bus->io_regs[offset] = val;
        if (bus->cpu) {
            bus->cpu->halted = true;
        }
        return;

    /* --- All other I/O: raw backing array --- */
    default:
        if (offset < IO_SIZE) {
            bus->io_regs[offset] = val;
        }
        return;
    }
}

/* ================================================================= */

void bus_init(Bus* bus) {
    memset(bus->bios, 0, BIOS_SIZE);
    memset(bus->ewram, 0, EWRAM_SIZE);
    memset(bus->iwram, 0, IWRAM_SIZE);
    memset(bus->io_regs, 0, IO_SIZE);
    memset(bus->palette_ram, 0, PALETTE_SIZE);
    memset(bus->vram, 0, VRAM_SIZE);
    memset(bus->oam, 0, OAM_SIZE);

    bus->open_bus = 0;
    bus->bios_readable = true;
    bus->last_bios_read = 0;
}

bool bus_load_bios(Bus* bus, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("Cannot open BIOS file: %s", path);
        return false;
    }

    size_t read = fread(bus->bios, 1, BIOS_SIZE, f);
    fclose(f);

    if (read != BIOS_SIZE) {
        LOG_WARN("BIOS file size mismatch: expected %d, got %zu",
                 BIOS_SIZE, read);
    }

    LOG_INFO("BIOS loaded: %zu bytes", read);
    return true;
}

/* Address decoding -- routes reads/writes to the correct memory region */
static uint32_t decode_region(uint32_t addr) {
    return (addr >> 24) & 0xFF;
}

uint8_t bus_read8(Bus* bus, uint32_t addr) {
    switch (decode_region(addr)) {
    case 0x00: /* BIOS */
        if (addr < BIOS_SIZE) {
            /*
             * BIOS protection: the CPU may only read the BIOS ROM while
             * the program counter is inside the BIOS region. At all other
             * times, reads return the last successfully fetched BIOS word.
             */
            if (bus->cpu &&
                bus->cpu->regs[REG_PC] >= BIOS_SIZE + 8) {
                /* CPU is outside BIOS -- return cached value */
                return (uint8_t)(bus->last_bios_read >> ((addr & 3) * 8));
            }
            /* CPU is in BIOS (or no CPU wired yet) -- allow read and cache */
            uint32_t aligned = addr & ~3u;
            bus->last_bios_read =
                  (uint32_t)bus->bios[aligned]
                | ((uint32_t)bus->bios[aligned + 1] << 8)
                | ((uint32_t)bus->bios[aligned + 2] << 16)
                | ((uint32_t)bus->bios[aligned + 3] << 24);
            return bus->bios[addr];
        }
        return 0;

    case 0x02: /* EWRAM (mirrored) */
        return bus->ewram[addr & (EWRAM_SIZE - 1)];

    case 0x03: /* IWRAM (mirrored) */
        return bus->iwram[addr & (IWRAM_SIZE - 1)];

    case 0x04: /* I/O Registers */
        if ((addr & 0xFFFFFF) < IO_SIZE) {
            return io_read8(bus, addr);
        }
        return 0;

    case 0x05: /* Palette RAM (mirrored) */
        return bus->palette_ram[addr & (PALETTE_SIZE - 1)];

    case 0x06: /* VRAM (mirrored with 96KB wrap) */
    {
        uint32_t vram_offset = addr & 0x1FFFF;
        if (vram_offset >= VRAM_SIZE) {
            vram_offset -= 0x8000; /* Mirror 96KB into 128KB space */
        }
        return bus->vram[vram_offset];
    }

    case 0x07: /* OAM (mirrored) */
        return bus->oam[addr & (OAM_SIZE - 1)];

    case 0x08: case 0x09: /* ROM Wait State 0 */
    case 0x0A: case 0x0B: /* ROM Wait State 1 */
    case 0x0C: case 0x0D: /* ROM Wait State 2 */
        if (bus->cart) {
            return cartridge_read8(bus->cart, addr);
        }
        return 0;

    case 0x0E: case 0x0F: /* Cart SRAM/Flash */
        if (bus->cart) {
            return cartridge_read8(bus->cart, addr);
        }
        return 0;

    default:
        return (uint8_t)(bus->open_bus);
    }
}

uint16_t bus_read16(Bus* bus, uint32_t addr) {
    addr &= ~1u; /* Force halfword alignment */
    return (uint16_t)bus_read8(bus, addr)
         | ((uint16_t)bus_read8(bus, addr + 1) << 8);
}

uint32_t bus_read32(Bus* bus, uint32_t addr) {
    addr &= ~3u; /* Force word alignment */
    return (uint32_t)bus_read8(bus, addr)
         | ((uint32_t)bus_read8(bus, addr + 1) << 8)
         | ((uint32_t)bus_read8(bus, addr + 2) << 16)
         | ((uint32_t)bus_read8(bus, addr + 3) << 24);
}

void bus_write8(Bus* bus, uint32_t addr, uint8_t val) {
    switch (decode_region(addr)) {
    case 0x02:
        bus->ewram[addr & (EWRAM_SIZE - 1)] = val;
        break;
    case 0x03:
        bus->iwram[addr & (IWRAM_SIZE - 1)] = val;
        break;
    case 0x04: /* I/O Registers */
        if ((addr & 0xFFFFFF) < IO_SIZE) {
            io_write8(bus, addr, val);
        }
        break;
    case 0x05:
        /* 8-bit writes to palette write both bytes of the halfword */
        {
            uint32_t pal_offset = addr & (PALETTE_SIZE - 1) & ~1u;
            bus->palette_ram[pal_offset] = val;
            bus->palette_ram[pal_offset + 1] = val;
        }
        break;
    case 0x06:
    {
        uint32_t vram_offset = addr & 0x1FFFF;
        if (vram_offset >= VRAM_SIZE) {
            vram_offset -= 0x8000;
        }
        /* 8-bit VRAM writes write both bytes of halfword (in BG modes) */
        vram_offset &= ~1u;
        bus->vram[vram_offset] = val;
        bus->vram[vram_offset + 1] = val;
        break;
    }
    case 0x07:
        /* 8-bit writes to OAM are ignored */
        break;
    case 0x0E: case 0x0F:
        if (bus->cart) {
            cartridge_write8(bus->cart, addr, val);
        }
        break;
    default:
        break;
    }
}

/* Helper: resolve VRAM offset with 96KB mirroring */
static uint32_t vram_offset(uint32_t addr) {
    uint32_t off = addr & 0x1FFFF;
    if (off >= VRAM_SIZE) {
        off -= 0x8000;
    }
    return off;
}

void bus_write16(Bus* bus, uint32_t addr, uint16_t val) {
    addr &= ~1u;
    /* Palette, VRAM, and OAM need direct writes — bus_write8 has special
     * 8-bit behaviour (byte duplication / ignore) that would corrupt wider
     * writes if we decomposed into two bus_write8 calls. */
    switch (decode_region(addr)) {
    case 0x05: { /* Palette RAM — normal 16-bit write */
        uint32_t off = addr & (PALETTE_SIZE - 1);
        bus->palette_ram[off]     = (uint8_t)(val);
        bus->palette_ram[off + 1] = (uint8_t)(val >> 8);
        return;
    }
    case 0x06: { /* VRAM — normal 16-bit write */
        uint32_t off = vram_offset(addr);
        bus->vram[off]     = (uint8_t)(val);
        bus->vram[off + 1] = (uint8_t)(val >> 8);
        return;
    }
    case 0x07: { /* OAM — 16-bit writes are allowed (only 8-bit ignored) */
        uint32_t off = addr & (OAM_SIZE - 1);
        bus->oam[off]     = (uint8_t)(val);
        bus->oam[off + 1] = (uint8_t)(val >> 8);
        return;
    }
    default:
        bus_write8(bus, addr, (uint8_t)(val & 0xFF));
        bus_write8(bus, addr + 1, (uint8_t)(val >> 8));
        return;
    }
}

void bus_write32(Bus* bus, uint32_t addr, uint32_t val) {
    addr &= ~3u;
    switch (decode_region(addr)) {
    case 0x05: { /* Palette RAM -- mirror each byte independently to prevent OOB */
        for (int i = 0; i < 4; i++) {
            uint32_t off = (addr + i) & (PALETTE_SIZE - 1);
            bus->palette_ram[off] = (uint8_t)(val >> (i * 8));
        }
        return;
    }
    case 0x06: { /* VRAM -- mirror each byte independently to prevent OOB */
        for (int i = 0; i < 4; i++) {
            uint32_t off = vram_offset(addr + i);
            bus->vram[off] = (uint8_t)(val >> (i * 8));
        }
        return;
    }
    case 0x07: { /* OAM -- mirror each byte independently to prevent OOB */
        for (int i = 0; i < 4; i++) {
            uint32_t off = (addr + i) & (OAM_SIZE - 1);
            bus->oam[off] = (uint8_t)(val >> (i * 8));
        }
        return;
    }
    default:
        bus_write8(bus, addr, (uint8_t)(val & 0xFF));
        bus_write8(bus, addr + 1, (uint8_t)((val >> 8) & 0xFF));
        bus_write8(bus, addr + 2, (uint8_t)((val >> 16) & 0xFF));
        bus_write8(bus, addr + 3, (uint8_t)((val >> 24) & 0xFF));
        return;
    }
}
