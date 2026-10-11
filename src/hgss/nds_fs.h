#ifndef NDS_FS_H
#define NDS_FS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* Read-only access to a Nintendo DS ROM file (the user's own HeartGold or
 * SoulSilver). The file is never loaded whole; every read seeks. */
typedef struct {
    FILE* f;
    uint32_t size;       /* file size in bytes */
    char code[5];        /* game code, NUL-terminated, e.g. "IPKE" */
    uint32_t fnt, fnt_size, fat, fat_size;
} NdsRom;

typedef struct { uint32_t start, end; } NdsSpan;   /* absolute file offsets, end exclusive */

bool nds_open(NdsRom* r, const char* path);         /* false if missing, short, or tables out of range */
void nds_close(NdsRom* r);
bool nds_read(const NdsRom* r, uint32_t off, void* dst, uint32_t len);   /* bounds-checked */
bool nds_find(const NdsRom* r, const char* path, NdsSpan* out);           /* "a/0/8/1" */
bool narc_count(const NdsRom* r, NdsSpan narc, uint16_t* count);
bool narc_member(const NdsRom* r, NdsSpan narc, uint32_t index, NdsSpan* out);
/* Reads member `index` into dst (cap bytes). *len = member size. False if it doesn't fit. */
bool narc_read(const NdsRom* r, NdsSpan narc, uint32_t index, void* dst, uint32_t cap, uint32_t* len);

#endif // NDS_FS_H
