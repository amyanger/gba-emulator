#ifndef BIOS_HLE_H
#define BIOS_HLE_H

#include "common.h"

typedef struct ARM7TDMI ARM7TDMI;

/* Execute a BIOS SWI function via high-level emulation.
 * Called when no BIOS ROM is loaded. cpu->regs[0-3] hold the arguments,
 * results are written back to cpu->regs[0-3]. */
void bios_hle_execute(ARM7TDMI* cpu, uint32_t swi_num);

#endif /* BIOS_HLE_H */
