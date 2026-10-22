#include "rando/rando.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define ROM_BASE 0x08000000u
#define SPECIES_MAX 411
#define TRAINER_LAST 850 /* 851-854 are Red/Leaf/Brendan/May placeholders (R7) */
#define LEARNSET_MAX 128 /* entry cap so a bad pointer can't walk the whole ROM */

typedef struct {
    uint8_t* rom;
    uint32_t size;
} Rom;

/* Pointer to len bytes at GBA address addr, or NULL when any of it is outside the ROM. */
static uint8_t* at(const Rom* r, uint32_t addr, uint32_t len) {
    if (addr < ROM_BASE) return NULL;
    uint32_t off = addr - ROM_BASE;
    if (off > r->size || len > r->size - off) return NULL;
    return r->rom + off;
}

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* murmur3 fmix32 */
static uint32_t mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

typedef struct {
    uint16_t s[SPECIES_MAX];
    uint16_t n;
} Pool;

/* R5: the roll depends only on the seed and where the field sits, never on what it held. */
static void roll(const Rom* r, uint8_t* field, const Pool* pool, uint32_t seed) {
    uint32_t off = (uint32_t)(field - r->rom);
    wr16(field, pool->s[mix32(seed ^ off) % pool->n]);
}

static bool is_excluded(uint16_t s) {
    static const uint16_t legends[] = {144, 145, 146, 150, 151, 243, 244, 245, 249, 250, 251};
    if (s >= 252 && s <= 276) return true; /* Old Unown placeholders */
    if (s >= 401 && s <= 410) return true; /* Hoenn legendaries */
    for (size_t i = 0; i < sizeof(legends) / sizeof(legends[0]); i++)
        if (legends[i] == s) return true;
    return false;
}

/* gLevelUpLearnsets[species], or NULL. */
static const uint8_t* learnset(const Rom* r, const GameProfile* p, uint16_t species) {
    const uint8_t* ptr = at(r, p->level_up_learnsets + 4u * species, 4);
    return ptr ? at(r, rd32(ptr), 2) : NULL;
}

/* Next learnset entry (level << 9 | move) at index i, or 0xFFFF at the end or out of bounds. */
static uint16_t learnset_entry(const Rom* r, const uint8_t* ls, uint32_t i) {
    if (!ls || i >= LEARNSET_MAX) return 0xFFFF;
    uint32_t addr = (uint32_t)(ls - r->rom) + ROM_BASE + 2u * i;
    const uint8_t* e = at(r, addr, 2);
    return e ? rd16(e) : 0xFFFF;
}

static bool has_early_damage(const Rom* r, const GameProfile* p, uint16_t species) {
    const uint8_t* ls = learnset(r, p, species);
    for (uint32_t i = 0;; i++) {
        uint16_t e = learnset_entry(r, ls, i);
        if (e == 0xFFFF) return false;
        if ((e >> 9) > 5) continue;
        const uint8_t* mv = at(r, p->battle_moves + 12u * (e & 0x1FF), 12);
        /* Counter, Bide, Mirror Coat only deal damage back, so they can't win the Route 103 rival fight. */
        uint16_t id = e & 0x1FF;
        if (mv && mv[1] > 0 && id != 68 && id != 117 && id != 243) return true;
    }
}

/* GiveBoxMonInitialMoveset: learn every move up to level, skipping known ones,
 * dropping slot 0 when all four are full. */
static void initial_moves(const Rom* r, const GameProfile* p, uint16_t species, uint8_t level, uint8_t* out) {
    uint16_t moves[4] = {0, 0, 0, 0};
    const uint8_t* ls = learnset(r, p, species);
    for (uint32_t i = 0;; i++) {
        uint16_t e = learnset_entry(r, ls, i);
        if (e == 0xFFFF || (e >> 9) > level) break;
        uint16_t move = e & 0x1FF;
        int slot = 0;
        while (slot < 4 && moves[slot] != 0 && moves[slot] != move) slot++;
        if (slot < 4) {
            if (moves[slot] == 0) moves[slot] = move;
            continue;
        }
        memmove(moves, moves + 1, 3 * sizeof(moves[0]));
        moves[3] = move;
    }
    for (int k = 0; k < 4; k++) wr16(out + 2 * k, moves[k]);
}

static void rando_wild(const Rom* r, const GameProfile* p, const Pool* pool, uint32_t seed) {
    static const uint8_t slots[4] = {12, 5, 5, 10}; /* land, water, rock smash, fishing */
    for (uint32_t h = 0; h < p->wild_header_cap; h++) {
        const uint8_t* hdr = at(r, p->wild_mon_headers + 20u * h, 20);
        if (!hdr || hdr[0] == 0xFF) break;
        for (int k = 0; k < 4; k++) {
            const uint8_t* info = at(r, rd32(hdr + 4 + 4 * k), 8);
            uint8_t* mons = info ? at(r, rd32(info + 4), 4u * slots[k]) : NULL;
            if (!mons) continue;
            for (int s = 0; s < slots[k]; s++) roll(r, mons + 4 * s + 2, pool, seed);
        }
    }
}

static void rando_trainers(const Rom* r, const GameProfile* p, const Pool* pool, uint32_t seed) {
    for (uint32_t t = 1; t <= TRAINER_LAST && t < p->trainer_count; t++) {
        const uint8_t* tr = at(r, p->trainers + 40u * t, 40);
        if (!tr) continue;
        uint8_t flags = tr[0x00], size = tr[0x20];
        uint32_t stride = (flags & 1) ? 16 : 8;
        uint8_t* party = at(r, rd32(tr + 0x24), stride * size);
        if (size < 1 || size > 6 || !party) continue;
        for (uint32_t m = 0; m < size; m++) {
            uint8_t* mon = party + stride * m;
            roll(r, mon + 4, pool, seed);
            if (flags & 1) initial_moves(r, p, rd16(mon + 4), mon[2], mon + 6 + (flags & 2));
        }
    }
}

bool rando_supported(const GameProfile* p) {
    return p && p->rules->region == GAME_REGION_HOENN && p->starter_mons != 0;
}

void rando_apply(uint8_t* rom, uint32_t rom_size, const GameProfile* p, uint32_t seed) {
    if (!rom || !rando_supported(p)) return;
    Rom r = {rom, rom_size};
    Pool base = {{0}, 0}, trainer = {{0}, 0}, starter = {{0}, 0};
    for (uint16_t s = 1; s <= SPECIES_MAX; s++) {
        if (is_excluded(s)) continue;
        base.s[base.n++] = s;
        if (s != 303) trainer.s[trainer.n++] = s; /* Shedinja (R9) */
        if (has_early_damage(&r, p, s)) starter.s[starter.n++] = s;
    }
    rando_wild(&r, p, &base, seed);
    rando_trainers(&r, p, &trainer, seed);
    uint8_t* st = at(&r, p->starter_mons, 6);
    if (st && starter.n > 0)
        for (int i = 0; i < 3; i++) roll(&r, st + 2 * i, &starter, seed);
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool rando_parse(const char* text, uint32_t* seed) {
    if (!text || strncmp(text, "seed ", 5) != 0) return false;
    uint32_t v = 0;
    for (int i = 0; i < 8; i++) {
        int d = hex_digit(text[5 + i]);
        if (d < 0) return false;
        v = (v << 4) | (uint32_t)d;
    }
    const char* rest = text + 13;
    if (strncmp(rest, " v1", 3) != 0) return false;
    for (rest += 3; *rest; rest++)
        if (*rest != '\n' && *rest != '\r' && *rest != ' ' && *rest != '\t') return false;
    *seed = v;
    return true;
}

void rando_format(char* out, size_t n, uint32_t seed) {
    snprintf(out, n, "seed %08X v1\n", (unsigned)seed);
}

static bool rando_path(char* out, size_t n, const char* save_path) {
    if (!save_path) return false;
    int len = snprintf(out, n, "%s%s", save_path, RANDO_FILE_EXT);
    return len > 0 && (size_t)len < n;
}

bool rando_load(const char* save_path, uint32_t* seed) {
    char path[1024], line[64];
    if (!rando_path(path, sizeof(path), save_path)) return false;
    FILE* f = fopen(path, "r");
    if (!f) return false;
    bool ok = fgets(line, sizeof(line), f) != NULL && rando_parse(line, seed);
    fclose(f);
    return ok;
}

bool rando_store(const char* save_path, uint32_t seed) {
    char path[1024], line[32];
    if (!rando_path(path, sizeof(path), save_path)) return false;
    FILE* f = fopen(path, "w");
    if (!f) return false;
    rando_format(line, sizeof(line), seed);
    bool ok = fputs(line, f) >= 0;
    return fclose(f) == 0 && ok;
}

bool rando_clear(const char* save_path) {
    char path[1024];
    if (!rando_path(path, sizeof(path), save_path)) return false;
    return remove(path) == 0 || errno == ENOENT;
}
