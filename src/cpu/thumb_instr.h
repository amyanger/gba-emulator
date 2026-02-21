#ifndef THUMB_INSTR_H
#define THUMB_INSTR_H

#include "arm7tdmi.h"

// Execute a Thumb (16-bit) instruction, returns cycles consumed
int thumb_execute(ARM7TDMI* cpu, uint16_t instr);

#endif // THUMB_INSTR_H
