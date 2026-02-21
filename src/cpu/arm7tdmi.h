#ifndef ARM7TDMI_H
#define ARM7TDMI_H

#include "common.h"

/* Forward declaration */
typedef struct Bus Bus;

/* CPU modes (low 5 bits of CPSR) */
typedef enum {
    CPU_MODE_USR = 0x10,
    CPU_MODE_FIQ = 0x11,
    CPU_MODE_IRQ = 0x12,
    CPU_MODE_SVC = 0x13,
    CPU_MODE_ABT = 0x17,
    CPU_MODE_UND = 0x1B,
    CPU_MODE_SYS = 0x1F
} CPUMode;

/* CPSR flag bit positions */
#define CPSR_N 31
#define CPSR_Z 30
#define CPSR_C 29
#define CPSR_V 28
#define CPSR_I 7
#define CPSR_F 6
#define CPSR_T 5

/* Register aliases */
#define REG_SP 13
#define REG_LR 14
#define REG_PC 15

struct ARM7TDMI {
    /* 16 visible registers (R0-R15) */
    uint32_t regs[16];

    /* Current Program Status Register */
    uint32_t cpsr;

    /* Saved PSR for each privileged mode */
    /* Index: 0=FIQ, 1=SVC, 2=ABT, 3=IRQ, 4=UND */
    uint32_t spsr[5];

    /* Banked registers (flat array)
     * 0-4:   FIQ R8-R12
     * 5-6:   FIQ R13-R14
     * 7-8:   SVC R13-R14
     * 9-10:  ABT R13-R14
     * 11-12: IRQ R13-R14
     * 13-14: UND R13-R14
     * 15-19: USR R8-R12 (saved when entering FIQ)
     * 20-21: USR/SYS R13-R14 (saved when entering privileged mode)
     */
    uint32_t banked[22];

    /* Pipeline (2-entry prefetch buffer) */
    uint32_t pipeline[2];
    bool pipeline_valid;

    /* CPU state */
    bool halted;
    int cycles_executed;

    /* Bus pointer (wired during gba_init) */
    Bus* bus;
};
typedef struct ARM7TDMI ARM7TDMI;

/* Initialize CPU to power-on state */
void cpu_init(ARM7TDMI* cpu);

/* Run CPU for at least 'cycles' cycles */
void cpu_run(ARM7TDMI* cpu, int cycles);

/* Execute a single instruction, returns cycles consumed */
int cpu_step(ARM7TDMI* cpu);

/* Switch CPU mode, banking registers appropriately */
void cpu_switch_mode(ARM7TDMI* cpu, CPUMode new_mode);

/* Invalidate the pipeline (e.g. after branch or mode change with PC write) */
void cpu_flush_pipeline(ARM7TDMI* cpu);

/* Handle SWI (Software Interrupt) exception entry */
void cpu_handle_swi(ARM7TDMI* cpu);

/* Set CPU to post-BIOS state when no BIOS ROM is loaded */
void cpu_skip_bios(ARM7TDMI* cpu);

/* Return pointer to current mode's SPSR, or NULL for USR/SYS */
uint32_t* cpu_get_spsr(ARM7TDMI* cpu);

/* Check if IRQ should fire (CPSR.I clear and interrupt controller has pending) */
bool cpu_check_irq(ARM7TDMI* cpu);

/* Handle IRQ exception entry */
void cpu_handle_irq(ARM7TDMI* cpu);

/* Get current CPU mode from CPSR */
static inline CPUMode cpu_get_mode(ARM7TDMI* cpu) {
    return (CPUMode)(cpu->cpsr & 0x1F);
}

/* Check condition code (top 4 bits of ARM instruction) */
bool cpu_condition_passed(ARM7TDMI* cpu, uint32_t cond);

/* Helper: map CPUMode to SPSR array index. Returns -1 for USR/SYS. */
static inline int spsr_index_for_mode(CPUMode mode) {
    switch (mode) {
    case CPU_MODE_FIQ: return 0;
    case CPU_MODE_SVC: return 1;
    case CPU_MODE_ABT: return 2;
    case CPU_MODE_IRQ: return 3;
    case CPU_MODE_UND: return 4;
    default:           return -1; /* USR/SYS have no SPSR */
    }
}

#endif /* ARM7TDMI_H */
