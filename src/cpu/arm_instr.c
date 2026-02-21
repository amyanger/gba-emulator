#include "arm_instr.h"
#include "memory/bus.h"

/*
 * ARM (32-bit) instruction decoder and executor for the ARM7TDMI.
 *
 * Decode order is critical and follows the priority defined by the ARM7TDMI
 * instruction encoding space. More specific bit patterns must be checked
 * before less specific ones to avoid mis-decoding.
 *
 * References: GBATEK "ARM Opcodes", ARM7TDMI Technical Reference Manual.
 */

/* ========================================================================
 * Barrel Shifter
 * ======================================================================== */

/*
 * Perform a barrel shift operation shared by data processing and single
 * data transfer instructions.
 *
 * shift_type: 0=LSL, 1=LSR, 2=ASR, 3=ROR
 * carry_out:  on entry holds the current C flag; on exit holds the
 *             shifter carry output.
 * reg_shift:  true when the shift amount comes from a register (bit4=1),
 *             false when from a 5-bit immediate (bit4=0).
 */
static uint32_t barrel_shift(uint32_t value, uint8_t shift_type,
                             uint8_t amount, bool* carry_out,
                             bool reg_shift) {
    if (reg_shift) {
        /* Register-specified shift amount (bottom byte of Rs).
         * When amount == 0, the value passes through unchanged and the
         * carry flag is preserved for ALL shift types. */
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
                /* amount >= 32: result is all-sign, carry = sign bit */
                *carry_out = BIT(value, 31);
                return BIT(value, 31) ? 0xFFFFFFFF : 0;
            }

        case 3: /* ROR */
            amount &= 31; /* Reduce modulo 32 */
            if (amount == 0) {
                /* ROR by 32 (or multiple of 32): result = value, C = bit31 */
                *carry_out = BIT(value, 31);
                return value;
            }
            *carry_out = BIT(value, amount - 1);
            return (value >> amount) | (value << (32 - amount));

        default:
            return value;
        }
    } else {
        /* Immediate (5-bit) shift amount.
         * Special cases when amount == 0 differ per shift type. */
        switch (shift_type) {
        case 0: /* LSL */
            if (amount == 0) {
                /* LSL #0: value unchanged, carry preserved */
                return value;
            }
            *carry_out = BIT(value, 32 - amount);
            return value << amount;

        case 1: /* LSR */
            if (amount == 0) {
                /* LSR #0 encodes LSR #32 */
                *carry_out = BIT(value, 31);
                return 0;
            }
            *carry_out = BIT(value, amount - 1);
            return value >> amount;

        case 2: /* ASR */
            if (amount == 0) {
                /* ASR #0 encodes ASR #32: all sign-extension */
                *carry_out = BIT(value, 31);
                return BIT(value, 31) ? 0xFFFFFFFF : 0;
            }
            *carry_out = BIT(value, amount - 1);
            return (uint32_t)((int32_t)value >> amount);

        case 3: /* ROR */
            if (amount == 0) {
                /* ROR #0 encodes RRX (rotate right extended):
                 * result = (C_in << 31) | (value >> 1), carry = bit0 */
                bool old_c = *carry_out;
                *carry_out = BIT(value, 0);
                return ((uint32_t)old_c << 31) | (value >> 1);
            }
            *carry_out = BIT(value, amount - 1);
            return (value >> amount) | (value << (32 - amount));

        default:
            return value;
        }
    }
}

/* ========================================================================
 * Data Processing Operand2 Helpers
 * ======================================================================== */

/*
 * Decode operand2 for data processing instructions.
 * When I=1 (bit25): 8-bit immediate rotated right by 2*rotate4.
 * When I=0 (bit25): register Rm with barrel shift.
 * Returns the operand2 value and updates shifter_carry.
 */
static uint32_t decode_dp_operand2(ARM7TDMI* cpu, uint32_t instr,
                                   bool* shifter_carry) {
    *shifter_carry = BIT(cpu->cpsr, CPSR_C);

    if (BIT(instr, 25)) {
        /* Immediate operand: 8-bit value rotated right by 2*rotate */
        uint32_t imm8 = instr & 0xFF;
        uint32_t rotate = BITS(instr, 11, 8) * 2;
        if (rotate == 0) {
            /* carry unchanged */
            return imm8;
        }
        uint32_t result = (imm8 >> rotate) | (imm8 << (32 - rotate));
        *shifter_carry = BIT(result, 31);
        return result;
    } else {
        /* Register operand with shift */
        uint8_t rm = instr & 0xF;
        uint32_t rm_val = cpu->regs[rm];
        uint8_t shift_type = BITS(instr, 6, 5);

        if (BIT(instr, 4)) {
            /* Register-specified shift (bit4=1) */
            uint8_t rs = BITS(instr, 11, 8);
            uint8_t shift_amount = (uint8_t)(cpu->regs[rs] & 0xFF);
            /* When Rm is R15 with register shift, PC + 12 */
            if (rm == REG_PC) {
                rm_val += 4;
            }
            return barrel_shift(rm_val, shift_type, shift_amount,
                                shifter_carry, true);
        } else {
            /* Immediate shift (bit4=0) */
            uint8_t shift_amount = BITS(instr, 11, 7);
            /* When Rm is R15 with immediate shift, value is PC (addr+8) */
            return barrel_shift(rm_val, shift_type, shift_amount,
                                shifter_carry, false);
        }
    }
}

/* ========================================================================
 * Flag Helpers
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

/* Detect subtraction overflow: (a ^ b) & (a ^ result) bit31
 * For SUB a - b, where result = a - b */
static bool sub_overflow(uint32_t a, uint32_t b, uint32_t result) {
    return BIT((a ^ b) & (a ^ result), 31);
}

/* ========================================================================
 * Instruction Handlers
 * ======================================================================== */

/* Software Interrupt: enter SVC mode via cpu_handle_swi */
static int arm_swi(ARM7TDMI* cpu, uint32_t instr) {
    (void)instr;
    cpu_handle_swi(cpu);
    return 3;
}

/* Branch (B) and Branch with Link (BL) */
static int arm_branch(ARM7TDMI* cpu, uint32_t instr) {
    bool link = BIT(instr, 24);

    /* Sign-extend the 24-bit offset and shift left 2 */
    int32_t offset = (int32_t)(instr << 8) >> 6;

    if (link) {
        /* BL: save return address in LR.
         * PC = executing_addr + 8, so executing_addr + 4 = PC - 4 */
        cpu->regs[REG_LR] = cpu->regs[REG_PC] - 4;
    }

    /* PC already points to executing_addr + 8 */
    cpu->regs[REG_PC] += (uint32_t)offset;
    cpu_flush_pipeline(cpu);
    return 3;
}

/* Block Data Transfer: LDM/STM with addressing mode variants */
static int arm_block_transfer(ARM7TDMI* cpu, uint32_t instr) {
    bool pre = BIT(instr, 24);      /* P: pre/post indexing */
    bool up = BIT(instr, 23);       /* U: add/subtract */
    bool s_bit = BIT(instr, 22);    /* S: PSR/user-mode force */
    bool writeback = BIT(instr, 21);/* W: base writeback */
    bool load = BIT(instr, 20);     /* L: load/store */
    uint8_t rn = BITS(instr, 19, 16);
    uint16_t rlist = instr & 0xFFFF;

    uint32_t base = cpu->regs[rn];
    int count = 0;
    int cycles = 0;

    /* Count registers in the list */
    for (int i = 0; i < 16; i++) {
        if (BIT(rlist, i)) {
            count++;
        }
    }

    /* Empty register list: transfer R15, offset by 0x40 (ARM7TDMI quirk) */
    if (count == 0) {
        count = 16; /* adjust offset as if 16 registers */
        rlist = 0x8000; /* only R15 */
    }

    /* Calculate start address based on addressing mode:
     * IA (P=0,U=1): start at base
     * IB (P=1,U=1): start at base+4
     * DA (P=0,U=0): start at base - count*4 + 4
     * DB (P=1,U=0): start at base - count*4
     */
    uint32_t addr;
    if (up) {
        addr = pre ? base + 4 : base;
    } else {
        addr = pre ? base - (uint32_t)count * 4
                   : base - (uint32_t)count * 4 + 4;
    }

    /* Calculate the final base value after writeback */
    uint32_t new_base;
    if (up) {
        new_base = base + (uint32_t)count * 4;
    } else {
        new_base = base - (uint32_t)count * 4;
    }

    /* Determine if we should access user-mode registers.
     * S bit without R15 in the list forces user-mode register access. */
    bool user_mode_transfer = s_bit && !(load && BIT(rlist, 15));

    /* Save the old mode if we need to swap to user registers */
    CPUMode old_mode = cpu_get_mode(cpu);
    if (user_mode_transfer && old_mode != CPU_MODE_USR &&
        old_mode != CPU_MODE_SYS) {
        cpu_switch_mode(cpu, CPU_MODE_USR);
    }

    /* Determine if Rn is the first register in the list (for STM edge case) */
    bool rn_first = false;
    if (BIT(rlist, rn)) {
        rn_first = true;
        for (int i = 0; i < rn; i++) {
            if (BIT(rlist, i)) {
                rn_first = false;
                break;
            }
        }
    }

    /* Transfer registers lowest-first at ascending addresses */
    bool first_transfer = true;
    bool pc_loaded = false;
    for (int i = 0; i < 16; i++) {
        if (!BIT(rlist, i)) {
            continue;
        }

        if (load) {
            /* LDM: load from memory into register */
            uint32_t val = bus_read32(cpu->bus, addr & ~3u);
            if (i == REG_PC) {
                cpu->regs[REG_PC] = val & ~3u;
                pc_loaded = true;
            } else {
                cpu->regs[i] = val;
            }
            cycles += (first_transfer) ? 2 : 1;
        } else {
            /* STM: store register to memory */
            uint32_t val;
            if (i == REG_PC) {
                val = cpu->regs[REG_PC] + 4;
            } else if ((uint8_t)i == rn && !rn_first && writeback) {
                /* If Rn is not the first register and writeback is set,
                 * store the updated (written-back) base value */
                val = new_base;
            } else {
                val = cpu->regs[i];
            }
            bus_write32(cpu->bus, addr & ~3u, val);
            cycles += (first_transfer) ? 2 : 1;
        }

        addr += 4;
        first_transfer = false;
    }

    /* Restore mode if we switched to user mode for the transfer */
    if (user_mode_transfer && old_mode != CPU_MODE_USR &&
        old_mode != CPU_MODE_SYS) {
        cpu_switch_mode(cpu, old_mode);
    }

    /* Writeback: update base register.
     * For LDM with Rn in rlist and W=1, the loaded value wins
     * (writeback does not happen). */
    if (writeback) {
        if (load && BIT(rlist, rn)) {
            /* Loaded value already in register, skip writeback */
        } else {
            cpu->regs[rn] = new_base;
        }
    }

    /* S bit with R15 in LDM: CPSR = SPSR (exception return) */
    if (load && pc_loaded && s_bit) {
        uint32_t* spsr = cpu_get_spsr(cpu);
        if (spsr) {
            CPUMode current = cpu_get_mode(cpu);
            CPUMode target = (CPUMode)(*spsr & 0x1F);
            if (current != target) {
                cpu_switch_mode(cpu, target);
            }
            cpu->cpsr = *spsr;
        }
    }

    /* Flush pipeline if PC was loaded */
    if (pc_loaded) {
        cpu_flush_pipeline(cpu);
    }

    return cycles;
}

/* Undefined instruction: trap */
static int arm_undefined(ARM7TDMI* cpu, uint32_t instr) {
    LOG_WARN("ARM undefined instruction: 0x%08X at PC=0x%08X",
             instr, cpu->regs[REG_PC] - 8);
    /* In a full implementation, this would trigger an UND exception.
     * For now, just log it. */
    (void)cpu;
    return 1;
}

/* Single Data Transfer: LDR/STR/LDRB/STRB */
static int arm_single_transfer(ARM7TDMI* cpu, uint32_t instr) {
    bool reg_offset = BIT(instr, 25); /* I: 0=imm, 1=reg offset */
    bool pre = BIT(instr, 24);        /* P: pre-index */
    bool up = BIT(instr, 23);         /* U: add offset */
    bool byte = BIT(instr, 22);       /* B: byte transfer */
    bool writeback = BIT(instr, 21);  /* W: writeback (or T bit post) */
    bool load = BIT(instr, 20);       /* L: load/store */
    uint8_t rn = BITS(instr, 19, 16);
    uint8_t rd = BITS(instr, 15, 12);

    uint32_t base = cpu->regs[rn];
    uint32_t offset;

    if (!reg_offset) {
        /* Immediate 12-bit offset */
        offset = instr & 0xFFF;
    } else {
        /* Register offset with shift (same as data processing operand2
         * but only immediate shift amounts, never register shift) */
        uint8_t rm = instr & 0xF;
        uint32_t rm_val = cpu->regs[rm];
        uint8_t shift_type = BITS(instr, 6, 5);
        uint8_t shift_amount = BITS(instr, 11, 7);
        bool carry = BIT(cpu->cpsr, CPSR_C);
        offset = barrel_shift(rm_val, shift_type, shift_amount,
                              &carry, false);
    }

    /* Calculate the effective address */
    uint32_t addr;
    if (pre) {
        addr = up ? base + offset : base - offset;
    } else {
        addr = base;
    }

    int cycles;

    if (load) {
        /* LDR/LDRB */
        if (byte) {
            cpu->regs[rd] = bus_read8(cpu->bus, addr);
        } else {
            /* LDR: word load with rotation for unaligned addresses */
            uint32_t aligned_addr = addr & ~3u;
            uint32_t word = bus_read32(cpu->bus, aligned_addr);
            uint8_t rot = (uint8_t)((addr & 3u) * 8);
            if (rot != 0) {
                word = (word >> rot) | (word << (32 - rot));
            }
            cpu->regs[rd] = word;
        }
        cycles = 3; /* 1S + 1N + 1I */

        /* If Rd is R15, flush pipeline */
        if (rd == REG_PC) {
            cpu->regs[REG_PC] &= ~3u;
            cpu_flush_pipeline(cpu);
            cycles += 2;
        }
    } else {
        /* STR/STRB */
        uint32_t val = cpu->regs[rd];
        /* STR with Rd = R15: stored value is PC (= executing_addr + 8) */
        if (rd == REG_PC) {
            val = cpu->regs[REG_PC] + 4;
        }

        if (byte) {
            bus_write8(cpu->bus, addr, (uint8_t)val);
        } else {
            bus_write32(cpu->bus, addr & ~3u, val);
        }
        cycles = 2; /* 1N + 1N */
    }

    /* Post-index: compute address after transfer */
    if (!pre) {
        /* Post-index always writes back */
        cpu->regs[rn] = up ? base + offset : base - offset;
    } else if (writeback) {
        /* Pre-index with W=1: writeback */
        cpu->regs[rn] = addr;
    }

    return cycles;
}

/* Branch and Exchange: BX Rm */
static int arm_bx(ARM7TDMI* cpu, uint32_t instr) {
    uint8_t rm = instr & 0xF;
    uint32_t addr = cpu->regs[rm];

    if (BIT(addr, 0)) {
        /* Switch to Thumb state */
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_T);
        cpu->regs[REG_PC] = addr & ~1u;
    } else {
        /* Stay in ARM state */
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_T);
        cpu->regs[REG_PC] = addr & ~3u;
    }

    cpu_flush_pipeline(cpu);
    return 3;
}

/* Single Data Swap: SWP/SWPB */
static int arm_swap(ARM7TDMI* cpu, uint32_t instr) {
    bool byte = BIT(instr, 22);
    uint8_t rn = BITS(instr, 19, 16);
    uint8_t rd = BITS(instr, 15, 12);
    uint8_t rm = instr & 0xF;

    uint32_t addr = cpu->regs[rn];

    if (byte) {
        /* SWPB: byte swap */
        uint8_t temp = bus_read8(cpu->bus, addr);
        bus_write8(cpu->bus, addr, (uint8_t)cpu->regs[rm]);
        cpu->regs[rd] = temp;
    } else {
        /* SWP: word swap with LDR-style rotation for misaligned */
        uint32_t aligned = addr & ~3u;
        uint32_t word = bus_read32(cpu->bus, aligned);
        uint8_t rot = (uint8_t)((addr & 3u) * 8);
        if (rot != 0) {
            word = (word >> rot) | (word << (32 - rot));
        }
        bus_write32(cpu->bus, aligned, cpu->regs[rm]);
        cpu->regs[rd] = word;
    }

    return 4; /* 1S + 2N + 1I */
}

/* Multiply Long: UMULL/UMLAL/SMULL/SMLAL */
static int arm_multiply_long(ARM7TDMI* cpu, uint32_t instr) {
    bool is_signed = BIT(instr, 22);
    bool accumulate = BIT(instr, 21);
    bool set_flags = BIT(instr, 20);
    uint8_t rd_hi = BITS(instr, 19, 16);
    uint8_t rd_lo = BITS(instr, 15, 12);
    uint8_t rs = BITS(instr, 11, 8);
    uint8_t rm = instr & 0xF;

    uint64_t result;

    if (is_signed) {
        int64_t a = (int64_t)(int32_t)cpu->regs[rm];
        int64_t b = (int64_t)(int32_t)cpu->regs[rs];
        result = (uint64_t)(a * b);
    } else {
        uint64_t a = (uint64_t)cpu->regs[rm];
        uint64_t b = (uint64_t)cpu->regs[rs];
        result = a * b;
    }

    if (accumulate) {
        /* Add existing value in RdHi:RdLo */
        uint64_t accum = ((uint64_t)cpu->regs[rd_hi] << 32) |
                         (uint64_t)cpu->regs[rd_lo];
        result += accum;
    }

    cpu->regs[rd_lo] = (uint32_t)(result & 0xFFFFFFFF);
    cpu->regs[rd_hi] = (uint32_t)(result >> 32);

    if (set_flags) {
        set_nz_flags(cpu, cpu->regs[rd_hi]);
        /* Z should reflect the full 64-bit result */
        if (result == 0) {
            cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_Z);
        } else {
            cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_Z);
        }
        /* N = bit63 of result */
        if (BIT(cpu->regs[rd_hi], 31)) {
            cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_N);
        } else {
            cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_N);
        }
        /* C and V are destroyed (UNPREDICTABLE), leave unchanged */
    }

    return accumulate ? 5 : 4;
}

/* Multiply: MUL/MLA */
static int arm_multiply(ARM7TDMI* cpu, uint32_t instr) {
    bool accumulate = BIT(instr, 21);
    bool set_flags = BIT(instr, 20);
    uint8_t rd = BITS(instr, 19, 16);
    uint8_t rn = BITS(instr, 15, 12);
    uint8_t rs = BITS(instr, 11, 8);
    uint8_t rm = instr & 0xF;

    uint32_t result = cpu->regs[rm] * cpu->regs[rs];
    if (accumulate) {
        result += cpu->regs[rn];
    }

    cpu->regs[rd] = result;

    if (set_flags) {
        set_nz_flags(cpu, result);
        /* C flag is destroyed (UNPREDICTABLE on ARM7TDMI), leave unchanged */
    }

    return accumulate ? 3 : 2;
}

/* Halfword/Signed Data Transfer: LDRH/STRH/LDRSB/LDRSH */
static int arm_halfword_transfer(ARM7TDMI* cpu, uint32_t instr) {
    bool pre = BIT(instr, 24);
    bool up = BIT(instr, 23);
    bool imm_offset = BIT(instr, 22); /* NOTE: I=1 means IMMEDIATE here */
    bool writeback = BIT(instr, 21);
    bool load = BIT(instr, 20);
    uint8_t rn = BITS(instr, 19, 16);
    uint8_t rd = BITS(instr, 15, 12);
    uint8_t sh = BITS(instr, 6, 5);

    uint32_t base = cpu->regs[rn];
    uint32_t offset;

    if (imm_offset) {
        /* Immediate offset: high nibble | low nibble */
        offset = (BITS(instr, 11, 8) << 4) | (instr & 0xF);
    } else {
        /* Register offset: Rm */
        uint8_t rm = instr & 0xF;
        offset = cpu->regs[rm];
    }

    /* Calculate effective address */
    uint32_t addr;
    if (pre) {
        addr = up ? base + offset : base - offset;
    } else {
        addr = base;
    }

    int cycles;

    if (load) {
        switch (sh) {
        case 1: /* LDRH: unsigned halfword */
            if (addr & 1u) {
                /* Misaligned halfword load: rotated read */
                uint32_t val = bus_read16(cpu->bus, addr & ~1u);
                cpu->regs[rd] = (val >> 8) | (val << 24);
            } else {
                cpu->regs[rd] = bus_read16(cpu->bus, addr);
            }
            break;

        case 2: /* LDRSB: signed byte */
            cpu->regs[rd] = (uint32_t)(int32_t)(int8_t)bus_read8(cpu->bus,
                                                                  addr);
            break;

        case 3: /* LDRSH: signed halfword */
            if (addr & 1u) {
                /* Misaligned LDRSH: loads byte, sign-extends to 32 */
                cpu->regs[rd] = (uint32_t)(int32_t)(int8_t)bus_read8(
                    cpu->bus, addr);
            } else {
                cpu->regs[rd] = (uint32_t)(int32_t)(int16_t)bus_read16(
                    cpu->bus, addr);
            }
            break;

        default: /* SH=0: should not reach here */
            break;
        }
        cycles = 3;

        if (rd == REG_PC) {
            cpu->regs[REG_PC] &= ~3u;
            cpu_flush_pipeline(cpu);
            cycles += 2;
        }
    } else {
        /* STRH: only SH=01 is valid for store */
        uint32_t val = cpu->regs[rd];
        if (rd == REG_PC) {
            val = cpu->regs[REG_PC] + 4;
        }
        bus_write16(cpu->bus, addr & ~1u, (uint16_t)val);
        cycles = 2;
    }

    /* Writeback */
    if (!pre) {
        /* Post-index: always writeback */
        uint32_t wb_addr = up ? base + offset : base - offset;
        cpu->regs[rn] = wb_addr;
    } else if (writeback) {
        cpu->regs[rn] = addr;
    }

    return cycles;
}

/* MSR: Move to Status Register (both immediate and register forms) */
static int arm_msr(ARM7TDMI* cpu, uint32_t instr) {
    bool use_spsr = BIT(instr, 22);

    /* Decode the operand */
    uint32_t operand;
    if (BIT(instr, 25)) {
        /* Immediate form */
        uint32_t imm8 = instr & 0xFF;
        uint32_t rotate = BITS(instr, 11, 8) * 2;
        if (rotate == 0) {
            operand = imm8;
        } else {
            operand = (imm8 >> rotate) | (imm8 << (32 - rotate));
        }
    } else {
        /* Register form: Rm */
        uint8_t rm = instr & 0xF;
        operand = cpu->regs[rm];
    }

    /* Build write mask from field mask bits [19:16] */
    uint32_t write_mask = 0;
    if (BIT(instr, 19)) write_mask |= 0xFF000000; /* f: flags */
    if (BIT(instr, 18)) write_mask |= 0x00FF0000; /* s: status */
    if (BIT(instr, 17)) write_mask |= 0x0000FF00; /* x: extension */
    if (BIT(instr, 16)) write_mask |= 0x000000FF; /* c: control */

    if (use_spsr) {
        /* Write to SPSR of current mode */
        uint32_t* spsr = cpu_get_spsr(cpu);
        if (spsr) {
            *spsr = (*spsr & ~write_mask) | (operand & write_mask);
        }
    } else {
        /* Write to CPSR */
        CPUMode current_mode = cpu_get_mode(cpu);

        /* In USR mode, only the flags field is writable */
        if (current_mode == CPU_MODE_USR) {
            write_mask &= 0xFF000000;
        }

        /* Compute the new CPSR value */
        uint32_t new_cpsr = (cpu->cpsr & ~write_mask) |
                            (operand & write_mask);
        CPUMode new_mode = (CPUMode)(new_cpsr & 0x1F);

        if (current_mode != new_mode) {
            /* Mode is changing. Write the non-mode fields first, then
             * switch mode. cpu_switch_mode reads old mode from cpsr,
             * does banking, writes new mode bits. */
            cpu->cpsr = (cpu->cpsr & ~write_mask) |
                        (operand & write_mask);
            /* Undo the mode bit change so cpu_switch_mode sees old mode */
            cpu->cpsr = (cpu->cpsr & ~0x1Fu) | current_mode;
            cpu_switch_mode(cpu, new_mode);
            /* Re-apply the full write including mode bits.
             * cpu_switch_mode already set the mode bits, so apply the
             * rest of the fields on top. */
            cpu->cpsr = (cpu->cpsr & ~write_mask) |
                        (operand & write_mask);
        } else {
            cpu->cpsr = new_cpsr;
        }
    }

    return 1;
}

/* MRS: Move from Status Register to general register */
static int arm_mrs(ARM7TDMI* cpu, uint32_t instr) {
    bool use_spsr = BIT(instr, 22);
    uint8_t rd = BITS(instr, 15, 12);

    if (use_spsr) {
        uint32_t* spsr = cpu_get_spsr(cpu);
        cpu->regs[rd] = spsr ? *spsr : cpu->cpsr;
    } else {
        cpu->regs[rd] = cpu->cpsr;
    }

    return 1;
}

/* Data Processing: all 16 ALU opcodes */
static int arm_data_processing(ARM7TDMI* cpu, uint32_t instr) {
    uint8_t opcode = BITS(instr, 24, 21);
    bool set_flags = BIT(instr, 20);
    uint8_t rn = BITS(instr, 19, 16);
    uint8_t rd = BITS(instr, 15, 12);

    /* Decode operand2 with barrel shifter */
    bool shifter_carry = false;
    uint32_t op2 = decode_dp_operand2(cpu, instr, &shifter_carry);

    uint32_t rn_val = cpu->regs[rn];

    /* If Rn is R15 and we are using a register shift (bit4=1),
     * the PC value is addr+12 instead of addr+8. */
    if (rn == REG_PC && !BIT(instr, 25) && BIT(instr, 4)) {
        rn_val += 4;
    }

    uint32_t result = 0;
    bool write_result = true;
    bool is_logical = false;

    /* Pre-read the carry flag for arithmetic ops that need it */
    uint32_t old_c = BIT(cpu->cpsr, CPSR_C);

    switch (opcode) {
    case 0x0: /* AND: Rd = Rn & Op2 */
        result = rn_val & op2;
        is_logical = true;
        break;

    case 0x1: /* EOR: Rd = Rn ^ Op2 */
        result = rn_val ^ op2;
        is_logical = true;
        break;

    case 0x2: { /* SUB: Rd = Rn - Op2 */
        uint64_t res64 = (uint64_t)rn_val + (uint64_t)~op2 + 1ULL;
        result = (uint32_t)res64;
        if (set_flags) {
            set_nz_flags(cpu, result);
            set_c_flag(cpu, res64 > 0xFFFFFFFF);
            set_v_flag(cpu, sub_overflow(rn_val, op2, result));
        }
        break;
    }

    case 0x3: { /* RSB: Rd = Op2 - Rn */
        uint64_t res64 = (uint64_t)op2 + (uint64_t)~rn_val + 1ULL;
        result = (uint32_t)res64;
        if (set_flags) {
            set_nz_flags(cpu, result);
            set_c_flag(cpu, res64 > 0xFFFFFFFF);
            set_v_flag(cpu, sub_overflow(op2, rn_val, result));
        }
        break;
    }

    case 0x4: { /* ADD: Rd = Rn + Op2 */
        uint64_t res64 = (uint64_t)rn_val + (uint64_t)op2;
        result = (uint32_t)res64;
        if (set_flags) {
            set_nz_flags(cpu, result);
            set_c_flag(cpu, res64 > 0xFFFFFFFF);
            set_v_flag(cpu, add_overflow(rn_val, op2, result));
        }
        break;
    }

    case 0x5: { /* ADC: Rd = Rn + Op2 + C */
        uint64_t res64 = (uint64_t)rn_val + (uint64_t)op2 +
                         (uint64_t)old_c;
        result = (uint32_t)res64;
        if (set_flags) {
            set_nz_flags(cpu, result);
            set_c_flag(cpu, res64 > 0xFFFFFFFF);
            set_v_flag(cpu, add_overflow(rn_val, op2, result));
        }
        break;
    }

    case 0x6: { /* SBC: Rd = Rn - Op2 - !C = Rn + ~Op2 + C */
        uint64_t res64 = (uint64_t)rn_val + (uint64_t)~op2 +
                         (uint64_t)old_c;
        result = (uint32_t)res64;
        if (set_flags) {
            set_nz_flags(cpu, result);
            set_c_flag(cpu, res64 > 0xFFFFFFFF);
            set_v_flag(cpu, sub_overflow(rn_val, op2, result));
        }
        break;
    }

    case 0x7: { /* RSC: Rd = Op2 - Rn - !C = Op2 + ~Rn + C */
        uint64_t res64 = (uint64_t)op2 + (uint64_t)~rn_val +
                         (uint64_t)old_c;
        result = (uint32_t)res64;
        if (set_flags) {
            set_nz_flags(cpu, result);
            set_c_flag(cpu, res64 > 0xFFFFFFFF);
            set_v_flag(cpu, sub_overflow(op2, rn_val, result));
        }
        break;
    }

    case 0x8: /* TST: Rn & Op2, flags only */
        result = rn_val & op2;
        write_result = false;
        is_logical = true;
        set_flags = true; /* TST always sets flags */
        break;

    case 0x9: /* TEQ: Rn ^ Op2, flags only */
        result = rn_val ^ op2;
        write_result = false;
        is_logical = true;
        set_flags = true;
        break;

    case 0xA: { /* CMP: Rn - Op2, flags only */
        uint64_t res64 = (uint64_t)rn_val + (uint64_t)~op2 + 1ULL;
        result = (uint32_t)res64;
        write_result = false;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, sub_overflow(rn_val, op2, result));
        break;
    }

    case 0xB: { /* CMN: Rn + Op2, flags only */
        uint64_t res64 = (uint64_t)rn_val + (uint64_t)op2;
        result = (uint32_t)res64;
        write_result = false;
        set_nz_flags(cpu, result);
        set_c_flag(cpu, res64 > 0xFFFFFFFF);
        set_v_flag(cpu, add_overflow(rn_val, op2, result));
        break;
    }

    case 0xC: /* ORR: Rd = Rn | Op2 */
        result = rn_val | op2;
        is_logical = true;
        break;

    case 0xD: /* MOV: Rd = Op2 */
        result = op2;
        is_logical = true;
        break;

    case 0xE: /* BIC: Rd = Rn & ~Op2 */
        result = rn_val & ~op2;
        is_logical = true;
        break;

    case 0xF: /* MVN: Rd = ~Op2 */
        result = ~op2;
        is_logical = true;
        break;
    }

    /* Set flags for logical operations.
     * Arithmetic ops handled inline above. */
    if (set_flags && is_logical) {
        set_nz_flags(cpu, result);
        set_c_flag(cpu, shifter_carry);
        /* V unchanged for logical ops */
    }

    /* Write result to Rd */
    if (write_result) {
        cpu->regs[rd] = result;

        if (rd == REG_PC) {
            if (set_flags) {
                /* When Rd=R15 and S=1: CPSR = SPSR, then flush */
                uint32_t* spsr = cpu_get_spsr(cpu);
                if (spsr) {
                    CPUMode old_m = cpu_get_mode(cpu);
                    CPUMode new_m = (CPUMode)(*spsr & 0x1F);
                    if (old_m != new_m) {
                        cpu_switch_mode(cpu, new_m);
                    }
                    cpu->cpsr = *spsr;
                }
            }
            cpu->regs[REG_PC] &= ~3u;
            cpu_flush_pipeline(cpu);
        }
    }

    /* Cycle timing: 1S for most, +1I for register shift, +1S+1N for PC write */
    int cycles = 1;
    if (!BIT(instr, 25) && BIT(instr, 4)) {
        cycles++; /* register shift adds 1I */
    }
    if (write_result && rd == REG_PC) {
        cycles += 2; /* branch penalty */
    }
    return cycles;
}

/* ========================================================================
 * Main Decode Entry Point
 * ======================================================================== */

int arm_execute(ARM7TDMI* cpu, uint32_t instr) {
    /* Decode in strict priority order. More specific patterns first. */

    /* 1. SWI: bits[27:24] = 1111 */
    if ((instr & 0x0F000000) == 0x0F000000) {
        return arm_swi(cpu, instr);
    }

    /* 2. Branch B/BL: bits[27:25] = 101 */
    if ((instr & 0x0E000000) == 0x0A000000) {
        return arm_branch(cpu, instr);
    }

    /* 3. Block Data Transfer LDM/STM: bits[27:25] = 100 */
    if ((instr & 0x0E000000) == 0x08000000) {
        return arm_block_transfer(cpu, instr);
    }

    /* 4. Undefined: bits[27:25] = 011, bit4 = 1 */
    if ((instr & 0x0E000010) == 0x06000010) {
        return arm_undefined(cpu, instr);
    }

    /* 5. Single Data Transfer LDR/STR: bits[27:26] = 01 */
    if ((instr & 0x0C000000) == 0x04000000) {
        return arm_single_transfer(cpu, instr);
    }

    /* --- bits[27:26] = 00 sub-decode --- */

    /* 6. BX: 0001 0010 1111 1111 1111 0001 xxxx */
    if ((instr & 0x0FFFFFF0) == 0x012FFF10) {
        return arm_bx(cpu, instr);
    }

    /* 7. SWP/SWPB */
    if ((instr & 0x0FB00FF0) == 0x01000090) {
        return arm_swap(cpu, instr);
    }

    /* 8. Multiply Long (UMULL/UMLAL/SMULL/SMLAL) */
    if ((instr & 0x0F8000F0) == 0x00800090) {
        return arm_multiply_long(cpu, instr);
    }

    /* 9. Multiply (MUL/MLA) */
    if ((instr & 0x0FC000F0) == 0x00000090) {
        return arm_multiply(cpu, instr);
    }

    /* 10. Halfword Transfer: bit7=1, bit4=1, bits[6:5] != 00 */
    if ((instr & 0x0E000090) == 0x00000090 &&
        (instr & 0x00000060) != 0) {
        return arm_halfword_transfer(cpu, instr);
    }

    /* 11. MSR immediate */
    if ((instr & 0x0FB0F000) == 0x0320F000) {
        return arm_msr(cpu, instr);
    }

    /* 12. MRS */
    if ((instr & 0x0FBF0FFF) == 0x010F0000) {
        return arm_mrs(cpu, instr);
    }

    /* 13. MSR register */
    if ((instr & 0x0FB0FFF0) == 0x0120F000) {
        return arm_msr(cpu, instr);
    }

    /* 14. Data Processing (catch-all for bits[27:26] = 00) */
    return arm_data_processing(cpu, instr);
}
