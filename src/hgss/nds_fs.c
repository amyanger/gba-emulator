#include "hgss/nds_fs.h"

#include <string.h>

/* File access only. The FNT/FAT/NARC walking (nds_find, narc_count,
 * narc_member) is in rust/hgss/src/nds.rs and reads through nds_read. */

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool nds_read(const NdsRom* r, uint32_t off, void* dst, uint32_t len) {
    if (!r->f || off > r->size || len > r->size - off) return false;
    if (fseek(r->f, (long)off, SEEK_SET) != 0) return false;
    return fread(dst, 1, len, r->f) == len;
}

static bool span_ok(const NdsRom* r, uint32_t off, uint32_t len) {
    return off <= r->size && len <= r->size - off;
}

bool nds_open(NdsRom* r, const char* path) {
    memset(r, 0, sizeof(*r));
    r->f = fopen(path, "rb");
    if (!r->f) return false;
    if (fseek(r->f, 0, SEEK_END) != 0) goto fail;
    long sz = ftell(r->f);
    if (sz < 0x200 || (unsigned long)sz > 0xFFFFFFFFul) goto fail;
    r->size = (uint32_t)sz;
    uint8_t h[0x50];
    if (!nds_read(r, 0, h, sizeof(h))) goto fail;
    memcpy(r->code, h + 0x0C, 4);
    r->code[4] = '\0';
    r->fnt = rd32(h + 0x40);
    r->fnt_size = rd32(h + 0x44);
    r->fat = rd32(h + 0x48);
    r->fat_size = rd32(h + 0x4C);
    if (!span_ok(r, r->fnt, r->fnt_size) || !span_ok(r, r->fat, r->fat_size) || r->fnt_size < 8) goto fail;
    return true;
fail:
    nds_close(r);
    return false;
}

void nds_close(NdsRom* r) {
    if (r->f) fclose(r->f);
    r->f = NULL;
}

bool narc_read(const NdsRom* r, NdsSpan narc, uint32_t index, void* dst, uint32_t cap, uint32_t* len) {
    NdsSpan m;
    if (!narc_member(r, narc, index, &m)) return false;
    *len = m.end - m.start;
    return *len <= cap && nds_read(r, m.start, dst, *len);
}
