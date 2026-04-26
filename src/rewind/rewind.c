#include "rewind/rewind.h"
#include "rewind/rewind_lz4.h"
#include "savestate/savestate.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

/* A typical state is ~400 KB; allow generous headroom. */
#define REWIND_SCRATCH_CAP (1u * 1024u * 1024u)

bool rewind_init(RewindBuffer* rb, uint32_t capacity_frames) {
    if (!rb || capacity_frames == 0) return false;
    memset(rb, 0, sizeof(*rb));

    rb->slots = (RewindFrame*)calloc(capacity_frames, sizeof(RewindFrame));
    if (!rb->slots) {
        LOG_WARN("Rewind: slot table alloc failed (%u entries)", capacity_frames);
        return false;
    }

    rb->scratch_cap = REWIND_SCRATCH_CAP;
    rb->scratch = (uint8_t*)malloc(rb->scratch_cap);
    if (!rb->scratch) {
        LOG_WARN("Rewind: scratch alloc failed");
        free(rb->slots);
        memset(rb, 0, sizeof(*rb));
        return false;
    }

    int worst = LZ4_compressBound((int)REWIND_SCRATCH_CAP);
    rb->lz4_scratch_cap = (worst > 0) ? (uint32_t)worst : REWIND_SCRATCH_CAP;
    rb->lz4_scratch = (uint8_t*)malloc(rb->lz4_scratch_cap);
    if (!rb->lz4_scratch) {
        LOG_WARN("Rewind: LZ4 scratch alloc failed");
        free(rb->scratch);
        free(rb->slots);
        memset(rb, 0, sizeof(*rb));
        return false;
    }

    rb->capacity = capacity_frames;
    return true;
}

void rewind_shutdown(RewindBuffer* rb) {
    if (!rb) return;
    if (rb->slots) {
        for (uint32_t i = 0; i < rb->capacity; i++) {
            free(rb->slots[i].data);
        }
        free(rb->slots);
    }
    free(rb->scratch);
    free(rb->lz4_scratch);
    memset(rb, 0, sizeof(*rb));
}

uint32_t rewind_depth(const RewindBuffer* rb) {
    return (rb && rb->slots) ? rb->count : 0;
}

size_t rewind_bytes_used(const RewindBuffer* rb) {
    return (rb && rb->slots) ? rb->bytes_used : 0;
}

bool rewind_active(const RewindBuffer* rb) {
    return rb && rb->active;
}

/* Record/begin/step/end/clear: implemented in later tasks. Stubs to satisfy linker. */
void rewind_record_frame(RewindBuffer* rb, GBA* gba) {
    (void)rb; (void)gba;
}
bool rewind_begin(RewindBuffer* rb)  { (void)rb; return false; }
bool rewind_step(RewindBuffer* rb, GBA* gba) { (void)rb; (void)gba; return false; }
void rewind_end(RewindBuffer* rb)    { (void)rb; }
void rewind_clear(RewindBuffer* rb)  { (void)rb; }
