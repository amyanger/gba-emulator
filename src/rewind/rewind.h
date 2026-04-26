#ifndef REWIND_H
#define REWIND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct GBA GBA;

typedef struct {
    uint8_t* data;          /* compressed payload, malloc'd */
    uint32_t size;          /* compressed size in bytes */
    uint32_t cap;           /* current alloc size of data */
    uint32_t raw_size;      /* uncompressed size */
    uint64_t frame;         /* monotonic frame counter at write time */
    bool     uncompressed;  /* true if LZ4 declined to compress; data holds raw bytes */
} RewindFrame;

typedef struct {
    RewindFrame* slots;
    uint32_t     capacity;       /* number of slots */
    uint32_t     head;           /* next write index, wraps */
    uint32_t     count;          /* valid entries, saturates at capacity */
    uint8_t*     scratch;        /* SAVESTATE_MAX_BUF bytes for serialize/decompress */
    uint32_t     scratch_cap;    /* allocated size of scratch */
    uint8_t*     lz4_scratch;    /* LZ4 compress destination */
    uint32_t     lz4_scratch_cap;
    uint64_t     frame_counter;  /* monotonic, ticks every frame regardless of record */
    size_t       bytes_used;     /* sum of slot.size for valid slots */
    bool         active;         /* in reverse-playback */
} RewindBuffer;

bool     rewind_init(RewindBuffer* rb, uint32_t capacity_frames);
void     rewind_shutdown(RewindBuffer* rb);
void     rewind_record_frame(RewindBuffer* rb, GBA* gba);
bool     rewind_begin(RewindBuffer* rb);
bool     rewind_step(RewindBuffer* rb, GBA* gba);
void     rewind_end(RewindBuffer* rb);
void     rewind_clear(RewindBuffer* rb);
bool     rewind_active(const RewindBuffer* rb);
uint32_t rewind_depth(const RewindBuffer* rb);
size_t   rewind_bytes_used(const RewindBuffer* rb);

#endif /* REWIND_H */
