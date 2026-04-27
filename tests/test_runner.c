#include "test_harness.h"
#include <stdio.h>

int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;
int _test_failed_flag = 0;

void run_savestate_tests(void);
void run_bus_tests(void);
void run_cpu_tests(void);
void run_rtc_tests(void);
void run_rewind_tests(void);
void run_screenshot_tests(void);
void run_trace_tests(void);

int main(void) {
    printf("=== GBA Emulator Test Suite ===\n\n");
    run_savestate_tests();
    run_bus_tests();
    run_cpu_tests();
    run_rtc_tests();
    run_rewind_tests();
    run_screenshot_tests();
    run_trace_tests();
    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
