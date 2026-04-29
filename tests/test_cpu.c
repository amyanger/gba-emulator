#include "test_harness.h"
#include "gba.h"
#include "cpu/arm_instr.h"
#include "cpu/bios_hle.h"

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

/* ---- Flag-setting instructions in FIQ mode -------------------------
 *
 * Regression: when tracing jsmolka arm.gba we observed CMP R8, #0x20 in
 * FIQ mode that appeared not to update flags. This pins down the basic
 * "data-processing flag updates work in non-USR/SYS modes" contract so
 * future divergence shows up here, not just in opaque ROM hangs. */

TEST(cmp_in_fiq_mode_updates_flags) {
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;

    /* Switch into FIQ mode so R8 reads from the FIQ bank. */
    cpu_switch_mode(cpu, CPU_MODE_FIQ);
    cpu->regs[8] = 0x40;

    /* Pre-set N flag from a prior compare; CMP R8, #0x20 should clear it. */
    cpu->cpsr |= (1u << CPSR_N);
    cpu->cpsr &= ~(1u << CPSR_C);

    /* E3580020 = CMP R8, #0x20 — flag-setting subtract. */
    arm_execute(cpu, 0xE3580020);

    /* 0x40 - 0x20 = 0x20: positive, nonzero, no borrow, no overflow. */
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_N), 0);
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_Z), 0);
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_C), 1);
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_V), 0);

    /* And FIQ mode bits must still be intact — flag updates must not
     * accidentally rewrite the mode bits. */
    ASSERT_EQ_HEX(cpu_get_mode(cpu), CPU_MODE_FIQ);
    free(gba);
}

TEST(soft_reset_default_jumps_to_rom) {
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;
    /* Boot flag at 0x03007FFA defaults to 0 → ROM (0x08000000). */
    gba->bus.iwram[0x7FFA] = 0;
    /* Dirty IWRAM scratch and a stack pointer to ensure SoftReset clears
     * them. */
    gba->bus.iwram[0x7E00] = 0xAB;
    gba->bus.iwram[0x7FFF] = 0xCD;

    bios_hle_execute(cpu, 0x00);

    ASSERT_EQ_HEX(cpu->regs[REG_PC], 0x08000000);
    ASSERT_EQ_HEX(cpu_get_mode(cpu), CPU_MODE_SYS);
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_F), 1); /* FIQ disabled */
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_I), 0); /* IRQ enabled */
    ASSERT_EQ(gba->bus.iwram[0x7E00], 0); /* scratch cleared */
    ASSERT_EQ(gba->bus.iwram[0x7FFF], 0);
    ASSERT_EQ_HEX(cpu->regs[REG_SP], 0x03007F00); /* SYS SP */
    free(gba);
}

TEST(soft_reset_flag_set_jumps_to_ewram) {
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;
    gba->bus.iwram[0x7FFA] = 1;

    bios_hle_execute(cpu, 0x00);

    ASSERT_EQ_HEX(cpu->regs[REG_PC], 0x02000000);
    free(gba);
}

TEST(cmp_in_user_mode_updates_flags) {
    /* Sanity — same operation in USR/SYS mode for comparison. */
    GBA* gba = make_gba();
    ARM7TDMI* cpu = &gba->cpu;
    cpu_switch_mode(cpu, CPU_MODE_SYS);
    cpu->regs[8] = 0x40;
    cpu->cpsr |= (1u << CPSR_N);

    arm_execute(cpu, 0xE3580020);

    ASSERT_EQ(BIT(cpu->cpsr, CPSR_N), 0);
    ASSERT_EQ(BIT(cpu->cpsr, CPSR_C), 1);
    free(gba);
}

void run_cpu_tests(void) {
    TEST_SUITE("cpu");
    RUN_TEST(cpu_init_state);
    RUN_TEST(cpu_pipeline_init);
    RUN_TEST(cpu_regs_zero_init);
    RUN_TEST(cmp_in_fiq_mode_updates_flags);
    RUN_TEST(cmp_in_user_mode_updates_flags);
    RUN_TEST(soft_reset_default_jumps_to_rom);
    RUN_TEST(soft_reset_flag_set_jumps_to_ewram);
}
