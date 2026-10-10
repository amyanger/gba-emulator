#include "test_harness.h"
#include "gba.h"

typedef struct {
    int calls;
    uint16_t vcount;
} ObserverLog;

static void record(const GBA* gba, void* ctx) {
    ObserverLog* log = (ObserverLog*)ctx;
    log->calls++;
    log->vcount = gba->ppu.vcount;
}

TEST(observer_runs_once_per_frame_at_line_160) {
    static GBA gba;   /* large; keep off the stack */
    ObserverLog log = {0};
    gba_init(&gba);
    cpu_skip_bios(&gba.cpu);
    gba_set_vblank_observer(&gba, record, &log);
    gba_run_frame(&gba);
    ASSERT_EQ(log.calls, 1);
    ASSERT_EQ(log.vcount, VDRAW_LINES);
    gba_run_frame(&gba);
    ASSERT_EQ(log.calls, 2);
    gba_destroy(&gba);
}

TEST(observer_null_by_default_and_clearable) {
    static GBA gba;
    ObserverLog log = {0};
    gba_init(&gba);
    cpu_skip_bios(&gba.cpu);
    ASSERT_TRUE(gba.vblank_observer == NULL);
    gba_run_frame(&gba);   /* must not crash with no observer */
    gba_set_vblank_observer(&gba, record, &log);
    gba_set_vblank_observer(&gba, NULL, NULL);
    gba_run_frame(&gba);
    ASSERT_EQ(log.calls, 0);
    gba_destroy(&gba);
}

void run_vblank_observer_tests(void) {
    RUN_TEST(observer_runs_once_per_frame_at_line_160);
    RUN_TEST(observer_null_by_default_and_clearable);
}
