#ifndef CHEAT_REPORT_H
#define CHEAT_REPORT_H

#include <stdio.h>
#include "cheat/cheat.h"

/* Human-readable dump for --cheat-report: each cheat's device, whether it
 * runs, why not, and its decoded operations. */
const char* cheat_device_name(CheatDevice dev);
void cheat_report_print(const CheatEngine* engine, FILE* out);

#endif // CHEAT_REPORT_H
