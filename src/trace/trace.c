#include "trace.h"

#include <stdio.h>
#include <stdlib.h>

#include "common.h"

bool g_trace_enabled = false;

static struct {
    FILE* fp;
    uint32_t pc_from;
    uint32_t pc_to;
    uint32_t max_frames;
    uint32_t frames_seen;
} s_trace = {0};

bool trace_init(const char* path, uint32_t pc_from, uint32_t pc_to, uint32_t max_frames) {
    if (!path) return false;
    if (s_trace.fp) trace_shutdown();

    s_trace.fp = fopen(path, "w");
    if (!s_trace.fp) {
        LOG_ERROR("trace: failed to open %s", path);
        return false;
    }

    s_trace.pc_from = pc_from;
    s_trace.pc_to = pc_to ? pc_to : 0xFFFFFFFFu;
    s_trace.max_frames = max_frames;
    s_trace.frames_seen = 0;
    g_trace_enabled = true;
    LOG_INFO("trace: writing to %s (PC %08X..%08X, %u frame budget)",
             path, s_trace.pc_from, s_trace.pc_to, max_frames);
    return true;
}

void trace_shutdown(void) {
    g_trace_enabled = false;
    if (s_trace.fp) {
        fclose(s_trace.fp);
        s_trace.fp = NULL;
    }
}

void trace_log(const ARM7TDMI* cpu, uint32_t executing_pc, uint32_t opcode, bool thumb) {
    if (!s_trace.fp) return;
    if (executing_pc < s_trace.pc_from || executing_pc > s_trace.pc_to) return;

    /* Format: <PC> <MODE> <OPCODE>  R00=... R01=... ... R15=... CPSR=...
     * Fixed-width fields keep the file diff-friendly across runs. */
    if (thumb) {
        fprintf(s_trace.fp, "%08X THUMB     %04X  ", executing_pc, opcode & 0xFFFF);
    } else {
        fprintf(s_trace.fp, "%08X ARM   %08X  ", executing_pc, opcode);
    }
    for (int i = 0; i < 16; i++) {
        fprintf(s_trace.fp, "R%02d=%08X ", i, cpu->regs[i]);
    }
    fprintf(s_trace.fp, "CPSR=%08X\n", cpu->cpsr);
}

void trace_frame_tick(void) {
    if (!g_trace_enabled || s_trace.max_frames == 0) return;
    s_trace.frames_seen++;
    if (s_trace.frames_seen >= s_trace.max_frames) {
        LOG_INFO("trace: frame budget reached (%u), closing file", s_trace.max_frames);
        trace_shutdown();
    }
}
