#include "cheat/cheat_decode.h"
#include "cheat/cheat_crypto.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GAME_ID_MAGIC 0x001DC0DEu
#define SEED_CHANGE   0xDEADFACEu

typedef struct {
    CheatOp* ops;
    uint32_t max, n;
    CheatDecodeInfo* info;
} Out;

static const struct { uint32_t lo, hi; } k_regions[] = {
    {0x02000000u, 0x0203FFFFu},   /* EWRAM */
    {0x03000000u, 0x03007FFFu},   /* IWRAM */
    {0x04000000u, 0x040003FFu},   /* I/O */
    {0x05000000u, 0x050003FFu},   /* palette */
    {0x06000000u, 0x06017FFFu},   /* VRAM */
    {0x07000000u, 0x070003FFu},   /* OAM */
};

bool cheat_addr_allowed(uint32_t addr, uint32_t width) {
    if (width == 0) width = 1;
    for (size_t i = 0; i < sizeof(k_regions) / sizeof(k_regions[0]); i++) {
        if (addr >= k_regions[i].lo && addr <= k_regions[i].hi &&
            addr + width - 1 <= k_regions[i].hi)
            return true;
    }
    return false;
}

static bool fail(Out* o, const char* fmt, uint32_t a, uint32_t b) {
    snprintf(o->info->fail, sizeof(o->info->fail), fmt, (unsigned)a, (unsigned)b);
    return false;
}

static CheatOp* emit(Out* o, uint8_t kind) {
    if (o->n >= o->max) return NULL;
    CheatOp* op = &o->ops[o->n++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

/* Appends an op touching memory, refusing addresses outside allowed regions. */
static bool emit_mem(Out* o, uint8_t kind, uint32_t addr, uint8_t width, uint32_t value) {
    if (!cheat_addr_allowed(addr, width)) return fail(o, "Address %08X is outside game RAM", addr, 0);
    CheatOp* op = emit(o, kind);
    if (!op) return fail(o, "Too many operations in one cheat", 0, 0);
    op->addr = addr;
    op->width = width;
    op->value = value;
    return true;
}

static bool emit_if(Out* o, uint8_t cmp, uint32_t addr, uint8_t width, uint32_t value, uint8_t skip) {
    if (!emit_mem(o, CHEAT_OP_IF, addr, width, value)) return false;
    o->ops[o->n - 1].cmp = cmp;
    o->ops[o->n - 1].skip = skip;
    return true;
}

static bool emit_slide(Out* o, uint32_t addr, uint8_t width, uint32_t value, uint16_t count,
                       int16_t value_step, uint32_t addr_step) {
    if (count == 0) count = 1;
    uint32_t last = addr + (uint32_t)(count - 1) * addr_step;
    if (!cheat_addr_allowed(addr, width) || !cheat_addr_allowed(last, width))
        return fail(o, "Address %08X is outside game RAM", cheat_addr_allowed(addr, width) ? last : addr, 0);
    if (!emit_mem(o, CHEAT_OP_SLIDE, addr, width, value)) return false;
    CheatOp* op = &o->ops[o->n - 1];
    op->count = count;
    op->value_step = value_step;
    op->addr_step = addr_step;
    return true;
}

static bool emit_simple(Out* o, uint8_t kind, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (!emit(o, kind)) return fail(o, "Too many operations in one cheat", 0, 0);
        if (kind == CHEAT_OP_MARKER) o->info->markers++;
        if (kind == CHEAT_OP_UNSUPPORTED) o->info->unsupported++;
    }
    return true;
}

static void note_id(Out* o, uint32_t word) {
    o->info->id_found = true;
    o->info->id_word = word;
}

/* ---- CodeBreaker (8+4 digits) ---- */

static bool decode_cb(const CheatRawList* raw, Out* o) {
    CbCrypt cb;
    bool encrypted = false;
    for (uint32_t i = 0; i < raw->count; i++) {
        uint32_t a = raw->lines[i].a;
        uint16_t v = (uint16_t)raw->lines[i].b;
        if (encrypted) cheat_cb_decrypt(&cb, &a, &v);
        uint32_t addr = a & 0x0FFFFFFFu;

        switch (a >> 28) {
        case 0x0:   /* game ID (checksum) */
        case 0x1:   /* hook */
            if (!emit_simple(o, CHEAT_OP_MARKER, 1)) return false;
            break;
        case 0x2: if (!emit_mem(o, CHEAT_OP_OR, addr, 2, v)) return false; break;
        case 0x3: if (!emit_mem(o, CHEAT_OP_WRITE, addr, 1, v & 0xFFu)) return false; break;
        case 0x4: {   /* slide; next line: iiiicccc ssss */
            if (i + 1 >= raw->count) return fail(o, "Slide code %08X needs a second line", a, 0);
            uint32_t na = raw->lines[i + 1].a;
            uint16_t nv = (uint16_t)raw->lines[i + 1].b;
            if (encrypted) cheat_cb_decrypt(&cb, &na, &nv);
            i++;
            if (!emit_slide(o, addr, 2, v, (uint16_t)(na & 0xFFFFu), (int16_t)(na >> 16), nv))
                return false;
            break;
        }
        case 0x5:
            /* String write: GBATEK counts cccc in bytes, mGBA and VBA-M in
             * halfwords. A wrong guess turns its data lines into code lines, so
             * refuse until a real code settles it. */
            return fail(o, "CodeBreaker type 5 (string write) codes aren't supported yet", 0, 0);
        case 0x6: if (!emit_mem(o, CHEAT_OP_AND, addr, 2, v)) return false; break;
        case 0x7: if (!emit_if(o, CHEAT_CMP_EQ, addr, 2, v, 1)) return false; break;
        case 0x8: if (!emit_mem(o, CHEAT_OP_WRITE, addr, 2, v)) return false; break;
        case 0x9:   /* key: everything after is encrypted */
            cheat_cb_reseed(&cb, a, v);
            encrypted = true;
            if (!emit_simple(o, CHEAT_OP_MARKER, 1)) return false;
            break;
        case 0xA: if (!emit_if(o, CHEAT_CMP_NE, addr, 2, v, 1)) return false; break;
        case 0xB: if (!emit_if(o, CHEAT_CMP_GT_S, addr, 2, v, 1)) return false; break;
        case 0xC: if (!emit_if(o, CHEAT_CMP_LT_S, addr, 2, v, 1)) return false; break;
        case 0xD:
            if (a == 0xD0000020u) {
                CheatOp* op = emit(o, CHEAT_OP_IF);
                if (!op) return fail(o, "Too many operations in one cheat", 0, 0);
                op->cmp = CHEAT_CMP_KEYS;
                op->value = v;
                op->skip = 1;
            } else if (!emit_simple(o, CHEAT_OP_UNSUPPORTED, 1)) {
                return false;
            }
            break;
        case 0xE: if (!emit_mem(o, CHEAT_OP_ADD, addr, 2, v)) return false; break;
        default:  /* 0xF */
            if (!emit_if(o, CHEAT_CMP_AND, addr, 2, v, 1)) return false;
            break;
        }
    }
    return true;
}

/* ---- GameShark / Action Replay v1/v2 (8+8 digits) ---- */

static bool decode_gsa(const CheatRawList* raw, bool encrypted, Out* o) {
    for (uint32_t i = 0; i < raw->count; i++) {
        uint32_t a = raw->lines[i].a, b = raw->lines[i].b;
        if (encrypted) cheat_tea_decrypt(&a, &b, CHEAT_SEEDS_GSA);
        uint32_t addr = a & 0x0FFFFFFFu;

        if (b == GAME_ID_MAGIC) {
            note_id(o, a);
            if (!emit_simple(o, CHEAT_OP_MARKER, 1)) return false;
            continue;
        }
        if (a == SEED_CHANGE) {   /* DEADFACE tables are not in GBATEK */
            if (!emit_simple(o, CHEAT_OP_UNSUPPORTED, 1)) return false;
            continue;
        }
        switch (a >> 28) {
        case 0x0: if (!emit_mem(o, CHEAT_OP_WRITE, addr, 1, b & 0xFFu)) return false; break;
        case 0x1: if (!emit_mem(o, CHEAT_OP_WRITE, addr, 2, b & 0xFFFFu)) return false; break;
        case 0x2: if (!emit_mem(o, CHEAT_OP_WRITE, addr, 4, b)) return false; break;
        case 0x3: {   /* one value to a list of addresses: rare, not run */
            uint32_t extra = ((a & 0xFFFFu) > 1) ? ((a & 0xFFFFu) - 1 + 1) / 2 : 0;
            if (i + extra >= raw->count) return fail(o, "Address list %08X is cut short", a, 0);
            i += extra;
            if (!emit_simple(o, CHEAT_OP_UNSUPPORTED, 1 + extra)) return false;
            break;
        }
        case 0x6:   /* ROM patch */
        case 0x8:   /* GameShark button */
            if (!emit_simple(o, CHEAT_OP_UNSUPPORTED, 1)) return false;
            break;
        case 0xD: {
            static const uint8_t cmps[] = {CHEAT_CMP_EQ, CHEAT_CMP_NE, CHEAT_CMP_LE_U, CHEAT_CMP_GE_U};
            uint32_t c = b >> 20;
            if (c >= 4) return fail(o, "Unknown condition in %08X %08X", a, b);
            if (!emit_if(o, cmps[c], addr, 2, b & 0xFFFFu, 1)) return false;
            break;
        }
        case 0xE:   /* E0zzxxxx 0aaaaaaa: IF [a] == xxxx THEN next zz */
            if (!emit_if(o, CHEAT_CMP_EQ, b & 0x0FFFFFFFu, 2, a & 0xFFFFu, (uint8_t)(a >> 16)))
                return false;
            break;
        case 0xF:   /* hook */
            if (!emit_simple(o, CHEAT_OP_MARKER, 1)) return false;
            break;
        default:
            return fail(o, "Unknown code type in %08X %08X", a, b);
        }
    }
    return true;
}

/* ---- Pro Action Replay v3 (8+8 digits) ---- */

#define PAR3_COND   0x38000000u
#define PAR3_WIDTH  0x06000000u
#define PAR3_ACTION 0xC0000000u

/* Codes pack the address as a0aaaaa: top nibble moved down next to the offset. */
static uint32_t par3_addr(uint32_t x) { return (x & 0x000FFFFFu) | ((x << 4) & 0x0F000000u); }

static uint32_t width_mask(uint32_t w) { return w >= 4 ? 0xFFFFFFFFu : ((1u << (w * 8)) - 1); }

static bool decode_par3_special(const CheatRawList* raw, uint32_t* i, bool encrypted, uint32_t b, Out* o) {
    uint32_t kind = b & 0xFF000000u;
    switch (kind) {
    case 0x00000000u: return emit_simple(o, CHEAT_OP_MARKER, 1);   /* end of list */
    case 0x08000000u: return emit_simple(o, CHEAT_OP_MARKER, 1);   /* slowdown */
    case 0x40000000u:
    case 0x60000000u:
        if (!emit(o, kind == 0x40000000u ? CHEAT_OP_ENDIF : CHEAT_OP_ELSE))
            return fail(o, "Too many operations in one cheat", 0, 0);
        o->info->weak++;
        return true;
    case 0x10000000u: case 0x12000000u: case 0x14000000u:   /* AR button */
    case 0x18000000u: case 0x1A000000u: case 0x1C000000u: case 0x1E000000u:   /* ROM patch */
        if (*i + 1 >= raw->count) return fail(o, "Code %08X needs a second line", b, 0);
        (*i)++;
        return emit_simple(o, CHEAT_OP_UNSUPPORTED, 2);
    case 0x80000000u: case 0x82000000u: case 0x84000000u: {   /* slide */
        if (*i + 1 >= raw->count) return fail(o, "Slide %08X needs a second line", b, 0);
        uint32_t na = raw->lines[*i + 1].a, nb = raw->lines[*i + 1].b;
        if (encrypted) cheat_tea_decrypt(&na, &nb, CHEAT_SEEDS_PAR3);
        (*i)++;
        uint8_t w = (uint8_t)(1u << ((kind >> 25) & 3u));
        return emit_slide(o, par3_addr(b), w, na & width_mask(w), (uint16_t)((nb >> 16) & 0xFFu),
                          (int16_t)(nb >> 24), (nb & 0xFFFFu) * w);
    }
    default:
        return fail(o, "Unknown code type in 00000000 %08X", b, 0);
    }
}

static bool decode_par3(const CheatRawList* raw, bool encrypted, Out* o) {
    for (uint32_t i = 0; i < raw->count; i++) {
        uint32_t a = raw->lines[i].a, b = raw->lines[i].b;
        if (encrypted) cheat_tea_decrypt(&a, &b, CHEAT_SEEDS_PAR3);

        if (b == GAME_ID_MAGIC) {
            note_id(o, a);
            if (!emit_simple(o, CHEAT_OP_MARKER, 1)) return false;
            continue;
        }
        if (a == 0) {
            if (!decode_par3_special(raw, &i, encrypted, b, o)) return false;
            continue;
        }
        if (a == SEED_CHANGE) {
            if (!emit_simple(o, CHEAT_OP_UNSUPPORTED, 1)) return false;
            continue;
        }
        if ((a >> 24) == 0xC4u) {   /* hook */
            if (!emit_simple(o, CHEAT_OP_MARKER, 1)) return false;
            continue;
        }
        if ((a & 0xFE000000u) == 0xC6000000u) {   /* I/O write: C6 16-bit, C7 32-bit (GBATEK) */
            uint8_t w = (a & 0x01000000u) ? 4 : 2;
            if (!emit_mem(o, CHEAT_OP_WRITE, 0x04000000u | (a & 0x00FFFFFFu), w, b & width_mask(w)))
                return false;
            continue;
        }
        if (a & 0x01000000u) return fail(o, "Unknown code type in %08X %08X", a, b);

        uint32_t w = 1u << ((a & PAR3_WIDTH) >> 25);
        if (a & PAR3_COND) {
            static const uint8_t cmps[8] = {CHEAT_CMP_NEVER, CHEAT_CMP_EQ, CHEAT_CMP_NE, CHEAT_CMP_LT_S,
                                            CHEAT_CMP_GT_S, CHEAT_CMP_LT_U, CHEAT_CMP_GT_U, CHEAT_CMP_AND};
            uint32_t action = a & PAR3_ACTION;
            if (action == 0xC0000000u) {   /* "turn off all codes" */
                if (!emit_simple(o, CHEAT_OP_UNSUPPORTED, 1)) return false;
                continue;
            }
            uint8_t skip = action == 0 ? 1 : action == 0x40000000u ? 2 : CHEAT_SKIP_BLOCK;
            uint8_t cmp = cmps[(a & PAR3_COND) >> 27];
            if (w > 4) {   /* width bits 11: the condition is always false */
                CheatOp* op = emit(o, CHEAT_OP_IF);
                if (!op) return fail(o, "Too many operations in one cheat", 0, 0);
                op->cmp = CHEAT_CMP_NEVER;
                op->skip = skip;
                o->info->weak++;
                continue;
            }
            if (!emit_if(o, cmp, par3_addr(a), (uint8_t)w, b & width_mask(w), skip)) return false;
            continue;
        }
        if (w > 4) return fail(o, "Unknown code type in %08X %08X", a, b);

        uint32_t addr = par3_addr(a);
        switch (a & 0xC0000000u) {
        case 0x00000000u:   /* assign, 8/16-bit forms fill (count+1) units */
            if (w < 4 && (b >> (w * 8)) != 0) {
                if (!emit_slide(o, addr, (uint8_t)w, b & width_mask(w), (uint16_t)((b >> (w * 8)) + 1), 0, w))
                    return false;
            } else if (!emit_mem(o, CHEAT_OP_WRITE, addr, (uint8_t)w, b & width_mask(w))) {
                return false;
            }
            break;
        case 0x40000000u: {   /* indirect: [[addr] + offset] = value */
            if (!emit_mem(o, CHEAT_OP_WRITE, addr, 4, b & width_mask(w))) return false;
            CheatOp* op = &o->ops[o->n - 1];
            op->width = (uint8_t)w;
            op->flags = CHEAT_OPF_INDIRECT;
            op->value2 = w < 4 ? (b >> (w * 8)) * w : 0;
            break;
        }
        case 0x80000000u:
            if (!emit_mem(o, CHEAT_OP_ADD, addr, (uint8_t)w, b & width_mask(w))) return false;
            break;
        default:
            return fail(o, "Unknown code type in %08X %08X", a, b);
        }
    }
    return true;
}

bool cheat_decode(const CheatRawList* raw, CheatDevice dev, CheatOp* ops, uint32_t max_ops,
                  uint32_t* op_count, CheatDecodeInfo* info) {
    Out o = {ops, max_ops, 0, info};
    memset(info, 0, sizeof(*info));
    bool ok;
    bool cb_shape = raw->count > 0 && raw->lines[0].digits == 4;
    if ((dev == CHEAT_DEV_CODEBREAKER) != cb_shape) {
        ok = fail(&o, cb_shape ? "These are CodeBreaker codes (8+4 digits)"
                               : "These are not CodeBreaker codes (8+8 digits)", 0, 0);
    } else {
        switch (dev) {
        case CHEAT_DEV_CODEBREAKER: ok = decode_cb(raw, &o); break;
        case CHEAT_DEV_GSA:         ok = decode_gsa(raw, true, &o); break;
        case CHEAT_DEV_GSA_RAW:     ok = decode_gsa(raw, false, &o); break;
        case CHEAT_DEV_PAR3:        ok = decode_par3(raw, true, &o); break;
        case CHEAT_DEV_PAR3_RAW:    ok = decode_par3(raw, false, &o); break;
        default:                    ok = fail(&o, "No device chosen", 0, 0); break;
        }
    }
    *op_count = ok ? o.n : 0;
    return ok;
}

void cheat_detect(const CheatRawList* raw, uint32_t game_code, CheatDetect* out) {
    static CheatOp scratch[CHEAT_MAX_OPS];
    out->device = CHEAT_DEV_AUTO;
    out->confident = false;
    out->id_match = false;
    if (raw->count == 0) return;

    CheatDecodeInfo info;
    uint32_t n;
    if (raw->lines[0].digits == 4) {
        if (cheat_decode(raw, CHEAT_DEV_CODEBREAKER, scratch, CHEAT_MAX_OPS, &n, &info)) {
            out->device = CHEAT_DEV_CODEBREAKER;
            out->confident = true;
        }
        return;
    }

    static const CheatDevice order[] = {CHEAT_DEV_GSA, CHEAT_DEV_PAR3, CHEAT_DEV_GSA_RAW, CHEAT_DEV_PAR3_RAW};
    int32_t best = INT32_MIN, second = INT32_MIN;
    for (size_t k = 0; k < sizeof(order) / sizeof(order[0]); k++) {
        if (!cheat_decode(raw, order[k], scratch, CHEAT_MAX_OPS, &n, &info)) continue;
        bool id = info.id_found && game_code != 0 && info.id_word == game_code;
        int32_t real = (int32_t)(n - info.markers - info.unsupported - info.weak);
        /* Random words often land on rarely used types (ROM patch, buttons), so a
         * reading with nothing real in it is implausible, and unsupported lines
         * count against one. */
        if (real == 0 && !id) continue;
        int32_t score = real * 10 - (int32_t)info.unsupported * 10 + (id ? 1000 : 0);
        if (score > best) {
            second = best;
            best = score;
            out->device = order[k];
            out->id_match = id;
        } else if (score > second) {
            second = score;
        }
    }
    out->confident = out->device != CHEAT_DEV_AUTO && (out->id_match || second == INT32_MIN || best - second >= 10);
}
