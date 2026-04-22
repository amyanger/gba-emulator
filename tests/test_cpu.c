#include "test_harness.h"
#include "gba.h"

/* Helper: create a fully wired GBA on the heap and return it. */
static GBA* make_gba(void) {
    GBA* gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    return gba;
}

/* ---- Initial CPU state after gba_init ----------------------------- */

TEST(cpu_init_state) {
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;

    /* cpu_init sets SVC mode with IRQs and FIQs disabled, ARM state */
    CPUMode mode = cpu_get_mode(cpu);
    ASSERT_EQ_HEX(mode, CPU_MODE_SVC);

    /* CPSR.I (bit 7) = 1 — IRQs disabled */
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_I), 1);

    /* CPSR.F (bit 6) = 1 — FIQs disabled */
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_F), 1);

    /* CPSR.T (bit 5) = 0 — ARM state, not Thumb */
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_T), 0);

    /* CPU is not halted */
    ASSERT_TRUE(!cpu->halted);
}

/* ---- Pipeline state after init ------------------------------------ */

TEST(cpu_pipeline_init) {
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;

    /* cpu_init sets pipeline_valid = false (pipeline needs filling) */
    ASSERT_TRUE(!cpu->pipeline_valid);
}

/* ---- General-purpose registers R0-R12 are zeroed ------------------ */

TEST(cpu_regs_zero_init) {
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;

    for (int i = 0; i <= 12; i++) {
        ASSERT_EQ_HEX(cpu->regs[i], 0x00000000);
    }
}

void run_cpu_tests(void) {
    TEST_SUITE("cpu");
    RUN_TEST(cpu_init_state);
    RUN_TEST(cpu_pipeline_init);
    RUN_TEST(cpu_regs_zero_init);
}
