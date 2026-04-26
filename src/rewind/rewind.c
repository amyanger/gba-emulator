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

void rewind_record_frame(RewindBuffer* rb, GBA* gba) {
    if (!rb || !rb->slots || !gba) return;
    if (rb->active) return;  /* don't record while playing back */

    uint64_t this_frame = rb->frame_counter++;
    if ((this_frame & 1u) != 0) return;  /* record every other frame */

    /* Serialize current GBA state into a malloc'd buffer. */
    uint8_t* raw = NULL;
    size_t   raw_size = 0;
    SaveStateResult r = savestate_save_to_buffer(gba, &raw, &raw_size);
    if (r != SS_OK) {
        LOG_WARN("Rewind: savestate_save_to_buffer failed (%d)", (int)r);
        return;
    }
    if (raw_size > rb->scratch_cap) {
        LOG_WARN("Rewind: state larger than scratch (%zu > %u), dropping",
                 raw_size, rb->scratch_cap);
        free(raw);
        return;
    }

    /* LZ4 compress raw -> lz4_scratch. */
    int c_len = LZ4_compress_default((const char*)raw, (char*)rb->lz4_scratch,
                                     (int)raw_size, (int)rb->lz4_scratch_cap);
    bool uncompressed = false;
    const uint8_t* payload;
    uint32_t       payload_size;
    if (c_len <= 0) {
        /* Incompressible: store raw. */
        uncompressed = true;
        payload = raw;
        payload_size = (uint32_t)raw_size;
    } else {
        payload = rb->lz4_scratch;
        payload_size = (uint32_t)c_len;
    }

    /* Place into slot at head, evicting whatever is there. */
    RewindFrame* slot = &rb->slots[rb->head];
    if (rb->count == rb->capacity) {
        rb->bytes_used -= slot->size;  /* evicting */
    }
    if (slot->cap < payload_size) {
        uint8_t* nbuf = (uint8_t*)realloc(slot->data, payload_size);
        if (!nbuf) {
            LOG_WARN("Rewind: slot realloc failed; dropping frame");
            free(raw);
            return;
        }
        slot->data = nbuf;
        slot->cap  = payload_size;
    }
    memcpy(slot->data, payload, payload_size);
    slot->size         = payload_size;
    slot->raw_size     = (uint32_t)raw_size;
    slot->frame        = this_frame;
    slot->uncompressed = uncompressed;

    rb->bytes_used += payload_size;
    rb->head = (rb->head + 1u) % rb->capacity;
    if (rb->count < rb->capacity) rb->count++;

    free(raw);
}

bool rewind_begin(RewindBuffer* rb) {
    if (!rb || !rb->slots) return false;
    if (rb->count == 0) return false;
    rb->active = true;
    return true;
}

bool rewind_step(RewindBuffer* rb, GBA* gba) {
    if (!rb || !rb->slots || !gba) return false;
    if (rb->count == 0) return false;

    /* Walk head backwards by 1, popping the most-recent slot. */
    rb->head = (rb->head + rb->capacity - 1u) % rb->capacity;
    rb->count--;
    RewindFrame* slot = &rb->slots[rb->head];
    rb->bytes_used -= slot->size;

    /* Decompress (or copy) into scratch. */
    if (slot->raw_size > rb->scratch_cap) {
        LOG_ERROR("Rewind: slot raw_size %u exceeds scratch %u",
                  slot->raw_size, rb->scratch_cap);
        rewind_clear(rb);
        rb->active = false;
        return false;
    }
    if (slot->uncompressed) {
        memcpy(rb->scratch, slot->data, slot->raw_size);
    } else {
        int d_len = LZ4_decompress_safe((const char*)slot->data,
                                        (char*)rb->scratch,
                                        (int)slot->size,
                                        (int)rb->scratch_cap);
        if (d_len <= 0 || (uint32_t)d_len != slot->raw_size) {
            LOG_ERROR("Rewind: LZ4 decompress failed (got %d, expected %u)",
                      d_len, slot->raw_size);
            rewind_clear(rb);
            rb->active = false;
            return false;
        }
    }

    SaveStateResult r = savestate_load_from_buffer(gba, rb->scratch, slot->raw_size);
    if (r != SS_OK) {
        LOG_ERROR("Rewind: savestate_load_from_buffer failed (%d)", (int)r);
        rewind_clear(rb);
        rb->active = false;
        return false;
    }
    return true;
}

void rewind_end(RewindBuffer* rb) {
    if (!rb) return;
    rb->active = false;
}

void rewind_clear(RewindBuffer* rb) {
    if (!rb || !rb->slots) return;
    rb->head = 0;
    rb->count = 0;
    rb->bytes_used = 0;
    rb->active = false;
    /* Keep slot allocations for reuse — only reset metadata. */
    for (uint32_t i = 0; i < rb->capacity; i++) {
        rb->slots[i].size = 0;
        rb->slots[i].raw_size = 0;
        rb->slots[i].frame = 0;
        rb->slots[i].uncompressed = false;
    }
}
