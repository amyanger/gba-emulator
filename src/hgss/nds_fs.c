#include "hgss/nds_fs.h"

#include <string.h>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
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

/* Looks `name` up in directory `dir` (0xF000-based id). Sets *is_dir and *id. */
static bool fnt_lookup(const NdsRom* r, uint16_t dir, const char* name, size_t name_len, bool* is_dir,
                       uint16_t* id) {
    uint8_t main[8];
    uint32_t idx = (uint32_t)(dir & 0x0FFF);
    if (8 * idx + 8 > r->fnt_size || !nds_read(r, r->fnt + 8 * idx, main, 8)) return false;
    uint32_t off = r->fnt + rd32(main);
    uint16_t file_id = rd16(main + 4);
    uint32_t end = r->fnt + r->fnt_size;
    while (off < end) {
        uint8_t len = 0;
        if (!nds_read(r, off, &len, 1)) return false;
        off++;
        if (len == 0) return false;
        uint8_t n = len & 0x7F;
        char buf[128];
        if (!nds_read(r, off, buf, n)) return false;
        off += n;
        bool dir_entry = (len & 0x80) != 0;
        uint16_t sub = 0;
        if (dir_entry) {
            uint8_t b[2];
            if (!nds_read(r, off, b, 2)) return false;
            off += 2;
            sub = rd16(b);
        }
        if (n == name_len && memcmp(buf, name, n) == 0) {
            *is_dir = dir_entry;
            *id = dir_entry ? sub : file_id;
            return true;
        }
        if (!dir_entry) file_id++;
    }
    return false;
}

bool nds_find(const NdsRom* r, const char* path, NdsSpan* out) {
    uint16_t dir = 0xF000;
    const char* p = path;
    for (;;) {
        const char* slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        bool is_dir = false;
        uint16_t id = 0;
        if (n == 0 || n > 127 || !fnt_lookup(r, dir, p, n, &is_dir, &id)) return false;
        if (slash) {
            if (!is_dir) return false;
            dir = id;
            p = slash + 1;
            continue;
        }
        if (is_dir || 8u * id + 8 > r->fat_size) return false;
        uint8_t e[8];
        if (!nds_read(r, r->fat + 8u * id, e, 8)) return false;
        out->start = rd32(e);
        out->end = rd32(e + 4);
        return out->start <= out->end && span_ok(r, out->start, out->end - out->start);
    }
}

/* BTAF position and GMIF data start for a NARC. */
static bool narc_layout(const NdsRom* r, NdsSpan narc, uint32_t* btaf, uint16_t* count, uint32_t* gmif_data,
                        uint32_t* gmif_end) {
    uint8_t h[16], b[12];
    if (narc.end - narc.start < 16 || !nds_read(r, narc.start, h, 16) || memcmp(h, "NARC", 4) != 0) return false;
    uint32_t at = narc.start + rd16(h + 0x0C);
    if (at + 12 > narc.end || !nds_read(r, at, b, 12) || memcmp(b, "BTAF", 4) != 0) return false;
    *btaf = at;
    *count = rd16(b + 8);
    at += rd32(b + 4);
    if (at + 8 > narc.end || !nds_read(r, at, b, 8) || memcmp(b, "BTNF", 4) != 0) return false;
    at += rd32(b + 4);
    if (at + 8 > narc.end || !nds_read(r, at, b, 8) || memcmp(b, "GMIF", 4) != 0) return false;
    uint32_t gsize = rd32(b + 4);
    if (gsize < 8 || gsize > narc.end - at) return false;
    *gmif_data = at + 8;
    *gmif_end = at + gsize;
    return true;
}

bool narc_count(const NdsRom* r, NdsSpan narc, uint16_t* count) {
    uint32_t btaf, gd, ge;
    return narc_layout(r, narc, &btaf, count, &gd, &ge);
}

bool narc_member(const NdsRom* r, NdsSpan narc, uint32_t index, NdsSpan* out) {
    uint32_t btaf, gd, ge;
    uint16_t count;
    uint8_t e[8];
    if (!narc_layout(r, narc, &btaf, &count, &gd, &ge) || index >= count) return false;
    if (!nds_read(r, btaf + 12 + 8 * index, e, 8)) return false;
    uint32_t s = rd32(e), t = rd32(e + 4);
    if (s > t || t > ge - gd) return false;
    out->start = gd + s;
    out->end = gd + t;
    return true;
}

bool narc_read(const NdsRom* r, NdsSpan narc, uint32_t index, void* dst, uint32_t cap, uint32_t* len) {
    NdsSpan m;
    if (!narc_member(r, narc, index, &m)) return false;
    *len = m.end - m.start;
    return *len <= cap && nds_read(r, m.start, dst, *len);
}
