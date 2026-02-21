#include "common.h"
#include "cpu/arm7tdmi.h"

#ifdef DEBUG

void debug_dump_registers(ARM7TDMI* cpu) {
    printf("=== CPU Register Dump ===\n");
    for (int i = 0; i < 16; i++) {
        printf("  R%-2d = 0x%08X", i, cpu->regs[i]);
        if (i % 4 == 3) printf("\n");
    }
    printf("  CPSR = 0x%08X [%c%c%c%c %c%c%c Mode:0x%02X]\n", cpu->cpsr,
           BIT(cpu->cpsr, CPSR_N) ? 'N' : '-', BIT(cpu->cpsr, CPSR_Z) ? 'Z' : '-',
           BIT(cpu->cpsr, CPSR_C) ? 'C' : '-', BIT(cpu->cpsr, CPSR_V) ? 'V' : '-',
           BIT(cpu->cpsr, CPSR_I) ? 'I' : '-', BIT(cpu->cpsr, CPSR_F) ? 'F' : '-',
           BIT(cpu->cpsr, CPSR_T) ? 'T' : '-', cpu->cpsr & 0x1F);
    printf("  Halted: %s\n", cpu->halted ? "yes" : "no");
}

void debug_log_instruction(ARM7TDMI* cpu, uint32_t addr, uint32_t instr, bool is_thumb) {
    if (is_thumb) {
        fprintf(stderr, "[TRACE] 0x%08X: 0x%04X\n", addr, instr & 0xFFFF);
    } else {
        fprintf(stderr, "[TRACE] 0x%08X: 0x%08X\n", addr, instr);
    }
}

#endif
