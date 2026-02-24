#include "bios_hle.h"
#include "arm7tdmi.h"
#include "memory/bus.h"
#include "apu/apu.h"
#include <math.h>

/*
 * High-Level Emulation (HLE) of the GBA BIOS SWI functions.
 *
 * When no BIOS ROM is loaded, SWI instructions are intercepted before
 * entering SVC mode.  The CPU registers (R0-R3) hold the arguments
 * exactly as the real BIOS expects, and this module writes the results
 * back into the same registers.
 *
 * All memory access goes through the bus_read / bus_write interface
 * to stay consistent with the hardware model.
 *
 * References: GBATEK "BIOS Functions", tonc BIOS reference.
 */

/* ======================================================================
 * Sin/Cos lookup table for affine computations (256 entries)
 *
 * The GBA BIOS uses a 256-entry table where index 0..255 maps to
 * angle 0..2*pi.  Values are 1.14 fixed-point (14 fraction bits).
 * sin_lut[i] = (int16_t)(sin(i * 2 * PI / 256) * 16384)
 * ====================================================================== */
static const int16_t sin_lut[256] = {
       0,    402,    804,   1205,   1606,   2006,   2404,   2801,
    3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897,
    6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,
    9102,   9434,   9760,  10080,  10394,  10702,  11003,  11297,
   11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,
   13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978,
   15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,
   16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,
   16384,  16379,  16364,  16340,  16305,  16261,  16207,  16143,
   16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,
   15137,  14978,  14811,  14635,  14449,  14256,  14053,  13842,
   13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,
   11585,  11297,  11003,  10702,  10394,  10080,   9760,   9434,
    9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
    6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,
    3196,   2801,   2404,   2006,   1606,   1205,    804,    402,
       0,   -402,   -804,  -1205,  -1606,  -2006,  -2404,  -2801,
   -3196,  -3590,  -3981,  -4370,  -4756,  -5139,  -5520,  -5897,
   -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,
   -9102,  -9434,  -9760, -10080, -10394, -10702, -11003, -11297,
  -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
  -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
  -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
  -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
  -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
  -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
  -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
  -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
  -11585, -11297, -11003, -10702, -10394, -10080,  -9760,  -9434,
   -9102,  -8765,  -8423,  -8076,  -7723,  -7366,  -7005,  -6639,
   -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,
   -3196,  -2801,  -2404,  -2006,  -1606,  -1205,   -804,   -402,
};

static int16_t bios_sin(uint16_t angle) {
    /* Map 16-bit angle [0, 0xFFFF] to 256-entry table index */
    uint8_t idx = (uint8_t)(angle >> 8);
    return sin_lut[idx];
}

static int16_t bios_cos(uint16_t angle) {
    /* cos(x) = sin(x + pi/2) = sin(x + 64 entries) */
    uint16_t shifted = (uint16_t)(angle + 0x4000);
    uint8_t idx = (uint8_t)(shifted >> 8);
    return sin_lut[idx];
}

/* ======================================================================
 * SWI 0x02 - Halt
 * ====================================================================== */
static void swi_halt(ARM7TDMI* cpu) {
    cpu->halted = true;
}

/* ======================================================================
 * SWI 0x04 - IntrWait
 * R0 = discard old flags, R1 = interrupt mask to wait for
 * ====================================================================== */
static void swi_intr_wait(ARM7TDMI* cpu) {
    uint32_t discard = cpu->regs[0];
    uint32_t mask = cpu->regs[1];

    /* IntrCheckFlag address in IWRAM: 0x03007FF8 */
    if (discard & 1) {
        /* Clear the bits we are waiting for in [0x03007FF8] */
        uint32_t old_flags = bus_read32(cpu->bus, 0x03007FF8);
        old_flags &= ~mask;
        bus_write32(cpu->bus, 0x03007FF8, old_flags);
    }

    cpu->halted = true;
}

/* ======================================================================
 * SWI 0x05 - VBlankIntrWait
 * Equivalent to IntrWait(1, 1) — wait for VBlank interrupt
 * ====================================================================== */
static void swi_vblank_intr_wait(ARM7TDMI* cpu) {
    cpu->regs[0] = 1;
    cpu->regs[1] = 1;
    swi_intr_wait(cpu);
}

/* ======================================================================
 * SWI 0x06 - Div
 * R0 = numerator, R1 = denominator
 * Returns: R0 = R0/R1, R1 = R0%R1, R3 = abs(R0/R1)
 * ====================================================================== */
static void swi_div(ARM7TDMI* cpu) {
    int32_t num = (int32_t)cpu->regs[0];
    int32_t den = (int32_t)cpu->regs[1];

    if (den == 0) {
        LOG_WARN("SWI Div: division by zero (num=%d)", num);
        /* GBA BIOS behavior on divide-by-zero is undefined; return safe values */
        cpu->regs[0] = (num < 0) ? (uint32_t)-1 : 1;
        cpu->regs[1] = (uint32_t)num;
        cpu->regs[3] = 1;
        return;
    }

    int32_t quot = num / den;
    int32_t rem = num % den;

    cpu->regs[0] = (uint32_t)quot;
    cpu->regs[1] = (uint32_t)rem;
    cpu->regs[3] = (uint32_t)(quot < 0 ? -quot : quot);
}

/* ======================================================================
 * SWI 0x07 - DivArm
 * Same as Div but arguments swapped: R1/R0
 * ====================================================================== */
static void swi_div_arm(ARM7TDMI* cpu) {
    /* Swap R0 and R1, then call Div */
    uint32_t tmp = cpu->regs[0];
    cpu->regs[0] = cpu->regs[1];
    cpu->regs[1] = tmp;
    swi_div(cpu);
}

/* ======================================================================
 * SWI 0x08 - Sqrt
 * R0 = value, returns R0 = integer square root
 * Bit-by-bit method (Newton's method is also acceptable)
 * ====================================================================== */
static void swi_sqrt(ARM7TDMI* cpu) {
    uint32_t val = cpu->regs[0];
    uint32_t result = 0;
    uint32_t bit = 1u << 30; /* Start with the highest power of 4 <= val */

    while (bit > val) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (val >= result + bit) {
            val -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    cpu->regs[0] = result;
}

/* ======================================================================
 * SWI 0x09 - ArcTan
 * R0 = tan value (1.14 fixed-point)
 * Returns R0 = arctan (1.14 fixed-point, range [-pi/4, pi/4])
 * ====================================================================== */
static void swi_arctan(ARM7TDMI* cpu) {
    /* Use the BIOS polynomial approximation:
     * a = -((r0 * r0) >> 14)
     * r0 = ((0xA9 * a >> 14) + 0x390) * a >> 14 + 0xFFFFA57B) * a >> 14 + 0x3276) * r0 >> 14
     * This matches the real BIOS output. */
    int32_t r0 = (int32_t)(int16_t)(uint16_t)cpu->regs[0];

    int32_t a = -(r0 * r0) >> 14;
    int32_t r = ((0xA9 * a) >> 14) + 0x390;
    r = ((r * a) >> 14) + (int32_t)0xFFFFA57B;
    r = ((r * a) >> 14) + 0x3276;
    r = (r * r0) >> 14;

    cpu->regs[0] = (uint32_t)(int16_t)r;
}

/* ======================================================================
 * SWI 0x0A - ArcTan2
 * R0 = x, R1 = y (both 1.14 fixed-point)
 * Returns R0 = angle [0, 0xFFFF] = [0, 2*pi)
 * ====================================================================== */
static void swi_arctan2(ARM7TDMI* cpu) {
    int16_t x = (int16_t)(uint16_t)cpu->regs[0];
    int16_t y = (int16_t)(uint16_t)cpu->regs[1];

    if (x == 0 && y == 0) {
        cpu->regs[0] = 0;
        return;
    }

    /* Use floating-point atan2 and convert to [0, 0xFFFF] range */
    double angle = atan2((double)y, (double)x);
    if (angle < 0) {
        angle += 2.0 * M_PI;
    }

    /* Map [0, 2*pi) to [0, 0x10000) */
    uint32_t result = (uint32_t)(angle * 65536.0 / (2.0 * M_PI));
    if (result >= 0x10000) {
        result = 0;
    }

    cpu->regs[0] = (uint32_t)(uint16_t)result;
}

/* ======================================================================
 * SWI 0x0B - CpuSet
 * R0 = source, R1 = dest, R2 = control word
 *   Bit 24: fill mode (0 = copy, 1 = fill from first word)
 *   Bit 26: word size (0 = 16-bit, 1 = 32-bit)
 *   Bits [20:0]: count (number of transfers)
 * ====================================================================== */
static void swi_cpu_set(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];
    uint32_t control = cpu->regs[2];

    bool fill = BIT(control, 24);
    bool word_mode = BIT(control, 26);
    uint32_t count = control & 0x1FFFFF;

    Bus* bus = cpu->bus;

    if (word_mode) {
        /* 32-bit transfers */
        src &= ~3u;
        dst &= ~3u;
        uint32_t fill_val = bus_read32(bus, src);

        for (uint32_t i = 0; i < count; i++) {
            uint32_t val = fill ? fill_val : bus_read32(bus, src);
            bus_write32(bus, dst, val);
            if (!fill) {
                src += 4;
            }
            dst += 4;
        }
    } else {
        /* 16-bit transfers */
        src &= ~1u;
        dst &= ~1u;
        uint16_t fill_val = bus_read16(bus, src);

        for (uint32_t i = 0; i < count; i++) {
            uint16_t val = fill ? fill_val : bus_read16(bus, src);
            bus_write16(bus, dst, val);
            if (!fill) {
                src += 2;
            }
            dst += 2;
        }
    }
}

/* ======================================================================
 * SWI 0x0C - CpuFastSet
 * R0 = source, R1 = dest, R2 = control word
 *   Always 32-bit transfers. Bit 24: fill mode.
 *   Count (bits [20:0]) is rounded up to multiple of 8.
 * ====================================================================== */
static void swi_cpu_fast_set(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0] & ~3u;
    uint32_t dst = cpu->regs[1] & ~3u;
    uint32_t control = cpu->regs[2];

    bool fill = BIT(control, 24);
    uint32_t count = control & 0x1FFFFF;

    /* Round up to multiple of 8 */
    count = (count + 7) & ~7u;

    Bus* bus = cpu->bus;
    uint32_t fill_val = bus_read32(bus, src);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t val = fill ? fill_val : bus_read32(bus, src);
        bus_write32(bus, dst, val);
        if (!fill) {
            src += 4;
        }
        dst += 4;
    }
}

/* ======================================================================
 * SWI 0x0D - GetBiosChecksum
 * Returns R0 = 0xBAAE187F (the known BIOS checksum)
 * ====================================================================== */
static void swi_get_bios_checksum(ARM7TDMI* cpu) {
    cpu->regs[0] = 0xBAAE187F;
}

/* ======================================================================
 * SWI 0x0E - BgAffineSet
 * R0 = source data ptr, R1 = dest data ptr, R2 = number of entries
 *
 * Per entry reads from src (28 bytes):
 *   centerX (32-bit), centerY (32-bit),
 *   dispX (16-bit), dispY (16-bit),
 *   scaleX (16-bit), scaleY (16-bit),
 *   angle (16-bit)
 *
 * Per entry writes to dst (20 bytes):
 *   PA (16), PB (16), PC (16), PD (16), startX (32), startY (32)
 * ====================================================================== */
static void swi_bg_affine_set(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];
    uint32_t count = cpu->regs[2];

    Bus* bus = cpu->bus;

    for (uint32_t i = 0; i < count; i++) {
        /* Read source entry (28 bytes) */
        int32_t center_x = (int32_t)bus_read32(bus, src);
        int32_t center_y = (int32_t)bus_read32(bus, src + 4);
        int16_t disp_x = (int16_t)bus_read16(bus, src + 8);
        int16_t disp_y = (int16_t)bus_read16(bus, src + 10);
        int16_t scale_x = (int16_t)bus_read16(bus, src + 12);
        int16_t scale_y = (int16_t)bus_read16(bus, src + 14);
        uint16_t angle = bus_read16(bus, src + 16);
        src += 20; /* Actually GBATEK says 20 bytes per src entry */

        /* Compute sin/cos (1.14 fixed-point) */
        int32_t sin_val = (int32_t)bios_sin(angle);
        int32_t cos_val = (int32_t)bios_cos(angle);

        /* Compute affine parameters:
         * PA = scaleX * cos(angle) >> 14
         * PB = scaleX * -sin(angle) >> 14
         * PC = scaleY * sin(angle) >> 14
         * PD = scaleY * cos(angle) >> 14 */
        int16_t pa = (int16_t)(((int32_t)scale_x * cos_val) >> 14);
        int16_t pb = (int16_t)(((int32_t)scale_x * (-sin_val)) >> 14);
        int16_t pc = (int16_t)(((int32_t)scale_y * sin_val) >> 14);
        int16_t pd = (int16_t)(((int32_t)scale_y * cos_val) >> 14);

        /* startX = centerX - (PA*dispX + PB*dispY) */
        int32_t start_x = center_x -
                           ((int32_t)pa * (int32_t)disp_x +
                            (int32_t)pb * (int32_t)disp_y);

        /* startY = centerY - (PC*dispX + PD*dispY) */
        int32_t start_y = center_y -
                           ((int32_t)pc * (int32_t)disp_x +
                            (int32_t)pd * (int32_t)disp_y);

        /* Write dest entry (20 bytes) */
        bus_write16(bus, dst, (uint16_t)pa);
        bus_write16(bus, dst + 2, (uint16_t)pb);
        bus_write16(bus, dst + 4, (uint16_t)pc);
        bus_write16(bus, dst + 6, (uint16_t)pd);
        bus_write32(bus, dst + 8, (uint32_t)start_x);
        bus_write32(bus, dst + 12, (uint32_t)start_y);
        dst += 16; /* 20 bytes per dest entry */
    }
}

/* ======================================================================
 * SWI 0x0F - ObjAffineSet
 * R0 = source data ptr, R1 = dest data ptr, R2 = count, R3 = stride
 *
 * Per src entry (8 bytes): scaleX(16), scaleY(16), angle(16), padding(16)
 * Writes PA/PB/PC/PD as 16-bit at dst, dst+stride, dst+2*stride, dst+3*stride
 * Then dst advances by 4*stride for the next entry.
 * ====================================================================== */
static void swi_obj_affine_set(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];
    uint32_t count = cpu->regs[2];
    uint32_t stride = cpu->regs[3];

    Bus* bus = cpu->bus;

    for (uint32_t i = 0; i < count; i++) {
        int16_t scale_x = (int16_t)bus_read16(bus, src);
        int16_t scale_y = (int16_t)bus_read16(bus, src + 2);
        uint16_t angle = bus_read16(bus, src + 4);
        src += 8;

        int32_t sin_val = (int32_t)bios_sin(angle);
        int32_t cos_val = (int32_t)bios_cos(angle);

        int16_t pa = (int16_t)(((int32_t)scale_x * cos_val) >> 14);
        int16_t pb = (int16_t)(((int32_t)scale_x * (-sin_val)) >> 14);
        int16_t pc = (int16_t)(((int32_t)scale_y * sin_val) >> 14);
        int16_t pd = (int16_t)(((int32_t)scale_y * cos_val) >> 14);

        bus_write16(bus, dst, (uint16_t)pa);
        bus_write16(bus, dst + stride, (uint16_t)pb);
        bus_write16(bus, dst + stride * 2, (uint16_t)pc);
        bus_write16(bus, dst + stride * 3, (uint16_t)pd);

        dst += stride * 4;
    }
}

/* ======================================================================
 * SWI 0x10 - BitUnPack
 * R0 = source, R1 = dest, R2 = info ptr
 *
 * Info struct at R2:
 *   srcLen  (16-bit): length of source data in bytes
 *   srcWidth (8-bit): source bit width (1, 2, 4, 8)
 *   destWidth (8-bit): destination bit width (1, 2, 4, 8, 16, 32)
 *   offset  (32-bit): value to add; bit31 = add-to-zero flag
 * ====================================================================== */
static void swi_bit_unpack(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];
    uint32_t info_ptr = cpu->regs[2];

    Bus* bus = cpu->bus;

    uint16_t src_len = bus_read16(bus, info_ptr);
    uint8_t src_width = bus_read8(bus, info_ptr + 2);
    uint8_t dest_width = bus_read8(bus, info_ptr + 3);
    uint32_t offset_raw = bus_read32(bus, info_ptr + 4);

    bool add_to_zero = BIT(offset_raw, 31);
    uint32_t offset = offset_raw & 0x7FFFFFFF;

    if (src_width == 0 || dest_width == 0) {
        LOG_WARN("SWI BitUnPack: invalid width src=%u dest=%u",
                 src_width, dest_width);
        return;
    }

    uint32_t src_mask = (1u << src_width) - 1;
    uint32_t out_word = 0;
    uint32_t out_bits = 0;

    for (uint32_t byte_idx = 0; byte_idx < src_len; byte_idx++) {
        uint8_t src_byte = bus_read8(bus, src + byte_idx);

        for (uint8_t bit_pos = 0; bit_pos < 8; bit_pos += src_width) {
            uint32_t val = (src_byte >> bit_pos) & src_mask;

            if (val != 0 || add_to_zero) {
                val += offset;
            }

            /* Mask to dest_width bits */
            val &= (dest_width < 32) ? ((1u << dest_width) - 1) : 0xFFFFFFFF;

            out_word |= val << out_bits;
            out_bits += dest_width;

            if (out_bits >= 32) {
                bus_write32(bus, dst, out_word);
                dst += 4;
                out_word = 0;
                out_bits = 0;
            }
        }
    }

    /* Flush remaining bits */
    if (out_bits > 0) {
        bus_write32(bus, dst, out_word);
    }
}

/* ======================================================================
 * SWI 0x11 - LZ77UnCompVram (16-bit VRAM variant)
 * R0 = source, R1 = dest
 *
 * Header at src: type[7:4]=1 (LZ77), decompressed_size[31:8]
 * Then: flag bytes + compressed data, 8 blocks per flag byte (MSB first)
 * For VRAM variant: buffer bytes and write 16-bit at a time
 * ====================================================================== */
static void swi_lz77_uncomp_vram(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];

    Bus* bus = cpu->bus;

    uint32_t header = bus_read32(bus, src);
    uint32_t decomp_size = header >> 8;
    src += 4;

    uint32_t bytes_written = 0;
    uint16_t out_halfword = 0;
    bool have_low_byte = false;

    while (bytes_written < decomp_size) {
        uint8_t flags = bus_read8(bus, src);
        src++;

        for (int block = 7; block >= 0 && bytes_written < decomp_size;
             block--) {
            if (BIT(flags, block)) {
                /* Compressed block: 2-byte reference */
                uint8_t byte1 = bus_read8(bus, src);
                uint8_t byte2 = bus_read8(bus, src + 1);
                src += 2;

                uint32_t length = ((uint32_t)(byte1 >> 4)) + 3;
                uint32_t disp = (((uint32_t)(byte1 & 0x0F)) << 8)
                              | (uint32_t)byte2;
                disp += 1;

                for (uint32_t j = 0; j < length && bytes_written < decomp_size;
                     j++) {
                    /* Read from already-decompressed output */
                    uint32_t read_addr = dst + bytes_written - disp;

                    /* For VRAM, we need to read back what we wrote.
                     * Since we write 16-bit at a time, we read from bus. */
                    uint8_t val;
                    if (have_low_byte &&
                        read_addr == dst + bytes_written - 1) {
                        /* The byte we need is still in the buffer */
                        val = (uint8_t)(out_halfword & 0xFF);
                    } else {
                        val = bus_read8(bus, read_addr);
                    }

                    if (!have_low_byte) {
                        out_halfword = val;
                        have_low_byte = true;
                    } else {
                        out_halfword |= (uint16_t)val << 8;
                        bus_write16(bus, dst + bytes_written - 1, out_halfword);
                        have_low_byte = false;
                    }

                    bytes_written++;
                }
            } else {
                /* Literal byte */
                uint8_t val = bus_read8(bus, src);
                src++;

                if (!have_low_byte) {
                    out_halfword = val;
                    have_low_byte = true;
                } else {
                    out_halfword |= (uint16_t)val << 8;
                    bus_write16(bus, dst + bytes_written - 1, out_halfword);
                    have_low_byte = false;
                }

                bytes_written++;
            }
        }
    }

    /* Flush any remaining byte (pad with 0) */
    if (have_low_byte) {
        bus_write16(bus, dst + bytes_written - 1, out_halfword);
    }
}

/* ======================================================================
 * SWI 0x12 - HuffUnComp
 * R0 = source, R1 = dest
 *
 * Header: type[7:4]=2 (Huffman), bitWidth[3:0], decompSize[31:8]
 * Followed by tree data, then compressed bitstream.
 * ====================================================================== */
static void swi_huff_uncomp(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];

    Bus* bus = cpu->bus;

    uint32_t header = bus_read32(bus, src);
    uint8_t bit_width = header & 0x0F;
    uint32_t decomp_size = header >> 8;
    src += 4;

    if (bit_width == 0) {
        bit_width = 8;
    }

    /* Tree size is in the first byte after header (number of tree nodes / 2 - 1) */
    uint8_t tree_size_byte = bus_read8(bus, src);
    uint32_t tree_offset = src + 1;
    uint32_t data_offset = src + ((uint32_t)tree_size_byte + 1) * 2;

    uint32_t bytes_written = 0;
    uint32_t out_word = 0;
    uint32_t out_bits = 0;

    /* Current tree position */
    uint32_t bit_pos = 0;
    uint32_t current_data = bus_read32(bus, data_offset);
    uint32_t data_addr = data_offset + 4;

    while (bytes_written < decomp_size) {
        /* Start from root (offset 0 in tree) */
        uint32_t node_addr = tree_offset;
        uint8_t node = bus_read8(bus, node_addr);

        while (true) {
            /* Read one bit from the bitstream (LSB first within each 32-bit word) */
            uint32_t bit = (current_data >> bit_pos) & 1;
            bit_pos++;

            if (bit_pos >= 32) {
                current_data = bus_read32(bus, data_addr);
                data_addr += 4;
                bit_pos = 0;
            }

            /* Navigate tree. The node byte encodes:
             * bits [5:0] = offset to child node / 2
             * bit 6 = right child is leaf
             * bit 7 = left child is leaf
             *
             * The child offset is: ((node & 0x3F) + 1) * 2 relative to
             * the byte AFTER the current node pair. Since nodes are stored
             * in pairs (2 bytes each), the absolute offset is:
             * node_addr - (node_addr & 1) + ((node & 0x3F) + 1) * 2
             */
            uint32_t child_base = (node_addr & ~1u) + ((uint32_t)(node & 0x3F) + 1) * 2;
            bool is_leaf;

            if (bit == 0) {
                /* Left child */
                node_addr = child_base;
                is_leaf = (node >> 7) & 1;
            } else {
                /* Right child */
                node_addr = child_base + 1;
                is_leaf = (node >> 6) & 1;
            }

            if (is_leaf) {
                uint8_t leaf_val = bus_read8(bus, node_addr);

                out_word |= (uint32_t)leaf_val << out_bits;
                out_bits += bit_width;

                if (out_bits >= 32) {
                    bus_write32(bus, dst + bytes_written, out_word);
                    bytes_written += 4;
                    out_word = 0;
                    out_bits = 0;
                }
                break;
            } else {
                node = bus_read8(bus, node_addr);
            }
        }
    }
}

/* ======================================================================
 * SWI 0x13 - RLUnCompVram (16-bit VRAM variant)
 * R0 = source, R1 = dest
 *
 * Header: type[7:4]=3 (RLE), decompressed_size[31:8]
 * Flag byte: bit7 set = compressed run, bit7 clear = literal run
 * ====================================================================== */
static void swi_rl_uncomp_vram(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];

    Bus* bus = cpu->bus;

    uint32_t header = bus_read32(bus, src);
    uint32_t decomp_size = header >> 8;
    src += 4;

    uint32_t bytes_written = 0;
    uint16_t out_halfword = 0;
    bool have_low_byte = false;

    while (bytes_written < decomp_size) {
        uint8_t flag = bus_read8(bus, src);
        src++;

        if (flag & 0x80) {
            /* Compressed run: (flag & 0x7F) + 3 copies of next byte */
            uint32_t run_len = (flag & 0x7Fu) + 3;
            uint8_t val = bus_read8(bus, src);
            src++;

            for (uint32_t j = 0; j < run_len && bytes_written < decomp_size;
                 j++) {
                if (!have_low_byte) {
                    out_halfword = val;
                    have_low_byte = true;
                } else {
                    out_halfword |= (uint16_t)val << 8;
                    bus_write16(bus, dst + bytes_written - 1, out_halfword);
                    have_low_byte = false;
                }
                bytes_written++;
            }
        } else {
            /* Literal run: (flag & 0x7F) + 1 bytes */
            uint32_t run_len = (flag & 0x7Fu) + 1;

            for (uint32_t j = 0; j < run_len && bytes_written < decomp_size;
                 j++) {
                uint8_t val = bus_read8(bus, src);
                src++;

                if (!have_low_byte) {
                    out_halfword = val;
                    have_low_byte = true;
                } else {
                    out_halfword |= (uint16_t)val << 8;
                    bus_write16(bus, dst + bytes_written - 1, out_halfword);
                    have_low_byte = false;
                }
                bytes_written++;
            }
        }
    }

    /* Flush remaining byte */
    if (have_low_byte) {
        bus_write16(bus, dst + bytes_written - 1, out_halfword);
    }
}

/* ======================================================================
 * SWI 0x14 - Diff8bitUnFilterWram
 * R0 = source, R1 = dest
 *
 * Header: type[7:4]=8 (Diff), decompressed_size[31:8]
 * Writes 8-bit at a time (WRAM variant).
 * ====================================================================== */
static void swi_diff8bit_unfilter_wram(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];

    Bus* bus = cpu->bus;

    uint32_t header = bus_read32(bus, src);
    uint32_t decomp_size = header >> 8;
    src += 4;

    if (decomp_size == 0) {
        return;
    }

    uint8_t prev = bus_read8(bus, src);
    src++;
    bus_write8(bus, dst, prev);
    dst++;

    for (uint32_t i = 1; i < decomp_size; i++) {
        uint8_t diff = bus_read8(bus, src);
        src++;
        prev = (uint8_t)(prev + diff);
        bus_write8(bus, dst, prev);
        dst++;
    }
}

/* ======================================================================
 * SWI 0x15 - Diff8bitUnFilterVram
 * Same as 0x14 but writes 16-bit at a time (VRAM variant).
 * ====================================================================== */
static void swi_diff8bit_unfilter_vram(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];

    Bus* bus = cpu->bus;

    uint32_t header = bus_read32(bus, src);
    uint32_t decomp_size = header >> 8;
    src += 4;

    if (decomp_size == 0) {
        return;
    }

    uint8_t prev = bus_read8(bus, src);
    src++;

    uint16_t out_halfword = prev;
    bool have_low_byte = true;

    for (uint32_t i = 1; i < decomp_size; i++) {
        uint8_t diff = bus_read8(bus, src);
        src++;
        prev = (uint8_t)(prev + diff);

        if (!have_low_byte) {
            out_halfword = prev;
            have_low_byte = true;
        } else {
            out_halfword |= (uint16_t)prev << 8;
            bus_write16(bus, dst, out_halfword);
            dst += 2;
            have_low_byte = false;
        }
    }

    /* Flush remaining byte */
    if (have_low_byte) {
        bus_write16(bus, dst, out_halfword);
    }
}

/* ======================================================================
 * SWI 0x16 - Diff16bitUnFilter
 * R0 = source, R1 = dest
 *
 * Header: type[7:4]=8 (Diff), decompressed_size[31:8]
 * Operates on 16-bit values. Always writes 16-bit.
 * ====================================================================== */
static void swi_diff16bit_unfilter(ARM7TDMI* cpu) {
    uint32_t src = cpu->regs[0];
    uint32_t dst = cpu->regs[1];

    Bus* bus = cpu->bus;

    uint32_t header = bus_read32(bus, src);
    uint32_t decomp_size = header >> 8;
    src += 4;

    if (decomp_size < 2) {
        return;
    }

    uint16_t prev = bus_read16(bus, src);
    src += 2;
    bus_write16(bus, dst, prev);
    dst += 2;

    /* Iterate in 16-bit units */
    uint32_t half_count = decomp_size / 2;
    for (uint32_t i = 1; i < half_count; i++) {
        uint16_t diff = bus_read16(bus, src);
        src += 2;
        prev = (uint16_t)(prev + diff);
        bus_write16(bus, dst, prev);
        dst += 2;
    }
}

/* ======================================================================
 * SWI 0x19 — SoundBias
 * ====================================================================== */
static void swi_sound_bias(ARM7TDMI* cpu) {
    /* R0: 0 = ramp bias down to 0x000, nonzero = ramp up to 0x200.
     * HLE: apply instantly (ramp delay is inaudible in emulation).
     * Preserve bits 14-15 (amplitude resolution setting). */
    Bus* bus = cpu->bus;
    if (!bus || !bus->apu) return;

    uint16_t target = cpu->regs[0] ? 0x200 : 0x000;
    uint16_t upper = bus->apu->soundbias & 0xC000;
    bus->apu->soundbias = upper | target;

    /* Sync io_regs backing store */
    bus->io_regs[0x88] = (uint8_t)(bus->apu->soundbias);
    bus->io_regs[0x89] = (uint8_t)(bus->apu->soundbias >> 8);
}

/* ======================================================================
 * SWI 0x1F — MidiKey2Freq
 * ====================================================================== */
static void swi_midi_key2freq(ARM7TDMI* cpu) {
    /* R0 = pointer to WaveData struct, R1 = MIDI key, R2 = fine pitch
     * WaveData.freq is at offset +4 (uint32_t).
     * Formula: result = WaveData.freq / 2^((180 - mk - fp/256) / 12)
     * Per GBATEK and mGBA reference implementation. */
    uint32_t wave_freq = bus_read32(cpu->bus, cpu->regs[0] + 4);
    uint32_t mk = cpu->regs[1];
    uint32_t fp = cpu->regs[2];

    float exponent = (180.0f - (float)mk - (float)fp / 256.0f) / 12.0f;
    float divisor = exp2f(exponent);

    float result = (float)wave_freq / divisor;
    if (result > (float)UINT32_MAX) result = (float)UINT32_MAX;
    if (result < 0.0f) result = 0.0f;
    cpu->regs[0] = (uint32_t)result;
}

/* ======================================================================
 * Main SWI dispatch
 * ====================================================================== */
void bios_hle_execute(ARM7TDMI* cpu, uint32_t swi_num) {
    switch (swi_num) {
    case 0x00: /* SoftReset — not fully implemented, just reset PC */
        LOG_WARN("SWI 0x00 SoftReset: minimal stub");
        cpu->regs[REG_PC] = 0x08000000;
        cpu_flush_pipeline(cpu);
        break;

    case 0x01: { /* RegisterRamReset */
        uint32_t flags = cpu->regs[0];
        Bus* bus = cpu->bus;

        /* Bit 0: Clear 256KB EWRAM */
        if (flags & 0x01) {
            memset(bus->ewram, 0, EWRAM_SIZE);
        }

        /* Bit 1: Clear 32KB IWRAM except top 512 bytes (stack area) */
        if (flags & 0x02) {
            memset(bus->iwram, 0, 0x7E00);
        }

        /* Bit 2: Clear Palette RAM */
        if (flags & 0x04) {
            memset(bus->palette_ram, 0, PALETTE_SIZE);
        }

        /* Bit 3: Clear VRAM */
        if (flags & 0x08) {
            memset(bus->vram, 0, VRAM_SIZE);
        }

        /* Bit 4: Clear OAM */
        if (flags & 0x10) {
            memset(bus->oam, 0, OAM_SIZE);
        }

        /* Bit 5: Reset SIO registers (0x04000120-0x0400012F) */
        if (flags & 0x20) {
            for (uint32_t off = 0x120; off < 0x130; off += 2) {
                bus->io_regs[off] = 0;
                bus->io_regs[off + 1] = 0;
            }
        }

        /* Bit 6: Reset Sound registers (0x04000060-0x040000AF) */
        if (flags & 0x40) {
            memset(&bus->io_regs[0x60], 0, 0xB0 - 0x60);
        }

        /* Bit 7: Reset all other I/O registers */
        if (flags & 0x80) {
            /* Clear 0x00-0x5F (display, BG, window, blend) */
            memset(&bus->io_regs[0x00], 0, 0x60);
            /* Set DISPCNT to forced blank */
            bus->io_regs[0x00] = 0x80;
            bus->io_regs[0x01] = 0x00;
            /* Clear DMA registers (0xB0-0xDF) */
            memset(&bus->io_regs[0xB0], 0, 0xE0 - 0xB0);
            /* Clear Timer registers (0x100-0x10F) */
            memset(&bus->io_regs[0x100], 0, 0x10);
            /* Clear Keypad register area (0x130-0x133) */
            memset(&bus->io_regs[0x130], 0, 4);
            /* Clear Interrupt registers (0x200-0x20B) */
            memset(&bus->io_regs[0x200], 0, 0x0C);
        }
        break;
    }

    case 0x02: /* Halt */
        swi_halt(cpu);
        break;

    case 0x03: /* Stop — deep sleep, treat as halt */
        LOG_WARN("SWI 0x03 Stop: treating as Halt");
        swi_halt(cpu);
        break;

    case 0x04: /* IntrWait */
        swi_intr_wait(cpu);
        break;

    case 0x05: /* VBlankIntrWait */
        swi_vblank_intr_wait(cpu);
        break;

    case 0x06: /* Div */
        swi_div(cpu);
        break;

    case 0x07: /* DivArm */
        swi_div_arm(cpu);
        break;

    case 0x08: /* Sqrt */
        swi_sqrt(cpu);
        break;

    case 0x09: /* ArcTan */
        swi_arctan(cpu);
        break;

    case 0x0A: /* ArcTan2 */
        swi_arctan2(cpu);
        break;

    case 0x0B: /* CpuSet */
        swi_cpu_set(cpu);
        break;

    case 0x0C: /* CpuFastSet */
        swi_cpu_fast_set(cpu);
        break;

    case 0x0D: /* GetBiosChecksum */
        swi_get_bios_checksum(cpu);
        break;

    case 0x0E: /* BgAffineSet */
        swi_bg_affine_set(cpu);
        break;

    case 0x0F: /* ObjAffineSet */
        swi_obj_affine_set(cpu);
        break;

    case 0x10: /* BitUnPack */
        swi_bit_unpack(cpu);
        break;

    case 0x11: /* LZ77UnCompWram (8-bit writes) */
        swi_lz77_uncomp_vram(cpu);  /* 8-bit writes work via 16-bit path */
        break;

    case 0x12: /* LZ77UnCompVram (16-bit writes) */
        swi_lz77_uncomp_vram(cpu);
        break;

    case 0x13: /* HuffUnComp */
        swi_huff_uncomp(cpu);
        break;

    case 0x14: /* RLUnCompWram (8-bit writes) */
        swi_rl_uncomp_vram(cpu);  /* 8-bit writes work via 16-bit path */
        break;

    case 0x15: /* RLUnCompVram (16-bit writes) */
        swi_rl_uncomp_vram(cpu);
        break;

    case 0x16: /* Diff8bitUnFilterWram */
        swi_diff8bit_unfilter_wram(cpu);
        break;

    case 0x17: /* Diff8bitUnFilterVram */
        swi_diff8bit_unfilter_vram(cpu);
        break;

    case 0x18: /* Diff16bitUnFilter */
        swi_diff16bit_unfilter(cpu);
        break;

    case 0x19: /* SoundBias */
        swi_sound_bias(cpu);
        break;

    case 0x1A: /* SoundDriverInit — stub for HLE */
    case 0x1B: /* SoundDriverMode — stub for HLE */
    case 0x1C: /* SoundDriverMain — stub for HLE */
    case 0x1D: /* SoundDriverVSync — stub for HLE */
    case 0x1E: /* SoundChannelClear — stub for HLE */
        break;

    case 0x1F: /* MidiKey2Freq */
        swi_midi_key2freq(cpu);
        break;

    case 0x28: /* SoundDriverVSyncOff — stub for HLE */
    case 0x29: /* SoundDriverVSyncOn — stub for HLE */
        break;

    default:
        LOG_WARN("Unimplemented SWI: 0x%02X at PC=0x%08X",
                 swi_num, cpu->regs[REG_PC]);
        break;
    }
}
