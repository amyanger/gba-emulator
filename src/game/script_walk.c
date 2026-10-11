#include "game/script_walk.h"
#include <stdbool.h>
#include <string.h>

/* Walk limits and rules from overworld-gifts.md (verified 66/66 on the retail ROM). */
#define SW_BUDGET       256
#define SW_MAX_DEPTH    4
#define SW_AFTER        16
#define SW_QUEUE        64
#define SW_PATH_FLAGS   16
#define SW_RAW_GIFTS    16
#define SW_CHECKED      64
#define SW_FLAG_MIN     0x20
#define SW_FLAG_END     0x500     /* story flags only: drops trainer, system and daily flags */
#define SW_DYNAMIC      0x4000    /* setorcopyvar copies from a var at and above this */
#define VAR_ITEM        0x8000
#define VAR_QTY         0x8001
#define VAR_LAST_TALKED 0x800F
#define ROM_BASE        0x08000000u

/* Total command length incl. opcode, from asm/macros/event.inc; 0 = unknown (stop). */
static const uint8_t k_len[256] = {
    [0x00] = 1, [0x01] = 1, [0x02] = 1, [0x03] = 1, [0x04] = 5, [0x05] = 5, [0x06] = 6, [0x07] = 6,
    [0x08] = 2, [0x09] = 2, [0x0A] = 3, [0x0B] = 3, [0x0C] = 1, [0x0D] = 1, [0x0E] = 2, [0x0F] = 6,
    [0x10] = 3, [0x11] = 6, [0x12] = 6, [0x13] = 6, [0x14] = 3, [0x15] = 9, [0x16] = 5, [0x17] = 5,
    [0x18] = 5, [0x19] = 5, [0x1A] = 5, [0x1B] = 3, [0x1C] = 3, [0x1D] = 6, [0x1E] = 6, [0x1F] = 6,
    [0x20] = 9, [0x21] = 5, [0x22] = 5, [0x23] = 5, [0x24] = 5, [0x25] = 3, [0x26] = 5, [0x27] = 1,
    [0x28] = 3, [0x29] = 3, [0x2A] = 3, [0x2B] = 3, [0x2C] = 5, [0x2D] = 1, [0x2E] = 1, [0x2F] = 3,
    [0x30] = 1, [0x31] = 3, [0x32] = 1, [0x33] = 4, [0x34] = 3, [0x35] = 1, [0x36] = 3, [0x37] = 2,
    [0x38] = 2, [0x39] = 8, [0x3A] = 8, [0x3B] = 8, [0x3C] = 3, [0x3D] = 8, [0x3E] = 8, [0x3F] = 8,
    [0x40] = 8, [0x41] = 8, [0x42] = 5, [0x43] = 1, [0x44] = 5, [0x45] = 5, [0x46] = 5, [0x47] = 5,
    [0x48] = 3, [0x49] = 5, [0x4A] = 5, [0x4B] = 3, [0x4C] = 3, [0x4D] = 3, [0x4E] = 3, [0x4F] = 7,
    [0x50] = 9, [0x51] = 3, [0x52] = 5, [0x53] = 3, [0x54] = 5, [0x55] = 3, [0x56] = 5, [0x57] = 7,
    [0x58] = 5, [0x59] = 5, [0x5A] = 1, [0x5B] = 4, [0x5D] = 1, [0x5E] = 1, [0x5F] = 1, [0x60] = 3,
    [0x61] = 3, [0x62] = 3, [0x63] = 7, [0x64] = 3, [0x65] = 4, [0x66] = 1, [0x67] = 5, [0x68] = 1,
    [0x69] = 1, [0x6A] = 1, [0x6B] = 1, [0x6C] = 1, [0x6D] = 1, [0x6E] = 3, [0x6F] = 5, [0x70] = 6,
    [0x71] = 6, [0x72] = 1, [0x73] = 5, [0x74] = 5, [0x75] = 5, [0x76] = 1, [0x77] = 2, [0x78] = 5,
    [0x79] = 15, [0x7A] = 3, [0x7B] = 5, [0x7C] = 3, [0x7D] = 4, [0x7E] = 2, [0x7F] = 4, [0x80] = 4,
    [0x81] = 4, [0x82] = 4, [0x83] = 4, [0x84] = 4, [0x85] = 6, [0x86] = 5, [0x87] = 5, [0x88] = 5,
    [0x89] = 3, [0x8A] = 4, [0x8B] = 1, [0x8C] = 1, [0x8D] = 1, [0x8E] = 1, [0x8F] = 3, [0x90] = 6,
    [0x91] = 6, [0x92] = 6, [0x93] = 4, [0x94] = 3, [0x95] = 4, [0x96] = 3, [0x97] = 2, [0x98] = 3,
    [0x99] = 3, [0x9A] = 2, [0x9B] = 5, [0x9C] = 3, [0x9D] = 4, [0x9E] = 3, [0x9F] = 3, [0xA0] = 1,
    [0xA1] = 5, [0xA2] = 9, [0xA3] = 1, [0xA4] = 3, [0xA5] = 1, [0xA6] = 2, [0xA7] = 3, [0xA8] = 6,
    [0xA9] = 5, [0xAA] = 9, [0xAB] = 3, [0xAC] = 5, [0xAD] = 5, [0xAE] = 1, [0xAF] = 5, [0xB0] = 5,
    [0xB1] = 8, [0xB2] = 1, [0xB3] = 3, [0xB4] = 3, [0xB5] = 3, [0xB6] = 6, [0xB7] = 1, [0xB8] = 5,
    [0xC0] = 3, [0xC1] = 3, [0xC2] = 3, [0xC3] = 2, [0xC4] = 8, [0xC5] = 1, [0xC6] = 4, [0xC7] = 2,
    [0xC8] = 5, [0xC9] = 1, [0xCA] = 1, [0xCB] = 1, [0xCC] = 6, [0xCD] = 3, [0xCE] = 3, [0xCF] = 1,
    [0xD0] = 3, [0xD1] = 8, [0xD2] = 4, [0xD3] = 3, [0xD4] = 1, [0xD5] = 3, [0xD6] = 1, [0xD7] = 8,
    [0xD8] = 1, [0xD9] = 1, [0xDA] = 1, [0xDB] = 5, [0xDC] = 2, [0xDD] = 4, [0xDE] = 4, [0xDF] = 5,
    [0xE0] = 8, [0xE1] = 4, [0xE2] = 6,
};
/* trainerbattle (0x5C): length and post-battle script offset by type byte. */
static const uint8_t k_tb_len[13] = {14, 18, 18, 10, 18, 14, 22, 18, 22, 14, 14, 14, 14};
static const uint8_t k_tb_event[13] = {0, 14, 14, 0, 0, 0, 18, 0, 18, 0, 0, 0, 0};

typedef struct { uint32_t addr; uint8_t depth, nflags; uint16_t flags[SW_PATH_FLAGS]; } SwWork;
typedef struct {
    uint16_t item;
    uint8_t npre, nafter;
    uint16_t pre[SW_PATH_FLAGS], after[SW_PATH_FLAGS];
    bool self_remove;
} SwRaw;
typedef struct {
    const GameMem* m;
    uint8_t lid;
    uint32_t seen[SW_BUDGET], nseen, cmds;
    SwWork queue[SW_QUEUE];
    uint32_t qhead, qtail;
    SwRaw raw[SW_RAW_GIFTS];
    uint32_t nraw;
    uint16_t checked[SW_CHECKED];
    uint32_t nchecked;
} SwState;

static uint8_t rd8(const SwState* st, uint32_t a) { uint8_t v = 0; game_mem_read8(st->m, a, &v); return v; }
static uint16_t rd16(const SwState* st, uint32_t a) { uint16_t v = 0; game_mem_read16(st->m, a, &v); return v; }
static uint32_t rd32(const SwState* st, uint32_t a) { uint32_t v = 0; game_mem_read32(st->m, a, &v); return v; }
/* Matches the prototype's inrom(): 16 bytes of headroom for the longest operands. */
static bool in_rom(const SwState* st, uint32_t a) { return a >= ROM_BASE && game_mem_ok(st->m, a, 16); }
static bool is_end(uint8_t op) {
    return op == 0x02 || op == 0x03 || op == 0x05 || op == 0x08 || op == 0x0C || op == 0x0D || op == 0x24 ||
           op == 0xB9;
}
static bool seen(const SwState* st, uint32_t a) {
    for (uint32_t i = 0; i < st->nseen; i++)
        if (st->seen[i] == a) return true;
    return false;
}
static bool checked(const SwState* st, uint16_t f) {
    for (uint32_t i = 0; i < st->nchecked; i++)
        if (st->checked[i] == f) return true;
    return false;
}
static void push(SwState* st, uint32_t addr, uint8_t depth, const uint16_t* flags, uint8_t n) {
    if (st->qtail >= SW_QUEUE) return;
    SwWork* w = &st->queue[st->qtail++];
    w->addr = addr;
    w->depth = depth;
    w->nflags = n;
    if (n) memcpy(w->flags, flags, n * sizeof(uint16_t));   /* flags is NULL for the entry */
}

/* Straight-line success path after a give: follow goto, step over the rest, collect setflags. */
static void scan_after(const SwState* st, uint32_t a, SwRaw* g) {
    uint32_t visited[SW_AFTER];
    uint32_t nvis = 0;
    for (uint32_t i = 0; i < SW_AFTER; i++) {
        bool again = false;
        for (uint32_t k = 0; k < nvis; k++) again = again || visited[k] == a;
        if (!in_rom(st, a) || again) break;
        visited[nvis++] = a;
        uint8_t op = rd8(st, a);
        if (op == 0x29 && g->nafter < SW_PATH_FLAGS) g->after[g->nafter++] = rd16(st, a + 1);
        if (op == 0x53) {
            uint16_t lid = rd16(st, a + 1);
            if (lid == VAR_LAST_TALKED || lid == st->lid) g->self_remove = true;
        }
        if (op == 0x05) {
            a = rd32(st, a + 1);
            continue;
        }
        if (is_end(op) || op == 0x5C) break;
        if (op == 0x09 && rd8(st, a + 1) == 0) break;   /* the next gift */
        if (!k_len[op]) break;
        a += k_len[op];
    }
}

static void record_gift(SwState* st, uint16_t item, const uint16_t* pf, uint8_t npf, uint32_t next) {
    if (st->nraw >= SW_RAW_GIFTS) return;
    SwRaw* g = &st->raw[st->nraw++];
    memset(g, 0, sizeof(*g));
    g->item = item;
    g->npre = npf;
    memcpy(g->pre, pf, npf * sizeof(uint16_t));
    scan_after(st, next, g);
}

static void walk(SwState* st, uint32_t start) {
    push(st, start, 0, NULL, 0);
    while (st->qhead < st->qtail) {
        SwWork w = st->queue[st->qhead++];
        uint32_t a = w.addr;
        uint8_t depth = w.depth, npf = w.nflags;
        uint16_t pf[SW_PATH_FLAGS];
        int32_t v8000 = -1;
        memcpy(pf, w.flags, sizeof(pf));
        for (;;) {
            if (seen(st, a) || !in_rom(st, a)) break;
            if (st->cmds >= SW_BUDGET) {
                st->qhead = st->qtail;   /* budget spent: stop everything */
                break;
            }
            st->seen[st->nseen++] = a;
            st->cmds++;
            uint8_t op = rd8(st, a);
            if (op == 0x5C) {
                uint8_t t = rd8(st, a + 1);
                if (t >= sizeof(k_tb_len)) break;
                if (k_tb_event[t] && depth < SW_MAX_DEPTH) push(st, rd32(st, a + k_tb_event[t]), (uint8_t)(depth + 1), pf, npf);
                a += k_tb_len[t];
                continue;
            }
            uint8_t n = k_len[op];
            if (!n) break;
            if (op == 0x1A || op == 0x16) {
                if (rd16(st, a + 1) == VAR_ITEM) v8000 = rd16(st, a + 3);
            } else if (op == 0x09 || op == 0x0B) {
                uint8_t fn = op == 0x09 ? rd8(st, a + 1) : rd8(st, a + 2);
                if (fn == 0 && v8000 >= 0) record_gift(st, (uint16_t)v8000, pf, npf, a + n);
            } else if (op == 0x2B) {
                if (st->nchecked < SW_CHECKED) st->checked[st->nchecked++] = rd16(st, a + 1);
            } else if (op == 0x29) {
                if (npf < SW_PATH_FLAGS) pf[npf++] = rd16(st, a + 1);
            }
            if (op == 0x05) {   /* goto continues this path */
                if (depth >= SW_MAX_DEPTH) break;
                a = rd32(st, a + 1);
                depth++;
                continue;
            }
            if (op == 0x04 || op == 0x06 || op == 0x07) {
                uint32_t tgt = op == 0x04 ? rd32(st, a + 1) : rd32(st, a + 2);
                if (depth < SW_MAX_DEPTH) push(st, tgt, (uint8_t)(depth + 1), pf, npf);
            }
            if (is_end(op)) break;
            a += n;
        }
    }
}

/* choose_flag(): 0 when no usable flag. */
static uint16_t choose_flag(const SwState* st, const SwRaw* g, uint16_t hide_flag) {
    for (uint8_t i = 0; i < g->nafter; i++)
        if (g->after[i] >= SW_FLAG_MIN && checked(st, g->after[i])) return g->after[i];
    for (uint8_t i = 0; i < g->nafter; i++)
        if (g->after[i] >= SW_FLAG_MIN) return g->after[i];
    for (uint8_t i = g->npre; i-- > 0;)
        if (g->pre[i] >= SW_FLAG_MIN && checked(st, g->pre[i])) return g->pre[i];
    if (g->self_remove && hide_flag) return hide_flag;
    return 0;
}

void script_walk_gifts(const GameMem* m, uint32_t script, uint8_t local_id, uint16_t hide_flag,
                       ScriptGifts* out) {
    static SwState st;   /* ~6 KB; the game layer runs on one thread */
    uint16_t pair_item[SW_RAW_GIFTS], pair_flag[SW_RAW_GIFTS];
    uint32_t npairs = 0, nflags = 0;
    uint16_t flags[SW_RAW_GIFTS];

    memset(out, 0, sizeof(*out));
    memset(&st, 0, sizeof(st));
    st.m = m;
    st.lid = local_id;
    walk(&st, script);

    /* predict(): constant item, story flag, unique (item, flag) pairs in walk order. */
    for (uint32_t i = 0; i < st.nraw; i++) {
        const SwRaw* g = &st.raw[i];
        uint16_t f = choose_flag(&st, g, hide_flag);
        if (g->item >= SW_DYNAMIC || f < SW_FLAG_MIN || f >= SW_FLAG_END) continue;
        bool dup = false;
        for (uint32_t k = 0; k < npairs; k++) dup = dup || (pair_item[k] == g->item && pair_flag[k] == f);
        if (dup) continue;
        pair_item[npairs] = g->item;
        pair_flag[npairs++] = f;
        bool known = false;
        for (uint32_t k = 0; k < nflags; k++) known = known || flags[k] == f;
        if (!known) flags[nflags++] = f;
    }
    if (nflags > SW_MAX_GIFTS) return;   /* menu or condition-picked rewards */
    for (uint32_t k = 0; k < nflags; k++) {
        ScriptGift* gift = &out->gift[out->count++];
        gift->flag = flags[k];
        for (uint32_t i = 0; i < npairs && gift->item_count < 2; i++)
            if (pair_flag[i] == flags[k]) gift->item[gift->item_count++] = pair_item[i];
    }
}
