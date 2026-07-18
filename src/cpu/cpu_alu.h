#ifndef CPU_ALU_H
#define CPU_ALU_H

/* ALU flag helpers and barrel shifter shared by the ARM and Thumb
 * decoders.  These MUST behave bit-identically in both instruction
 * sets — keeping a single copy here prevents silent divergence. */

#include "arm7tdmi.h"

/* Set N and Z flags from a 32-bit result */
static inline void set_nz_flags(ARM7TDMI* cpu, uint32_t result) {
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
static inline void set_c_flag(ARM7TDMI* cpu, bool carry) {
    if (carry) {
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_C);
    } else {
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_C);
    }
}

/* Set overflow flag */
static inline void set_v_flag(ARM7TDMI* cpu, bool overflow) {
    if (overflow) {
        cpu->cpsr = SET_BIT(cpu->cpsr, CPSR_V);
    } else {
        cpu->cpsr = CLR_BIT(cpu->cpsr, CPSR_V);
    }
}

/* Detect addition overflow: (a ^ result) & (b ^ result) bit31 */
static inline bool add_overflow(uint32_t a, uint32_t b, uint32_t result) {
    return BIT((a ^ result) & (b ^ result), 31);
}

/* Detect subtraction overflow: (a ^ b) & (a ^ result) bit31
 * For SUB a - b, where result = a - b */
static inline bool sub_overflow(uint32_t a, uint32_t b, uint32_t result) {
    return BIT((a ^ b) & (a ^ result), 31);
}

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
static inline uint32_t barrel_shift(uint32_t value, uint8_t shift_type,
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

/* Barrel shift for register-specified shift amounts (Thumb Format 4 ALU). */
static inline uint32_t barrel_shift_reg(uint32_t value, uint8_t shift_type,
                                        uint8_t amount, bool* carry_out) {
    return barrel_shift(value, shift_type, amount, carry_out, true);
}

#endif // CPU_ALU_H
