#include "test_harness.h"
#include "gba.h"
#include "cpu/arm7tdmi.h"
#include <stdlib.h>

static int s_fired;
static void on_hook(const GBA* gba, void* ctx) {
    (void)gba;
    (*(int*)ctx)++;
}

static void put16(GBA* g, uint32_t addr, uint16_t v) {
    g->bus.iwram[addr - 0x03000000u] = (uint8_t)v;
    g->bus.iwram[addr - 0x03000000u + 1] = (uint8_t)(v >> 8);
}

/* movs r0,#1 ; movs r0,#2 (hook) ; b . */
static GBA* make_program(void) {
    GBA* g = calloc(1, sizeof(GBA));
    gba_init(g);
    put16(g, 0x03000100u, 0x2001);
    put16(g, 0x03000102u, 0x2002);
    put16(g, 0x03000104u, 0xE7FE);
    g->cpu.cpsr |= 1u << CPSR_T;
    g->cpu.regs[REG_PC] = 0x03000100u;
    cpu_flush_pipeline(&g->cpu);
    return g;
}

TEST(pc_hook_fires_once_per_execution) {
    GBA* g = make_program();
    s_fired = 0;
    gba_set_pc_hook(g, 0x03000102u, on_hook, &s_fired);
    cpu_run(&g->cpu, 200);
    ASSERT_EQ(s_fired, 1);
    ASSERT_EQ(g->cpu.regs[0], 2u);   /* the hooked instruction still ran */
    free(g);
}

TEST(pc_hook_removed_never_fires) {
    GBA* g = make_program();
    s_fired = 0;
    gba_set_pc_hook(g, 0x03000102u, on_hook, &s_fired);
    gba_set_pc_hook(g, 0, NULL, NULL);
    cpu_run(&g->cpu, 200);
    ASSERT_EQ(s_fired, 0);
    free(g);
}

void run_pc_hook_tests(void) {
    TEST_SUITE("pc_hook");
    RUN_TEST(pc_hook_fires_once_per_execution);
    RUN_TEST(pc_hook_removed_never_fires);
}
