#ifndef TRACE_H
#define TRACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu/arm7tdmi.h"

/* Per-instruction execution trace.
 *
 * Writes one line per executed instruction to a text file:
 *   <PC> <MODE> <OPCODE>  R00=... R01=... ... R15=... CPSR=...
 *
 * Designed to be diff-able against another emulator's trace (after format
 * normalization) to find the first instruction where execution diverges.
 *
 * Activation: pass --trace <file> to the emulator. PC range and frame-count
 * bounds are essential because an unbounded trace fills gigabytes per second.
 *
 * Performance: one branch per instruction when off (g_trace_enabled check).
 * When on, expect ~5-10x slowdown from the synchronous file write.
 */

extern bool g_trace_enabled;

/* Open the trace file and configure bounds.
 *   pc_from / pc_to : inclusive PC range (0 / 0xFFFFFFFF = unbounded)
 *   max_frames      : stop tracing after N frames (0 = unbounded)
 * Returns true on success. */
bool trace_init(const char* path, uint32_t pc_from, uint32_t pc_to, uint32_t max_frames);

/* Flush and close the trace file. Safe to call when not initialized. */
void trace_shutdown(void);

/* Write one trace line. Internally checks PC bounds and frame budget. */
void trace_log(const ARM7TDMI* cpu, uint32_t executing_pc, uint32_t opcode, bool thumb);

/* Bump the frame counter (called once per emulated frame from main loop).
 * When the count reaches max_frames, tracing auto-disables. */
void trace_frame_tick(void);

/* Zero-overhead-when-off macro used by cpu_step. */
#define TRACE_LOG(cpu, pc, op, thumb) do { \
    if (g_trace_enabled) trace_log((cpu), (pc), (op), (thumb)); \
} while (0)

#endif // TRACE_H
