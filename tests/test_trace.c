#include "test_harness.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/arm7tdmi.h"
#include "trace/trace.h"

static void zero_cpu(ARM7TDMI* cpu) {
    memset(cpu, 0, sizeof(*cpu));
}

static size_t count_lines(const char* path) {
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;
    size_t lines = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') lines++;
    }
    fclose(fp);
    return lines;
}

TEST(trace_init_opens_file_and_sets_flag) {
    const char* path = "test_trace_init.log";
    remove(path);
    ASSERT_TRUE(trace_init(path, 0, 0, 0));
    ASSERT_TRUE(g_trace_enabled);
    trace_shutdown();
    ASSERT_TRUE(!g_trace_enabled);

    /* File should exist (even if empty) after init. */
    FILE* fp = fopen(path, "r");
    ASSERT_TRUE(fp != NULL);
    fclose(fp);
    remove(path);
}

TEST(trace_log_writes_arm_format) {
    const char* path = "test_trace_arm.log";
    remove(path);
    ASSERT_TRUE(trace_init(path, 0, 0, 0));

    ARM7TDMI cpu;
    zero_cpu(&cpu);
    cpu.regs[0] = 0xDEADBEEF;
    cpu.regs[15] = 0x08000130;
    cpu.cpsr = 0x600000DF;
    trace_log(&cpu, 0x08000128, 0xE3A00012, false);
    trace_shutdown();

    FILE* fp = fopen(path, "r");
    ASSERT_TRUE(fp != NULL);
    char line[1024] = {0};
    char* got = fgets(line, sizeof(line), fp);
    fclose(fp);
    ASSERT_TRUE(got != NULL);

    /* Spot-check key fields appear in the expected positions. */
    ASSERT_TRUE(strstr(line, "08000128 ARM   E3A00012") != NULL);
    ASSERT_TRUE(strstr(line, "R00=DEADBEEF") != NULL);
    ASSERT_TRUE(strstr(line, "R15=08000130") != NULL);
    ASSERT_TRUE(strstr(line, "CPSR=600000DF") != NULL);
    remove(path);
}

TEST(trace_log_writes_thumb_format) {
    const char* path = "test_trace_thumb.log";
    remove(path);
    ASSERT_TRUE(trace_init(path, 0, 0, 0));

    ARM7TDMI cpu;
    zero_cpu(&cpu);
    trace_log(&cpu, 0x08000200, 0x4770, true);  /* BX LR */
    trace_shutdown();

    FILE* fp = fopen(path, "r");
    ASSERT_TRUE(fp != NULL);
    char line[1024] = {0};
    fgets(line, sizeof(line), fp);
    fclose(fp);
    ASSERT_TRUE(strstr(line, "08000200 THUMB     4770") != NULL);
    remove(path);
}

TEST(trace_pc_range_filter) {
    const char* path = "test_trace_range.log";
    remove(path);
    ASSERT_TRUE(trace_init(path, 0x08000100, 0x08000200, 0));

    ARM7TDMI cpu;
    zero_cpu(&cpu);
    trace_log(&cpu, 0x080000FF, 0, false);  /* below — skip */
    trace_log(&cpu, 0x08000100, 0, false);  /* lower bound — keep */
    trace_log(&cpu, 0x08000180, 0, false);  /* inside — keep */
    trace_log(&cpu, 0x08000200, 0, false);  /* upper bound — keep */
    trace_log(&cpu, 0x08000201, 0, false);  /* above — skip */
    trace_shutdown();

    ASSERT_EQ(count_lines(path), (size_t)3);
    remove(path);
}

TEST(trace_frame_budget_auto_disables) {
    const char* path = "test_trace_frames.log";
    remove(path);
    ASSERT_TRUE(trace_init(path, 0, 0, 2));
    ASSERT_TRUE(g_trace_enabled);

    trace_frame_tick();
    ASSERT_TRUE(g_trace_enabled);  /* 1 frame seen, still enabled */
    trace_frame_tick();
    ASSERT_TRUE(!g_trace_enabled); /* 2 frames seen, disabled */

    /* Subsequent log calls should be no-ops. */
    ARM7TDMI cpu;
    zero_cpu(&cpu);
    trace_log(&cpu, 0, 0, false);
    ASSERT_EQ(count_lines(path), (size_t)0);
    remove(path);
}

TEST(trace_init_rejects_null_path) {
    ASSERT_TRUE(!trace_init(NULL, 0, 0, 0));
    ASSERT_TRUE(!g_trace_enabled);
}

TEST(trace_log_when_disabled_is_noop) {
    const char* path = "test_trace_disabled.log";
    remove(path);
    ASSERT_TRUE(!g_trace_enabled);
    ARM7TDMI cpu;
    zero_cpu(&cpu);
    trace_log(&cpu, 0, 0, false);  /* must not crash, must not create file */
    FILE* fp = fopen(path, "r");
    ASSERT_TRUE(fp == NULL);
}

void run_trace_tests(void) {
    TEST_SUITE("trace");
    RUN_TEST(trace_init_opens_file_and_sets_flag);
    RUN_TEST(trace_log_writes_arm_format);
    RUN_TEST(trace_log_writes_thumb_format);
    RUN_TEST(trace_pc_range_filter);
    RUN_TEST(trace_frame_budget_auto_disables);
    RUN_TEST(trace_init_rejects_null_path);
    RUN_TEST(trace_log_when_disabled_is_noop);
}
