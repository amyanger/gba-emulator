#include "test_harness.h"
#include <stdio.h>

int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;
int _test_failed_flag = 0;

void run_savestate_tests(void);
void run_bus_tests(void);
void run_cpu_tests(void);
void run_dma_tests(void);
void run_interrupt_tests(void);
void run_timer_tests(void);
void run_flash_tests(void);
void run_cheat_tests(void);
void run_input_tests(void);
void run_rewind_tests(void);
void run_screenshot_tests(void);
void run_trace_tests(void);
void run_cartridge_autosave_tests(void);
void run_keymap_tests(void);
void run_eeprom_tests(void);
void run_practice_tests(void);
void run_fb_hash_tests(void);
#ifndef _WIN32
// These suites depend on POSIX APIs (unistd.h, pthreads, AF_UNIX socketpair)
// and are not compiled into the Windows test binary.
void run_rtc_tests(void);
void run_sio_tests(void);
void run_sio_link_tests(void);
#endif

#define RUN_SUITE(fn) do { printf(">> " #fn "\n"); fflush(stdout); fn(); } while (0)

int main(void) {
    printf("=== GBA Emulator Test Suite ===\n\n");
    RUN_SUITE(run_savestate_tests);
    RUN_SUITE(run_bus_tests);
    RUN_SUITE(run_cpu_tests);
    RUN_SUITE(run_dma_tests);
    RUN_SUITE(run_interrupt_tests);
    RUN_SUITE(run_timer_tests);
    RUN_SUITE(run_flash_tests);
    RUN_SUITE(run_cheat_tests);
    RUN_SUITE(run_input_tests);
    RUN_SUITE(run_rewind_tests);
    RUN_SUITE(run_screenshot_tests);
    RUN_SUITE(run_trace_tests);
    RUN_SUITE(run_cartridge_autosave_tests);
    RUN_SUITE(run_keymap_tests);
    RUN_SUITE(run_eeprom_tests);
    RUN_SUITE(run_practice_tests);
    RUN_SUITE(run_fb_hash_tests);
#ifndef _WIN32
    RUN_SUITE(run_rtc_tests);
    RUN_SUITE(run_sio_tests);
    RUN_SUITE(run_sio_link_tests);
#endif
    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           tests_passed, tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
