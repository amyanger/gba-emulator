#include "thumb_instr.h"
#include "bios_hle.h"
#include "memory/bus.h"

/*
 * Thumb (16-bit) instruction decoder and executor for the ARM7TDMI.
 *
 * All 19 Thumb instruction formats are implemented, decoded in strict
 * priority order (most specific bit patterns first).
 *
 * References: GBATEK "THUMB Opcodes", ARM7TDMI Technical Reference Manual.
 */

/* ========================================================================
 * Flag Helpers (duplicated from arm_instr.c — those are static)
 * ======================================================================== */

/* Set N and Z flags from a 32-bit result */
static void set_nz_flags(ARM7TDMI* cpu, uint32_t result) {
    if (BIT(result, 31)) {
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_N);
    } else {
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_N);
    }
    if (result == 0) {
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_Z);
    } else {
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_Z);
    }
}

/* Set carry flag */
static void set_c_flag(ARM7TDMI* cpu, bool carry) {
    if (carry) {
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_C);
    } else {
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_C);
    }
}

/* Set overflow flag */
static void set_v_flag(ARM7TDMI* cpu, bool overflow) {
    if (overflow) {
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_V);
    } else {
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_V);
    }
}

/* Detect addition overflow: (a ^ result) & (b ^ result) bit31 */
static bool add_overflow(uint32_t a, uint32_t b, uint32_t result) {
    return BIT((a ^ result) & (b ^ result), 31);
}

/* Detect subtraction overflow: (a ^ b) & (a ^ result) bit31 */
static bool sub_overflow(uint32_t a, uint32_t b, uint32_t result) {
    return BIT((a ^ b) & (a ^ result), 31);
}

/* ========================================================================
 * Barrel Shifter (duplicated from arm_instr.c — those are static)
 * ======================================================================== */

/*
 * Barrel shift for register-specified shift amounts (used by Format 4 ALU).
 * When amount == 0, value passes through unchanged and carry is preserved.
 */
static uint32_t barrel_shift_reg(uint32_t value, uint8_t shift_type,
                                 uint8_t amount, bool* carry_out) {
    if (amount == 0) {
        return value;
    }

    switch (shift_type) {
    case 0: /* LSL */
        if (amount < 32) {
            *carry_out = BIT(value, 32 - amount);
            return value << amount;
        } else if (amount == 32) {
            *carry_out = BIT(value, 0);
            return 0;
        } else {
            *carry_out = false;
            return 0;
        }

    case 1: /* LSR */
        if (amount < 32) {
            *carry_out = BIT(value, amount - 1);
            return value >> amount;
        } else if (amount == 32) {
            *carry_out = BIT(value, 31);
            return 0;
        } else {
            *carry_out = false;
            return 0;
        }

    case 2: /* ASR */
        if (amount < 32) {
            *carry_out = BIT(value, amount - 1);
            return (uint32_t)((int32_t)value >> amount);
        } else {
            *carry_out = BIT(value, 31);
            return BIT(value, 31) ? 0xFFFFFFFF : 0;
        }

    case 3: /* ROR */
        amount &= 31;
        if (amount == 0) {
            /* ROR by 32 (or multiple of 32) */
            *carry_out = BIT(value, 31);
            return value;
        }
        *carry_out = BIT(value, amount - 1);
        return (value >> amount) | (value << (32 - amount));

    default:
        return value;
    }
}

/* ========================================================================
 * Format 1: Move Shifted Register (bits[15:13]=000, bits[12:11]!=11)
 * LSL/LSR/ASR Rd, Rs, #offset5
 * ======================================================================== */
static int thumb_move_shifted(ARM7TDMI* cpu, uint16_t instr) {
    uint8_t op = BITS(instr, 12, 11);
    uint8_t offset5 = BITS(instr, 10, 6);
    uint8_t rs = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    uint32_t value = cpu->regs[rs];
    bool carry = BIT(cpu->cpsr, CPSR_C);

    switch (op) {
    case 0: /* LSL */
        if (offset5 == 0) {
            /* LSL #0: value unchanged, carry preserved */
        } else {
            carry = BIT(value, 32 - offset5);
            value = value << offset5;
        }
        break;

    case 1: /* LSR */
        if (offset5 == 0) {
            /* LSR #0 encodes LSR #32 */
            carry = BIT(value, 31);
            value = 0;
        } else {
            carry = BIT(value, offset5 - 1);
            value = value >> offset5;
        }
        break;

    case 2: /* ASR */
        if (offset5 == 0) {
            /* ASR #0 encodes ASR #32 */
            carry = BIT(value, 31);
            value = BIT(value, 31) ? 0xFFFFFFFF : 0;
        } else {
            carry = BIT(value, offset5 - 1);
            value = (uint32_t)((int32_t)value >> offset5);
        }
        break;

    default:
        break;
    }

    cpu->regs[rd] = value;
    set_nz_flags(cpu, value);
    set_c_flag(cpu, carry);

    return 1; /* 1S */
}

/* ========================================================================
 * Format 2: Add/Subtract (bits[15:11]=00011)
 * ADD/SUB Rd, Rs, Rn/imm3
 * ======================================================================== */
static int thumb_add_subtract(ARM7TDMI* cpu, uint16_t instr) {
    bool is_imm = BIT(instr, 10);
    bool is_sub = BIT(instr, 9);
    uint8_t rn_or_imm3 = BITS(instr, 8, 6);
    uint8_t rs = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    uint32_t operand1 = cpu->regs[rs];
    uint32_t operand2 = is_imm ? (uint32_t)rn_or_imm3 : cpu->regs[rn_or_imm3];

    uint32_t result;
    if (is_sub) {
        /* SUB */
        uint64_t res64 = (uint64_t)operand1 + (uint64_t)~operand2 + 1ULL;
        result = (uint32_t)res64;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(operand1, operand2, result));
    } else {
        /* ADD */
        uint64_t res64 = (uint64_t)operand1 + (uint64_t)operand2;
        result = (uint32_t)res64;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, add_overflow(operand1, operand2, result));
    }

    cpu->regs[rd] = result;
    return 1; /* 1S */
}

/* ========================================================================
 * Format 3: Move/Compare/Add/Subtract Immediate (bits[15:13]=001)
 * MOV/CMP/ADD/SUB Rd, #imm8
 * ======================================================================== */
static int thumb_mov_cmp_add_sub_imm(ARM7TDMI* cpu, uint16_t instr) {
    uint8_t op = BITS(instr, 12, 11);
    uint8_t rd = BITS(instr, 10, 8);
    uint32_t imm8 = instr & 0xFF;

    uint32_t rd_val = cpu->regs[rd];

    switch (op) {
    case 0: /* MOV */
        cpu->regs[rd] = imm8;
        set_nz_flags(cpu, imm8);
        /* C and V unchanged */
        break;

    case 1: { /* CMP: Rd - imm8, flags only */
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)~imm8 + 1ULL;
        uint32_t result = (uint32_t)res64;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(rd_val, imm8, result));
        break;
    }

    case 2: { /* ADD */
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)imm8;
        uint32_t result = (uint32_t)res64;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, add_overflow(rd_val, imm8, result));
        break;
    }

    case 3: { /* SUB */
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)~imm8 + 1ULL;
        uint32_t result = (uint32_t)res64;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(rd_val, imm8, result));
        break;
    }
    }

    return 1; /* 1S */
}

/* ========================================================================
 * Format 4: ALU Operations (bits[15:10]=010000)
 * 16 ALU opcodes operating on low registers
 * ======================================================================== */
static int thumb_alu(ARM7TDMI* cpu, uint16_t instr) {
    uint8_t op = BITS(instr, 9, 6);
    uint8_t rs = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    uint32_t rd_val = cpu->regs[rd];
    uint32_t rs_val = cpu->regs[rs];
    uint32_t result;
    int cycles = 1; /* 1S default */

    switch (op) {
    case 0x0: /* AND */
        result = rd_val & rs_val;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        break;

    case 0x1: /* EOR */
        result = rd_val ^ rs_val;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        break;

    case 0x2: { /* LSL: Rd = Rd << (Rs & 0xFF) */
        uint8_t amount = (uint8_t)(rs_val & 0xFF);
        bool carry = BIT(cpu->cpsr, CPSR_C);
        result = barrel_shift_reg(rd_val, 0, amount, &carry);
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, carry);
        break;
    }

    case 0x3: { /* LSR: Rd = Rd >> (Rs & 0xFF) */
        uint8_t amount = (uint8_t)(rs_val & 0xFF);
        bool carry = BIT(cpu->cpsr, CPSR_C);
        result = barrel_shift_reg(rd_val, 1, amount, &carry);
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, carry);
        break;
    }

    case 0x4: { /* ASR: Rd = Rd ASR (Rs & 0xFF) */
        uint8_t amount = (uint8_t)(rs_val & 0xFF);
        bool carry = BIT(cpu->cpsr, CPSR_C);
        result = barrel_shift_reg(rd_val, 2, amount, &carry);
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, carry);
        break;
    }

    case 0x5: { /* ADC: Rd = Rd + Rs + C */
        uint32_t old_c = BIT(cpu->cpsr, CPSR_C);
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)rs_val +
                         (uint64_t)old_c;
        result = (uint32_t)res64;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, add_overflow(rd_val, rs_val, result));
        break;
    }

    case 0x6: { /* SBC: Rd = Rd - Rs - !C = Rd + ~Rs + C */
        uint32_t old_c = BIT(cpu->cpsr, CPSR_C);
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)~rs_val +
                         (uint64_t)old_c;
        result = (uint32_t)res64;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(rd_val, rs_val, result));
        break;
    }

    case 0x7: { /* ROR: Rd = Rd ROR (Rs & 0xFF) */
        uint8_t amount = (uint8_t)(rs_val & 0xFF);
        bool carry = BIT(cpu->cpsr, CPSR_C);
        result = barrel_shift_reg(rd_val, 3, amount, &carry);
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, carry);
        break;
    }

    case 0x8: /* TST: Rd & Rs, flags only */
        result = rd_val & rs_val;
        set_nz_flags(cpu, result);
        break;

    case 0x9: { /* NEG: Rd = 0 - Rs */
        uint64_t res64 = (uint64_t)~rs_val + 1ULL;
        result = (uint32_t)res64;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(0, rs_val, result));
        break;
    }

    case 0xA: { /* CMP: Rd - Rs, flags only */
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)~rs_val + 1ULL;
        result = (uint32_t)res64;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(rd_val, rs_val, result));
        break;
    }

    case 0xB: { /* CMN: Rd + Rs, flags only */
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)rs_val;
        result = (uint32_t)res64;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, add_overflow(rd_val, rs_val, result));
        break;
    }

    case 0xC: /* ORR: Rd = Rd | Rs */
        result = rd_val | rs_val;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        break;

    case 0xD: { /* MUL: Rd = Rd * Rs */
        result = rd_val * rs_val;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        /* C flag is destroyed on ARMv4 (set to meaningless value).
         * Use 0 as a safe default per common emulator behavior. */
        set_c_flag(cpu, false);

        /* MUL internal cycles: approximate with 4 cycles total.
         * Exact timing depends on leading zeros/ones of the multiplier
         * in groups of 8 bits, but this is non-critical for Pokemon
         * Emerald. */
        cycles = 4;
        break;
    }

    case 0xE: /* BIC: Rd = Rd & ~Rs */
        result = rd_val & ~rs_val;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        break;

    case 0xF: /* MVN: Rd = ~Rs */
        result = ~rs_val;
        cpu->regs[rd] = result;
        set_nz_flags(cpu, result);
        break;
    }

    return cycles;
}

/* ========================================================================
 * Format 5: Hi Register Operations / Branch Exchange
 * (bits[15:10]=010001)
 * ======================================================================== */
static int thumb_hi_reg_bx(ARM7TDMI* cpu, uint16_t instr) {
    uint8_t op = BITS(instr, 9, 8);
    bool h1 = BIT(instr, 7);
    bool h2 = BIT(instr, 6);
    uint8_t rd = BITS(instr, 2, 0) | ((uint8_t)h1 << 3);
    uint8_t rs = BITS(instr, 5, 3) | ((uint8_t)h2 << 3);

    uint32_t rs_val = cpu->regs[rs];

    switch (op) {
    case 0: /* ADD: Rd = Rd + Rs (no flag change) */
        cpu->regs[rd] += rs_val;
        if (rd == REG_PC) {
            cpu->regs[REG_PC] &= ~1u;
            cpu_flush_pipeline(cpu);
            return 3; /* 2S+1N */
        }
        return 1; /* 1S */

    case 1: { /* CMP: Rd - Rs, sets all flags */
        uint32_t rd_val = cpu->regs[rd];
        uint64_t res64 = (uint64_t)rd_val + (uint64_t)~rs_val + 1ULL;
        uint32_t result = (uint32_t)res64;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(rd_val, rs_val, result));
        return 1; /* 1S */
    }

    case 2: /* MOV: Rd = Rs (no flag change) */
        cpu->regs[rd] = rs_val;
        if (rd == REG_PC) {
            cpu->regs[REG_PC] &= ~1u;
            cpu_flush_pipeline(cpu);
            return 3; /* 2S+1N */
        }
        return 1; /* 1S */

    case 3: /* BX: Branch and optionally exchange instruction set */
        if (BIT(rs_val, 0)) {
            /* Stay in Thumb state */
            cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_T);
            cpu->regs[REG_PC] = rs_val & ~1u;
        } else {
            /* Switch to ARM state */
            cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_T);
            cpu->regs[REG_PC] = rs_val & ~3u;
        }
        cpu_flush_pipeline(cpu);
        return 3; /* 2S+1N */

    default:
        return 1;
    }
}

/* ========================================================================
 * Format 6: PC-Relative Load (bits[15:11]=01001)
 * LDR Rd, [PC, #imm8*4]
 * ======================================================================== */
static int thumb_pc_relative_load(ARM7TDMI* cpu, uint16_t instr) {
    uint8_t rd = BITS(instr, 10, 8);
    uint32_t imm8 = instr & 0xFF;

    /* PC is already 4 ahead of the executing instruction.
     * Force bit[1] to 0 (word-align). */
    uint32_t addr = (cpu->regs[REG_PC] & ~2u) + (imm8 << 2);

    cpu->regs[rd] = bus_read32(cpu->bus, addr);
    return 3; /* 1S+1N+1I */
}

/* ========================================================================
 * Format 7: Load/Store with Register Offset
 * (bits[15:12]=0101, bit[9]=0)
 * STR/STRB/LDR/LDRB Rd, [Rb, Ro]
 * ======================================================================== */
static int thumb_load_store_reg(ARM7TDMI* cpu, uint16_t instr) {
    bool load = BIT(instr, 11);
    bool byte = BIT(instr, 10);
    uint8_t ro = BITS(instr, 8, 6);
    uint8_t rb = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    uint32_t addr = cpu->regs[rb] + cpu->regs[ro];

    if (load) {
        if (byte) {
            cpu->regs[rd] = bus_read8(cpu->bus, addr);
        } else {
            /* Word load with rotation for unaligned addresses */
            uint32_t aligned = addr & ~3u;
            uint32_t word = bus_read32(cpu->bus, aligned);
            uint8_t rot = (uint8_t)((addr & 3u) * 8);
            if (rot != 0) {
                word = (word >> rot) | (word << (32 - rot));
            }
            cpu->regs[rd] = word;
        }
        return 3; /* 1S+1N+1I */
    } else {
        if (byte) {
            bus_write8(cpu->bus, addr, (uint8_t)cpu->regs[rd]);
        } else {
            bus_write32(cpu->bus, addr & ~3u, cpu->regs[rd]);
        }
        return 2; /* 2N */
    }
}

/* ========================================================================
 * Format 8: Load/Store Sign-Extended Byte/Halfword
 * (bits[15:12]=0101, bit[9]=1)
 * STRH/LDRH/LDSB/LDSH Rd, [Rb, Ro]
 * ======================================================================== */
static int thumb_load_store_sign_ext(ARM7TDMI* cpu, uint16_t instr) {
    bool h_bit = BIT(instr, 11);
    bool s_bit = BIT(instr, 10);
    uint8_t ro = BITS(instr, 8, 6);
    uint8_t rb = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    uint32_t addr = cpu->regs[rb] + cpu->regs[ro];

    if (!s_bit && !h_bit) {
        /* STRH: store halfword */
        bus_write16(cpu->bus, addr & ~1u, (uint16_t)cpu->regs[rd]);
        return 2; /* 2N */
    } else if (!s_bit && h_bit) {
        /* LDRH: unsigned halfword load */
        if (addr & 1u) {
            /* Misaligned: rotated read */
            uint32_t val = bus_read16(cpu->bus, addr & ~1u);
            cpu->regs[rd] = (val >> 8) | (val << 24);
        } else {
            cpu->regs[rd] = bus_read16(cpu->bus, addr);
        }
        return 3; /* 1S+1N+1I */
    } else if (s_bit && !h_bit) {
        /* LDSB: signed byte load */
        cpu->regs[rd] = (uint32_t)(int32_t)(int8_t)bus_read8(cpu->bus, addr);
        return 3; /* 1S+1N+1I */
    } else {
        /* LDSH: signed halfword load */
        if (addr & 1u) {
            /* Misaligned LDSH: loads byte, sign-extends to 32 */
            cpu->regs[rd] = (uint32_t)(int32_t)(int8_t)bus_read8(
                cpu->bus, addr);
        } else {
            cpu->regs[rd] = (uint32_t)(int32_t)(int16_t)bus_read16(
                cpu->bus, addr);
        }
        return 3; /* 1S+1N+1I */
    }
}

/* ========================================================================
 * Format 9: Load/Store with Immediate Offset (bits[15:13]=011)
 * LDR/STR/LDRB/STRB Rd, [Rb, #offset5]
 * ======================================================================== */
static int thumb_load_store_imm(ARM7TDMI* cpu, uint16_t instr) {
    bool byte = BIT(instr, 12);
    bool load = BIT(instr, 11);
    uint8_t offset5 = BITS(instr, 10, 6);
    uint8_t rb = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    /* B=0: word transfer, offset = offset5 * 4
     * B=1: byte transfer, offset = offset5 */
    uint32_t offset = byte ? (uint32_t)offset5 : (uint32_t)offset5 << 2;
    uint32_t addr = cpu->regs[rb] + offset;

    if (load) {
        if (byte) {
            cpu->regs[rd] = bus_read8(cpu->bus, addr);
        } else {
            /* Word load with rotation for unaligned addresses */
            uint32_t aligned = addr & ~3u;
            uint32_t word = bus_read32(cpu->bus, aligned);
            uint8_t rot = (uint8_t)((addr & 3u) * 8);
            if (rot != 0) {
                word = (word >> rot) | (word << (32 - rot));
            }
            cpu->regs[rd] = word;
        }
        return 3; /* 1S+1N+1I */
    } else {
        if (byte) {
            bus_write8(cpu->bus, addr, (uint8_t)cpu->regs[rd]);
        } else {
            bus_write32(cpu->bus, addr & ~3u, cpu->regs[rd]);
        }
        return 2; /* 2N */
    }
}

/* ========================================================================
 * Format 10: Load/Store Halfword (bits[15:12]=1000)
 * LDRH/STRH Rd, [Rb, #offset5*2]
 * ======================================================================== */
static int thumb_load_store_halfword(ARM7TDMI* cpu, uint16_t instr) {
    bool load = BIT(instr, 11);
    uint8_t offset5 = BITS(instr, 10, 6);
    uint8_t rb = BITS(instr, 5, 3);
    uint8_t rd = BITS(instr, 2, 0);

    uint32_t addr = cpu->regs[rb] + ((uint32_t)offset5 << 1);

    if (load) {
        /* LDRH */
        if (addr & 1u) {
            /* Misaligned: rotated read */
            uint32_t val = bus_read16(cpu->bus, addr & ~1u);
            cpu->regs[rd] = (val >> 8) | (val << 24);
        } else {
            cpu->regs[rd] = bus_read16(cpu->bus, addr);
        }
        return 3; /* 1S+1N+1I */
    } else {
        /* STRH */
        bus_write16(cpu->bus, addr & ~1u, (uint16_t)cpu->regs[rd]);
        return 2; /* 2N */
    }
}

/* ========================================================================
 * Format 11: SP-Relative Load/Store (bits[15:12]=1001)
 * LDR/STR Rd, [SP, #imm8*4]
 * ======================================================================== */
static int thumb_sp_relative_load_store(ARM7TDMI* cpu, uint16_t instr) {
    bool load = BIT(instr, 11);
    uint8_t rd = BITS(instr, 10, 8);
    uint32_t imm8 = instr & 0xFF;

    uint32_t addr = cpu->regs[REG_SP] + (imm8 << 2);

    if (load) {
        /* Word load with rotation for unaligned addresses */
        uint32_t aligned = addr & ~3u;
        uint32_t word = bus_read32(cpu->bus, aligned);
        uint8_t rot = (uint8_t)((addr & 3u) * 8);
        if (rot != 0) {
            word = (word >> rot) | (word << (32 - rot));
        }
        cpu->regs[rd] = word;
        return 3; /* 1S+1N+1I */
    } else {
        bus_write32(cpu->bus, addr & ~3u, cpu->regs[rd]);
        return 2; /* 2N */
    }
}

/* ========================================================================
 * Format 12: Load Address (bits[15:12]=1010)
 * ADD Rd, PC/SP, #imm8*4
 * ======================================================================== */
static int thumb_load_address(ARM7TDMI* cpu, uint16_t instr) {
    bool sp = BIT(instr, 11);
    uint8_t rd = BITS(instr, 10, 8);
    uint32_t imm8 = instr & 0xFF;

    if (sp) {
        /* Rd = SP + imm8*4 */
        cpu->regs[rd] = cpu->regs[REG_SP] + (imm8 << 2);
    } else {
        /* Rd = (PC & ~2) + imm8*4 */
        cpu->regs[rd] = (cpu->regs[REG_PC] & ~2u) + (imm8 << 2);
    }
    /* No flags affected */
    return 1; /* 1S */
}

/* ========================================================================
 * Format 13: Add Offset to Stack Pointer (bits[15:8]=10110000)
 * ADD SP, #imm7*4 / ADD SP, #-imm7*4
 * ======================================================================== */
static int thumb_add_sp_offset(ARM7TDMI* cpu, uint16_t instr) {
    bool negative = BIT(instr, 7);
    uint32_t imm7 = instr & 0x7F;
    uint32_t offset = imm7 << 2;

    if (negative) {
        cpu->regs[REG_SP] -= offset;
    } else {
        cpu->regs[REG_SP] += offset;
    }
    /* No flags affected */
    return 1; /* 1S */
}

/* ========================================================================
 * Format 14: Push/Pop Registers (bits[15:12]=1011, bits[11:9]=x10)
 * PUSH {rlist[, LR]} / POP {rlist[, PC]}
 * ======================================================================== */
static int thumb_push_pop(ARM7TDMI* cpu, uint16_t instr) {
    bool load = BIT(instr, 11);   /* L: 0=PUSH, 1=POP */
    bool r_bit = BIT(instr, 8);   /* R: PUSH=LR, POP=PC */
    uint8_t rlist = instr & 0xFF;

    /* Count registers */
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (BIT(rlist, i)) {
            count++;
        }
    }
    if (r_bit) {
        count++;
    }

    int cycles;

    if (load) {
        /* POP: load registers, then increment SP */
        uint32_t addr = cpu->regs[REG_SP];

        for (int i = 0; i < 8; i++) {
            if (BIT(rlist, i)) {
                cpu->regs[i] = bus_read32(cpu->bus, addr);
                addr += 4;
            }
        }

        if (r_bit) {
            /* Pop PC */
            cpu->regs[REG_PC] = bus_read32(cpu->bus, addr) & ~1u;
            addr += 4;
            cpu_flush_pipeline(cpu);
        }

        cpu->regs[REG_SP] = addr;

        /* nS + 1N + 1I (extra if PC loaded) */
        cycles = count + 1 + 1;
        if (r_bit) {
            cycles++; /* Extra cycle for PC load */
        }
    } else {
        /* PUSH: decrement SP, then store registers */
        uint32_t addr = cpu->regs[REG_SP] - (uint32_t)count * 4;
        cpu->regs[REG_SP] = addr;

        for (int i = 0; i < 8; i++) {
            if (BIT(rlist, i)) {
                bus_write32(cpu->bus, addr, cpu->regs[i]);
                addr += 4;
            }
        }

        if (r_bit) {
            /* Push LR */
            bus_write32(cpu->bus, addr, cpu->regs[REG_LR]);
        }

        /* (n-1)S + 2N */
        cycles = (count > 0) ? count - 1 + 2 : 2;
    }

    return cycles;
}

/* ========================================================================
 * Format 15: Multiple Load/Store (bits[15:12]=1100)
 * STMIA/LDMIA Rb!, {rlist}
 * ======================================================================== */
static int thumb_multiple_load_store(ARM7TDMI* cpu, uint16_t instr) {
    bool load = BIT(instr, 11);
    uint8_t rb = BITS(instr, 10, 8);
    uint8_t rlist = instr & 0xFF;

    uint32_t addr = cpu->regs[rb];
    int count = 0;

    /* Count registers */
    for (int i = 0; i < 8; i++) {
        if (BIT(rlist, i)) {
            count++;
        }
    }

    /* Edge case: empty rlist on ARM7TDMI stores/loads R15 and
     * advances address by 0x40 */
    if (count == 0) {
        if (load) {
            cpu->regs[REG_PC] = bus_read32(cpu->bus, addr) & ~1u;
            cpu_flush_pipeline(cpu);
        } else {
            /* Store PC (which is ahead by 4 in Thumb) */
            bus_write32(cpu->bus, addr, cpu->regs[REG_PC] + 2);
        }
        cpu->regs[rb] = addr + 0x40;
        return 3;
    }

    int cycles;

    if (load) {
        /* LDMIA: load registers, writeback to Rb */
        bool rb_in_list = BIT(rlist, rb);

        for (int i = 0; i < 8; i++) {
            if (BIT(rlist, i)) {
                cpu->regs[i] = bus_read32(cpu->bus, addr);
                addr += 4;
            }
        }

        /* Writeback: only if Rb is NOT in the register list */
        if (!rb_in_list) {
            cpu->regs[rb] = addr;
        }

        /* nS + 1N + 1I */
        cycles = count + 1 + 1;
    } else {
        /* STMIA: store registers, always writeback to Rb */
        /* ARM7TDMI behavior: if Rb is in the list and is the first
         * register, store the old base value. If Rb is in the list but
         * not first, store the new (written-back) base value. */
        uint32_t new_base = addr + (uint32_t)count * 4;
        bool rb_first = BIT(rlist, rb);
        if (rb_first) {
            for (int i = 0; i < rb; i++) {
                if (BIT(rlist, i)) {
                    rb_first = false;
                    break;
                }
            }
        }

        for (int i = 0; i < 8; i++) {
            if (!BIT(rlist, i)) {
                continue;
            }

            if ((uint8_t)i == rb && !rb_first) {
                /* Rb is not the first register: store new base */
                bus_write32(cpu->bus, addr, new_base);
            } else {
                bus_write32(cpu->bus, addr, cpu->regs[i]);
            }
            addr += 4;
        }

        cpu->regs[rb] = new_base;

        /* (n-1)S + 2N */
        cycles = (count > 0) ? count - 1 + 2 : 2;
    }

    return cycles;
}

/* ========================================================================
 * Format 16: Conditional Branch (bits[15:12]=1101, cond!=1110/1111)
 * B{cond} offset8
 * ======================================================================== */
static int thumb_cond_branch(ARM7TDMI* cpu, uint16_t instr) {
    uint8_t cond = BITS(instr, 11, 8);

    if (!cpu_condition_passed(cpu, cond)) {
        return 1; /* Not taken: 1S */
    }

    /* Sign-extend 8-bit offset and shift left 1 */
    int32_t offset = (int32_t)(int8_t)(instr & 0xFF);
    offset <<= 1;

    cpu->regs[REG_PC] += (uint32_t)offset;
    cpu_flush_pipeline(cpu);
    return 3; /* Taken: 2S+1N */
}

/* ========================================================================
 * Format 17: Software Interrupt (bits[15:8]=11011111)
 * SWI comment8
 *
 * Thumb SWI number is the low 8 bits of the instruction.
 * ======================================================================== */
static int thumb_swi(ARM7TDMI* cpu, uint16_t instr) {
    uint32_t swi_num = instr & 0xFF;
    cpu_handle_swi(cpu, swi_num);
    return 3; /* 2S+1N */
}

/* ========================================================================
 * Format 18: Unconditional Branch (bits[15:11]=11100)
 * B offset11
 * ======================================================================== */
static int thumb_unconditional_branch(ARM7TDMI* cpu, uint16_t instr) {
    /* Sign-extend 11-bit offset and shift left 1 */
    int32_t offset = instr & 0x7FF;
    if (offset & 0x400) {
        offset |= (int32_t)0xFFFFF800;
    }
    offset <<= 1;

    cpu->regs[REG_PC] += (uint32_t)offset;
    cpu_flush_pipeline(cpu);
    return 3; /* 2S+1N */
}

/* ========================================================================
 * Format 19: Long Branch with Link (bits[15:12]=1111)
 * Two-instruction BL sequence
 * ======================================================================== */
static int thumb_long_branch_link(ARM7TDMI* cpu, uint16_t instr) {
    bool h_bit = BIT(instr, 11);
    uint32_t offset11 = instr & 0x7FF;

    if (!h_bit) {
        /* First instruction (H=0): LR = PC + (sign_extend(offset11) << 12) */
        int32_t offset = (int32_t)offset11;
        if (offset & 0x400) {
            offset |= (int32_t)0xFFFFF800;
        }
        offset <<= 12;

        cpu->regs[REG_LR] = cpu->regs[REG_PC] + (uint32_t)offset;
        return 1; /* 1S */
    } else {
        /* Second instruction (H=1):
         * temp = next_instr_addr (= PC - 2, since PC is 2 ahead)
         * PC = LR + (offset11 << 1)
         * LR = temp | 1 */
        uint32_t next_instr = cpu->regs[REG_PC] - 2;

        cpu->regs[REG_PC] = cpu->regs[REG_LR] + (offset11 << 1);
        cpu->regs[REG_LR] = next_instr | 1u;

        cpu_flush_pipeline(cpu);
        return 3; /* 2S+1N */
    }
}

/* ========================================================================
 * Main Decode Entry Point
 * ======================================================================== */

int thumb_execute(ARM7TDMI* cpu, uint16_t instr) {
    /* Decode in strict priority order. Most specific bit patterns first. */

    /* 1. Format 19: Long branch with link — (instr & 0xF000) == 0xF000 */
    if ((instr & 0xF000) == 0xF000) {
        return thumb_long_branch_link(cpu, instr);
    }

    /* 2. Format 17: SWI — (instr & 0xFF00) == 0xDF00 */
    if ((instr & 0xFF00) == 0xDF00) {
        return thumb_swi(cpu, instr);
    }

    /* 3. Format 16: Conditional branch — (instr & 0xF000) == 0xD000 */
    if ((instr & 0xF000) == 0xD000) {
        return thumb_cond_branch(cpu, instr);
    }

    /* 4. Format 18: Unconditional branch — (instr & 0xF800) == 0xE000 */
    if ((instr & 0xF800) == 0xE000) {
        return thumb_unconditional_branch(cpu, instr);
    }

    /* 5. Format 14: Push/Pop — (instr & 0xF600) == 0xB400 */
    if ((instr & 0xF600) == 0xB400) {
        return thumb_push_pop(cpu, instr);
    }

    /* 6. Format 13: Add offset to SP — (instr & 0xFF00) == 0xB000 */
    if ((instr & 0xFF00) == 0xB000) {
        return thumb_add_sp_offset(cpu, instr);
    }

    /* 7. Format 15: Multiple load/store — (instr & 0xF000) == 0xC000 */
    if ((instr & 0xF000) == 0xC000) {
        return thumb_multiple_load_store(cpu, instr);
    }

    /* 8. Format 12: Load address — (instr & 0xF000) == 0xA000 */
    if ((instr & 0xF000) == 0xA000) {
        return thumb_load_address(cpu, instr);
    }

    /* 9. Format 11: SP-relative load/store — (instr & 0xF000) == 0x9000 */
    if ((instr & 0xF000) == 0x9000) {
        return thumb_sp_relative_load_store(cpu, instr);
    }

    /* 10. Format 10: Load/store halfword — (instr & 0xF000) == 0x8000 */
    if ((instr & 0xF000) == 0x8000) {
        return thumb_load_store_halfword(cpu, instr);
    }

    /* 10b. Format 9: Load/store imm offset — (instr & 0xE000) == 0x6000 */
    if ((instr & 0xE000) == 0x6000) {
        return thumb_load_store_imm(cpu, instr);
    }

    /* 11. Format 8: Sign-extended load/store — (instr & 0xF200) == 0x5200 */
    if ((instr & 0xF200) == 0x5200) {
        return thumb_load_store_sign_ext(cpu, instr);
    }

    /* 12. Format 7: Register offset load/store — (instr & 0xF200) == 0x5000 */
    if ((instr & 0xF200) == 0x5000) {
        return thumb_load_store_reg(cpu, instr);
    }

    /* 13. Format 6: PC-relative load — (instr & 0xF800) == 0x4800 */
    if ((instr & 0xF800) == 0x4800) {
        return thumb_pc_relative_load(cpu, instr);
    }

    /* 14. Format 5: Hi register ops / BX — (instr & 0xFC00) == 0x4400 */
    if ((instr & 0xFC00) == 0x4400) {
        return thumb_hi_reg_bx(cpu, instr);
    }

    /* 15. Format 4: ALU operations — (instr & 0xFC00) == 0x4000 */
    if ((instr & 0xFC00) == 0x4000) {
        return thumb_alu(cpu, instr);
    }

    /* 16. Format 3: Move/Compare/Add/Sub immediate — (instr & 0xE000) == 0x2000 */
    if ((instr & 0xE000) == 0x2000) {
        return thumb_mov_cmp_add_sub_imm(cpu, instr);
    }

    /* 17. Format 2: Add/Subtract — (instr & 0xF800) == 0x1800 */
    if ((instr & 0xF800) == 0x1800) {
        return thumb_add_subtract(cpu, instr);
    }

    /* 18. Format 1: Move shifted register — (instr & 0xE000) == 0x0000 */
    if ((instr & 0xE000) == 0x0000) {
        return thumb_move_shifted(cpu, instr);
    }

    LOG_WARN("Unimplemented Thumb instruction: 0x%04X at PC=0x%08X",
             instr, cpu->regs[REG_PC] - 4);
    return 1;
}
