#include "game/species_search.h"
#include "game/emerald_layout.h"
#include <string.h>
#include "game/encounters.h"
#include "game/game.h"
#include "game/gen3_text.h"

_Static_assert(SPECIES_NAME_LEN == GAME_NAME_LEN, "species name buffers must match GameSnapshot");
_Static_assert(SPECIES_MAP_NAME_LEN == GAME_MAP_NAME_LEN, "map name buffers must match game_mapsec_name");

#define NAME_BYTES    11
#define NATIONAL_MAX  386
#define MAPS_MAX       256

void species_search_normalize(const char* in, char* out, size_t out_size) {
    size_t n = 0;
    if (out_size == 0) return;
    for (; *in && n + 1 < out_size; in++) {
        char c = *in;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
    }
    out[n] = '\0';
}

bool species_index_is_wild(const SpeciesIndex* ix, uint16_t species) {
    return species < SPECIES_MAX && (ix->wild[species >> 3] & (1u << (species & 7))) != 0;
}

static void mark_wild(SpeciesIndex* ix, uint16_t species) {
    if (species < SPECIES_MAX) ix->wild[species >> 3] |= (uint8_t)(1u << (species & 7));
}

/* Visits each reachable wild header once: only the first header per (group,num) counts, as in
 * the game's GetCurrentMapWildMonHeaderId. Stops at the terminator or a bad read. */
typedef void (*HeaderVisit)(void* ctx, uint32_t header_index, uint8_t group, uint8_t num);

static void for_each_reachable_header(const GameProfile* p, const GameMem* rom, HeaderVisit visit,
                                      void* ctx) {
    uint16_t maps[MAPS_MAX];
    uint16_t seen = 0;
    for (uint16_t i = 0; i < p->wild_header_cap; i++) {
        uint8_t group, num;
        EncSlot slots[12];
        if (encounters_header_slots(rom, p->wild_mon_headers, i, p->species_count, ENC_LAND,
                                    &group, &num, slots) < 0)
            return;
        uint16_t pair = (uint16_t)((group << 8) | num);
        bool dup = false;
        for (uint16_t k = 0; k < seen; k++) dup = dup || maps[k] == pair;
        if (dup) continue;
        if (seen < MAPS_MAX) maps[seen++] = pair;
        visit(ctx, i, group, num);
    }
}

typedef struct {
    SpeciesIndex* ix;
    const GameProfile* p;
    const GameMem* rom;
} WildCtx;

static void wild_visit(void* vctx, uint32_t index, uint8_t group, uint8_t num) {
    WildCtx* c = vctx;
    (void)group; (void)num;
    for (int m = 0; m < ENC_METHOD_COUNT; m++) {
        uint8_t g, n;
        EncSlot slots[12];
        int cnt = encounters_header_slots(c->rom, c->p->wild_mon_headers, (uint16_t)index,
                                          c->p->species_count, (EncMethod)m, &g, &n, slots);
        for (int k = 0; k < cnt; k++) mark_wild(c->ix, slots[k].species);
    }
}

static bool read_species(SpeciesIndex* ix, const GameProfile* p, const GameMem* rom, uint16_t sp) {
    uint8_t raw[NAME_BYTES];
    uint16_t nat;
    if (!game_mem_copy(rom, p->species_names + (uint32_t)NAME_BYTES * sp, raw, sizeof(raw)) ||
        !game_mem_read16(rom, p->species_to_national + 2u * (uint32_t)(sp - 1), &nat))
        return false;
    size_t len = 0;
    while (len < NAME_BYTES && raw[len] != 0xFF) len++;
    uint8_t mark = 0;
    if (len > 0 && raw[len - 1] == 0xB6) mark = 1;
    else if (len > 0 && raw[len - 1] == 0xB5) mark = 2;
    if (mark) len--;
    gen3_decode(raw, len, ix->name[sp], sizeof(ix->name[sp]));
    ix->gender_mark[sp] = mark;
    char tmp[SPECIES_KEY_LEN + 4];
    species_search_normalize(ix->name[sp], tmp, sizeof(tmp));
    size_t kl = strlen(tmp);
    if (kl > SPECIES_KEY_LEN - 2) kl = SPECIES_KEY_LEN - 2;
    memcpy(ix->key[sp], tmp, kl);
    if (mark) ix->key[sp][kl++] = mark == 1 ? 'f' : 'm';
    ix->key[sp][kl] = '\0';
    if (nat >= 1 && nat <= NATIONAL_MAX && strcmp(ix->name[sp], "?") != 0) ix->national[sp] = nat;
    return true;
}

void species_index_build(SpeciesIndex* ix, const GameProfile* p, const GameMem* rom) {
    memset(ix, 0, sizeof(*ix));
    if (!p || p->species_count == 0 || p->species_count > SPECIES_MAX) return;

    for (uint16_t sp = 1; sp < p->species_count; sp++)
        if (!read_species(ix, p, rom, sp)) return;

    uint16_t by_nat[NATIONAL_MAX + 1] = {0};
    for (uint16_t sp = 1; sp < p->species_count; sp++)
        if (ix->national[sp] && !by_nat[ix->national[sp]]) by_nat[ix->national[sp]] = sp;
    for (uint16_t n = 1; n <= NATIONAL_MAX; n++)
        if (by_nat[n]) ix->order[ix->count++] = by_nat[n];

    for (uint16_t sp = 1; sp < p->species_count; sp++) {
        for (uint32_t e = 0; e < EVOS_PER_MON; e++) {
            uint8_t evo[EVO_ENTRY_SIZE];
            if (!game_mem_copy(rom, p->evolution_table + (sp * EVOS_PER_MON + e) * EVO_ENTRY_SIZE,
                               evo, sizeof(evo)))
                return;
            uint16_t method = (uint16_t)(evo[0] | (evo[1] << 8));
            uint16_t target = (uint16_t)(evo[4] | (evo[5] << 8));
            if (method != 0 && target > 0 && target < p->species_count && !ix->parent[target])
                ix->parent[target] = sp;
        }
    }

    WildCtx wc = { ix, p, rom };
    for_each_reachable_header(p, rom, wild_visit, &wc);

    uint8_t fb[4];
    if (!game_mem_copy(rom, p->wild_feebas, fb, sizeof(fb))) return;
    mark_wild(ix, (uint16_t)(fb[2] | (fb[3] << 8)));
    ix->ok = true;
}

uint16_t species_search_query(const SpeciesIndex* ix, const char* query, uint16_t out[SPECIES_MAX]) {
    char q[SPECIES_KEY_LEN + 4];
    species_search_normalize(query, q, sizeof(q));
    size_t ql = strlen(q);
    uint16_t n = 0;
    if (ql == 0) {
        for (uint16_t i = 0; i < ix->count; i++) out[n++] = ix->order[i];
        return n;
    }
    for (int pass = 0; pass < 3; pass++) {
        for (uint16_t i = 0; i < ix->count; i++) {
            uint16_t sp = ix->order[i];
            const char* k = ix->key[sp];
            bool exact = strcmp(k, q) == 0;
            bool prefix = strncmp(k, q, ql) == 0;
            bool hit = pass == 0 ? exact : pass == 1 ? (prefix && !exact)
                                                     : (!prefix && strstr(k, q) != NULL);
            if (hit) out[n++] = sp;
        }
    }
    return n;
}

/* ---- Detail: how to get a species ---- */

/* struct MapHeader.regionMapSectionId (pret include/global.fieldmap.h), as in game.c. */
#define MAPSEC_UNKNOWN 0xFF
#define EVO_METHOD_TRADE_ITEM 6
#define EVO_METHOD_ITEM 7
_Static_assert(SROW_ANY_ROD < 8, "row key packs method in 3 bits");
#define ROW_KEY_BYTES 512 /* bitset over method (3 bits), mapsec (8), postgame (1) */

/* Region map section of map (group, num): gMapGroups[group][num]->regionMapSectionId. Group
 * lengths come from the pointer gaps; the last group ends where gMapGroups starts. */
static bool map_sec(const GameProfile* p, const GameMem* rom, uint8_t group, uint8_t num,
                    uint8_t* sec) {
    uint32_t list = 0, next = p->map_groups, header = 0;
    if (group >= p->map_group_count || !game_mem_read32(rom, p->map_groups + 4u * group, &list))
        return false;
    if (group + 1u < p->map_group_count &&
        !game_mem_read32(rom, p->map_groups + 4u * (group + 1u), &next))
        return false;
    if (next <= list || num >= (next - list) / 4u) return false;
    return game_mem_read32(rom, list + 4u * num, &header) &&
           game_mem_read8(rom, header + MAP_HEADER_REGION_MAP_SEC, sec);
}

/* Reads evolution entry e of `from`; false on a bad read. */
static bool evo_read(const GameProfile* p, const GameMem* rom, uint16_t from, uint32_t e,
                     uint16_t* method, uint16_t* param, uint16_t* target) {
    uint8_t evo[EVO_ENTRY_SIZE];
    if (!game_mem_copy(rom, p->evolution_table + (from * EVOS_PER_MON + e) * EVO_ENTRY_SIZE, evo,
                       sizeof(evo)))
        return false;
    *method = (uint16_t)(evo[0] | (evo[1] << 8));
    *param = (uint16_t)(evo[2] | (evo[3] << 8));
    *target = (uint16_t)(evo[4] | (evo[5] << 8));
    return true;
}

typedef struct {
    const GameProfile* p;
    const GameMem* rom;
    SpeciesDetail* out;
    uint8_t pass;                  /* row method collected by this walk */
    uint8_t seen[ROW_KEY_BYTES];   /* merge keys already counted in row_total */
} RowCtx;

static void add_row(RowCtx* c, uint8_t method, uint8_t sec, uint8_t flags, uint8_t lo, uint8_t hi,
                    uint8_t pct) {
    SpeciesDetail* d = c->out;
    uint16_t key = (uint16_t)((method << 9) | (sec << 1) | ((flags & SROW_POSTGAME) ? 1 : 0));
    uint8_t bit = (uint8_t)(1u << (key & 7));
    if (!(c->seen[key >> 3] & bit)) {
        c->seen[key >> 3] |= bit;
        d->row_total++;
        if (d->row_count >= SPECIES_ROWS_MAX) return;
        SpeciesRow* r = &d->rows[d->row_count++];
        r->method = method;
        r->mapsec = sec;
        game_mapsec_name(c->rom, c->p, sec, r->place);
        r->min_level = lo;
        r->max_level = hi;
        r->pct_lo = r->pct_hi = pct;
        r->flags = flags;
        return;
    }
    for (uint16_t i = 0; i < d->row_count; i++) {
        SpeciesRow* r = &d->rows[i];
        if (r->method != method || r->mapsec != sec ||
            (r->flags & SROW_POSTGAME) != (flags & SROW_POSTGAME))
            continue;
        if (lo < r->min_level) r->min_level = lo;
        if (hi > r->max_level) r->max_level = hi;
        if (pct < r->pct_lo) r->pct_lo = pct;
        if (pct > r->pct_hi) r->pct_hi = pct;
        r->flags |= flags;
        return;
    }
    /* Key seen but its row was truncated: already counted. */
}

static void row_visit(void* vctx, uint32_t index, uint8_t group, uint8_t num) {
    RowCtx* c = vctx;
    const GameProfile* p = c->p;
    uint8_t sec;
    if (!map_sec(p, c->rom, group, num, &sec)) sec = MAPSEC_UNKNOWN;
    uint8_t base_flags = sec == p->low_tide_mapsec ? SROW_LOW_TIDE : 0;
    for (size_t i = 0; i < sizeof(p->postgame_map) / sizeof(p->postgame_map[0]); i++)
        if (p->postgame_map[i][0] == group && p->postgame_map[i][1] == num) base_flags |= SROW_POSTGAME;

    for (int m = 0; m < ENC_METHOD_COUNT; m++) {
        uint8_t method = (uint8_t)m, row_sec = sec, flags = base_flags;
        if (m == ENC_WATER)
            for (size_t i = 0; i < sizeof(p->dive_map) / sizeof(p->dive_map[0]); i++)
                if (p->dive_map[i][0] == group && p->dive_map[i][1] == num) {
                    method = SROW_DIVE;
                    row_sec = p->dive_surface_mapsec[i];
                }
        if (m == ENC_LAND && group == p->mirage_map_group && num == p->mirage_map_num) {
            flags |= SROW_MIRAGE;
            row_sec = p->mirage_mapsec;
        }
        if (method != c->pass) continue;

        uint8_t g, n, lo = 0xFF, hi = 0;
        uint16_t pct = 0;
        EncSlot slots[12];
        int cnt = encounters_header_slots(c->rom, p->wild_mon_headers, (uint16_t)index,
                                          p->species_count, (EncMethod)m, &g, &n, slots);
        for (int k = 0; k < cnt; k++) {
            if (slots[k].species != c->out->rows_species) continue;
            pct = (uint16_t)(pct + slots[k].percent);
            if (slots[k].min_level < lo) lo = slots[k].min_level;
            if (slots[k].max_level > hi) hi = slots[k].max_level;
        }
        if (pct > 0) add_row(c, method, row_sec, flags, lo, hi, (uint8_t)pct);
    }
}

static void collect_rows(const GameProfile* p, const GameMem* rom, SpeciesDetail* out) {
    static const uint8_t k_order[] = {ENC_LAND, ENC_WATER, SROW_DIVE, ENC_ROCK_SMASH,
                                      ENC_OLD_ROD, ENC_GOOD_ROD, ENC_SUPER_ROD, SROW_ANY_ROD};
    RowCtx c;
    memset(&c, 0, sizeof(c));
    c.p = p;
    c.rom = rom;
    c.out = out;
    for (size_t i = 0; i < sizeof(k_order); i++) {
        c.pass = k_order[i];
        if (c.pass != SROW_ANY_ROD) {
            for_each_reachable_header(p, rom, row_visit, &c);
            continue;
        }
        /* sWildFeebas {u8 min, u8 max, u16 species}: any rod on hidden Route 119 tiles. */
        uint8_t fb[4], sec;
        if (!game_mem_copy(rom, p->wild_feebas, fb, sizeof(fb)) ||
            (uint16_t)(fb[2] | (fb[3] << 8)) != out->rows_species)
            continue;
        if (!map_sec(p, rom, p->feebas_map_group, p->feebas_map_num, &sec)) sec = MAPSEC_UNKNOWN;
        add_row(&c, SROW_ANY_ROD, sec, SROW_FEEBAS_TILES, fb[0], fb[1], 50);
    }
    /* Within each method, lowest region map section first; insertion sort keeps ties stable. */
    for (uint16_t i = 1; i < out->row_count; i++) {
        SpeciesRow r = out->rows[i];
        uint16_t j = i;
        while (j > 0 && out->rows[j - 1].method == r.method && out->rows[j - 1].mapsec > r.mapsec) {
            out->rows[j] = out->rows[j - 1];
            j--;
        }
        out->rows[j] = r;
    }
}

void species_search_detail(const SpeciesIndex* ix, const GameProfile* p, const GameMem* rom,
                           uint16_t species, SpeciesDetail* out) {
    memset(out, 0, sizeof(*out));
    out->species = species;
    out->kind = SPECIES_NOT_WILD;
    if (!ix || !ix->ok || !p || species == 0 || species >= p->species_count) return;

    if (species_index_is_wild(ix, species)) {
        out->kind = SPECIES_WILD;
        out->rows_species = species;
    } else {
        /* Walk pre-evolutions, at most 3 hops so a malformed table cannot loop. */
        uint16_t cur = species;
        for (int hop = 0; hop < 3 && ix->parent[cur]; hop++) {
            uint16_t parent = ix->parent[cur];
            SpeciesStep* st = &out->steps[out->step_count++];
            st->from = parent;
            for (uint32_t e = 0; e < EVOS_PER_MON; e++) {
                uint16_t method, param, target;
                if (!evo_read(p, rom, parent, e, &method, &param, &target)) break;
                if (method == 0 || target != cur) continue;
                st->method = method;
                st->param = param;
                if (method == EVO_METHOD_TRADE_ITEM || method == EVO_METHOD_ITEM)
                    game_item_name(rom, p, param, st->item);
                break;
            }
            if (species_index_is_wild(ix, parent)) {
                out->kind = SPECIES_EVOLVE;
                out->rows_species = parent;
                break;
            }
            cur = parent;
        }
        if (out->kind != SPECIES_EVOLVE && out->step_count > 0) {
            out->steps_truncated = ix->parent[cur] != 0;
            out->kind = SPECIES_EVOLVE_NOT_WILD;
        } else if (out->step_count == 0) {
            /* No pre-evolution: a baby that hatches from eggs of its wild evolution. */
            for (uint32_t e = 0; e < EVOS_PER_MON; e++) {
                uint16_t method, param, target;
                if (!evo_read(p, rom, species, e, &method, &param, &target)) break;
                if (method == 0 || !species_index_is_wild(ix, target)) continue;
                out->kind = SPECIES_BREED;
                out->rows_species = target;
                for (size_t i = 0; i < sizeof(p->incense_baby) / sizeof(p->incense_baby[0]); i++)
                    if (p->incense_baby[i] == species)
                        game_item_name(rom, p, p->incense_item[i], out->incense);
                break;
            }
        }
    }
    if (out->kind == SPECIES_WILD || out->kind == SPECIES_EVOLVE || out->kind == SPECIES_BREED)
        collect_rows(p, rom, out);
}
