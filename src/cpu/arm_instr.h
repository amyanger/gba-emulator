#ifndef ARM_INSTR_H
#define ARM_INSTR_H

#include "arm7tdmi.h"

// Execute an ARM (32-bit) instruction, returns cycles consumed
int arm_execute(ARM7TDMI* cpu, uint32_t instr);

#endif // ARM_INSTR_H
