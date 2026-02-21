#include "arm7tdmi.h"
#include "arm_instr.h"
#include "thumb_instr.h"
#include "memory/bus.h"
#include "interrupt/interrupt.h"

/* Return banked-register array offset for modes that bank only SP/LR.
 * FIQ is handled separately (banks R8-R14). USR/SYS have no private bank. */
static int bank_offset_for_mode(CPUMode mode) {
    switch (mode) {
    case CPU_MODE_SVC: return 7;
    case CPU_MODE_ABT: return 9;
    case CPU_MODE_IRQ: return 11;
    case CPU_MODE_UND: return 13;
    default:           return -1; /* USR, SYS, FIQ handled elsewhere */
    }
}

/* Initialize CPU to power-on state (ARM state, SVC mode, IRQs/FIQs disabled) */
void cpu_init(ARM7TDMI* cpu) {
    memset(cpu->regs, 0, sizeof(cpu->regs));
    memset(cpu->banked, 0, sizeof(cpu->banked));
    memset(cpu->spsr, 0, sizeof(cpu->spsr));

    /* Power-on state: SVC mode, IRQs and FIQs disabled, ARM state */
    cpu->cpsr = CPU_MODE_SVC | (1u << CPSR_I) | (1u << CPSR_F);

    /* PC starts at BIOS entry point */
    cpu->regs[REG_PC] = 0x00000000;

    /* SVC stack pointer (default, overwritten by BIOS or cpu_skip_bios) */
    cpu->regs[REG_SP] = 0x03007FE0;

    cpu->pipeline_valid = false;
    cpu->halted = false;
    cpu->cycles_executed = 0;
}

/* Check ARM condition code against current CPSR flags */
bool cpu_condition_passed(ARM7TDMI* cpu, uint32_t cond) {
    bool n = BIT(cpu->cpsr, CPSR_N);
    bool z = BIT(cpu->cpsr, CPSR_Z);
    bool c = BIT(cpu->cpsr, CPSR_C);
    bool v = BIT(cpu->cpsr, CPSR_V);

    switch (cond) {
    case 0x0: return z;              /* EQ: Z set */
    case 0x1: return !z;             /* NE: Z clear */
    case 0x2: return c;              /* CS/HS: C set */
    case 0x3: return !c;             /* CC/LO: C clear */
    case 0x4: return n;              /* MI: N set */
    case 0x5: return !n;             /* PL: N clear */
    case 0x6: return v;              /* VS: V set */
    case 0x7: return !v;             /* VC: V clear */
    case 0x8: return c && !z;        /* HI: C set and Z clear */
    case 0x9: return !c || z;        /* LS: C clear or Z set */
    case 0xA: return n == v;         /* GE: N == V */
    case 0xB: return n != v;         /* LT: N != V */
    case 0xC: return !z && (n == v); /* GT: Z clear and N == V */
    case 0xD: return z || (n != v);  /* LE: Z set or N != V */
    case 0xE: return true;           /* AL: always */
    case 0xF: return true;           /* NV: unconditional in ARMv4+ */
    default:  return false;
    }
}

/* Switch CPU mode with full register banking.
 *
 * Saves the outgoing mode's banked registers and restores the incoming
 * mode's banked registers.  FIQ banks R8-R14; IRQ/SVC/ABT/UND bank
 * only R13-R14; USR/SYS share the base register set with no banking. */
void cpu_switch_mode(ARM7TDMI* cpu, CPUMode new_mode) {
    CPUMode old_mode = (CPUMode)(cpu->cpsr & 0x1F);

    if (old_mode == new_mode) {
        return;
    }

    /* ---- Save outgoing mode's banked registers ---- */
    if (old_mode == CPU_MODE_FIQ) {
        /* FIQ banks R8-R14.  Save FIQ copies, restore USR R8-R12. */
        for (int i = 0; i < 5; i++) {
            cpu->banked[i] = cpu->regs[8 + i];       /* FIQ R8-R12 -> banked[0..4] */
        }
        cpu->banked[5] = cpu->regs[REG_SP];          /* FIQ SP -> banked[5] */
        cpu->banked[6] = cpu->regs[REG_LR];          /* FIQ LR -> banked[6] */

        /* Restore USR R8-R12 from banked[15..19] */
        for (int i = 0; i < 5; i++) {
            cpu->regs[8 + i] = cpu->banked[15 + i];
        }
    } else {
        int offset = bank_offset_for_mode(old_mode);
        if (offset >= 0) {
            /* IRQ/SVC/ABT/UND: save SP and LR */
            cpu->banked[offset]     = cpu->regs[REG_SP];
            cpu->banked[offset + 1] = cpu->regs[REG_LR];
        } else {
            /* USR/SYS: save SP and LR to dedicated bank slots */
            cpu->banked[20] = cpu->regs[REG_SP];
            cpu->banked[21] = cpu->regs[REG_LR];
        }
    }

    /* ---- Load incoming mode's banked registers ---- */
    if (new_mode == CPU_MODE_FIQ) {
        /* Save USR R8-R12 away, load FIQ R8-R14 */
        for (int i = 0; i < 5; i++) {
            cpu->banked[15 + i] = cpu->regs[8 + i];  /* USR R8-R12 -> banked[15..19] */
        }
        for (int i = 0; i < 5; i++) {
            cpu->regs[8 + i] = cpu->banked[i];        /* banked[0..4] -> FIQ R8-R12 */
        }
        cpu->regs[REG_SP] = cpu->banked[5];           /* banked[5] -> FIQ SP */
        cpu->regs[REG_LR] = cpu->banked[6];           /* banked[6] -> FIQ LR */
    } else {
        int offset = bank_offset_for_mode(new_mode);
        if (offset >= 0) {
            /* IRQ/SVC/ABT/UND: restore SP and LR */
            cpu->regs[REG_SP] = cpu->banked[offset];
            cpu->regs[REG_LR] = cpu->banked[offset + 1];
        } else {
            /* USR/SYS: restore SP and LR from dedicated bank slots */
            cpu->regs[REG_SP] = cpu->banked[20];
            cpu->regs[REG_LR] = cpu->banked[21];
        }
    }

    /* Write the new mode bits into CPSR */
    cpu->cpsr = (cpu->cpsr & ~0x1Fu) | ((uint32_t)new_mode & 0x1Fu);
}

/* Return pointer to the current mode's SPSR, or NULL for USR/SYS */
uint32_t* cpu_get_spsr(ARM7TDMI* cpu) {
    int idx = spsr_index_for_mode(cpu_get_mode(cpu));
    if (idx < 0) {
        return NULL;
    }
    return &cpu->spsr[idx];
}

/* Invalidate the instruction pipeline (must refill before next execute) */
void cpu_flush_pipeline(ARM7TDMI* cpu) {
    cpu->pipeline_valid = false;
}

/* Check whether an IRQ should fire: CPSR.I must be clear and the
 * interrupt controller must report a pending interrupt (IME && IE & IF). */
bool cpu_check_irq(ARM7TDMI* cpu) {
    if (BIT(cpu->cpsr, CPSR_I)) {
        return false;
    }
    InterruptController* ic = cpu->bus->interrupts;
    return ic && interrupt_pending(ic);
}

/* Enter IRQ exception: save state, switch to IRQ mode, jump to vector.
 *
 * Called between cpu_step calls.  Sets LR_irq = PC so the BIOS handler
 * can return with SUBS PC, LR, #4. */
void cpu_handle_irq(ARM7TDMI* cpu) {
    uint32_t old_cpsr = cpu->cpsr;

    /* Switch to IRQ mode (banks SP/LR) */
    cpu_switch_mode(cpu, CPU_MODE_IRQ);

    /* Save old CPSR into SPSR_irq (index 3).
     * Must happen AFTER mode switch so SPSR_irq slot is accessible. */
    cpu->spsr[3] = old_cpsr;

    /* LR_irq = current PC.  The BIOS IRQ handler does SUBS PC, LR, #4
     * to return to the correct instruction. */
    cpu->regs[REG_LR] = cpu->regs[REG_PC];

    /* Disable IRQs to prevent re-entry */
    cpu->cpsr |= (1u << CPSR_I);

    /* Force ARM state */
    cpu->cpsr &= ~(1u << CPSR_T);

    /* Jump to IRQ vector */
    cpu->regs[REG_PC] = 0x00000018;

    cpu_flush_pipeline(cpu);
}

/* Enter SWI (Software Interrupt) exception.
 *
 * Called during instruction execution.  LR_svc = address of instruction
 * after the SWI.  Since PC = executing_instr + 8 (ARM) or +4 (Thumb),
 * LR = PC - 4 (ARM) or PC - 2 (Thumb). */
void cpu_handle_swi(ARM7TDMI* cpu) {
    uint32_t old_cpsr = cpu->cpsr;
    bool thumb = BIT(cpu->cpsr, CPSR_T);

    /* Switch to SVC mode (banks SP/LR) */
    cpu_switch_mode(cpu, CPU_MODE_SVC);

    /* Save old CPSR into SPSR_svc (index 1) */
    cpu->spsr[1] = old_cpsr;

    /* LR = next instruction after SWI */
    if (thumb) {
        cpu->regs[REG_LR] = cpu->regs[REG_PC] - 2;
    } else {
        cpu->regs[REG_LR] = cpu->regs[REG_PC] - 4;
    }

    /* Disable IRQs */
    cpu->cpsr |= (1u << CPSR_I);

    /* Force ARM state */
    cpu->cpsr &= ~(1u << CPSR_T);

    /* Jump to SWI vector */
    cpu->regs[REG_PC] = 0x00000008;

    cpu_flush_pipeline(cpu);
}

/* Set CPU to the state the BIOS would leave it in.
 * Used when no BIOS ROM is loaded so execution starts directly in ROM. */
void cpu_skip_bios(ARM7TDMI* cpu) {
    /* System mode, ARM state, IRQs enabled */
    cpu->cpsr = CPU_MODE_SYS;

    /* Set up stack pointers for each privileged mode */
    cpu_switch_mode(cpu, CPU_MODE_IRQ);
    cpu->regs[REG_SP] = 0x03007FA0;

    cpu_switch_mode(cpu, CPU_MODE_SVC);
    cpu->regs[REG_SP] = 0x03007FE0;

    cpu_switch_mode(cpu, CPU_MODE_SYS);
    cpu->regs[REG_SP] = 0x03007F00;

    /* Jump to ROM entry point */
    cpu->regs[REG_PC] = 0x08000000;

    cpu_flush_pipeline(cpu);
}

/* Execute one instruction through the 2-stage pipeline.
 *
 * Pipeline model:
 *   pipeline[0] = instruction about to execute
 *   pipeline[1] = next prefetched instruction
 *   PC          = address of next fetch (executing_addr + 8 for ARM, +4 for Thumb)
 *
 * When the pipeline is invalid (after flush), two fetches refill it before
 * any instruction executes. */
int cpu_step(ARM7TDMI* cpu) {
    if (!cpu->pipeline_valid) {
        /* Refill the 2-entry pipeline */
        if (BIT(cpu->cpsr, CPSR_T)) {
            /* Thumb: two 16-bit fetches */
            cpu->pipeline[0] = bus_read16(cpu->bus, cpu->regs[REG_PC]);
            cpu->pipeline[1] = bus_read16(cpu->bus, cpu->regs[REG_PC] + 2);
            cpu->regs[REG_PC] += 4;
        } else {
            /* ARM: two 32-bit fetches */
            cpu->pipeline[0] = bus_read32(cpu->bus, cpu->regs[REG_PC]);
            cpu->pipeline[1] = bus_read32(cpu->bus, cpu->regs[REG_PC] + 4);
            cpu->regs[REG_PC] += 8;
        }
        cpu->pipeline_valid = true;
        return 2; /* pipeline refill cost */
    }

    if (BIT(cpu->cpsr, CPSR_T)) {
        /* Thumb mode: execute pipeline[0], shift, fetch next */
        uint16_t instr = (uint16_t)cpu->pipeline[0];
        cpu->pipeline[0] = cpu->pipeline[1];
        cpu->pipeline[1] = bus_read16(cpu->bus, cpu->regs[REG_PC]);
        cpu->regs[REG_PC] += 2;
        return thumb_execute(cpu, instr);
    } else {
        /* ARM mode: execute pipeline[0], shift, fetch next */
        uint32_t instr = cpu->pipeline[0];
        cpu->pipeline[0] = cpu->pipeline[1];
        cpu->pipeline[1] = bus_read32(cpu->bus, cpu->regs[REG_PC]);
        cpu->regs[REG_PC] += 4;

        uint32_t cond = (instr >> 28) & 0xF;
        if (cpu_condition_passed(cpu, cond)) {
            return arm_execute(cpu, instr);
        }
        return 1; /* skipped conditional: 1S cycle */
    }
}

/* Run the CPU for at least 'cycles' cycles.
 * Checks for pending IRQs between each instruction step.
 * If halted, fast-forwards cycles unless an IRQ wakes the CPU. */
void cpu_run(ARM7TDMI* cpu, int cycles) {
    cpu->cycles_executed = 0;

    while (cpu->cycles_executed < cycles) {
        if (cpu->halted) {
            if (cpu_check_irq(cpu)) {
                cpu->halted = false;
                cpu_handle_irq(cpu);
            } else {
                /* Stay halted: consume all remaining cycles */
                cpu->cycles_executed = cycles;
                break;
            }
        }

        /* Check for pending IRQ before each instruction */
        if (cpu_check_irq(cpu)) {
            cpu_handle_irq(cpu);
        }

        int step_cycles = cpu_step(cpu);
        cpu->cycles_executed += step_cycles;
    }
}
