#include "game/feebas.h"
#include <string.h>

/* Game-code constants (pret src/wild_encounter.c, include/global.h,
 * include/constants/metatile_behaviors.h, src/metatile_behavior.c). */
#define NUM_FISHING_SPOTS  447    /* 131 + 167 + 149 */
#define SECTION_COUNT      3
#define SB1_LOC_GROUP      0x04
#define SB1_LOC_NUM        0x05
#define SB1_TREND_SEED     0x2E6A /* dewfordTrends[0].rand */
#define MAP_HEADER_LAYOUT  0x00
#define LAYOUT_WIDTH       0x00
#define LAYOUT_HEIGHT      0x04
#define LAYOUT_PRIMARY     0x10
#define LAYOUT_SECONDARY   0x14
#define TILESET_ATTRS      0x10
#define MAP_OFFSET         7
#define MAP_OFFSET_W       15
#define MAP_OFFSET_H       14
#define VMAP_MAX_CELLS     10240
#define MAX_ROW            256
#define GRID_UNDEFINED     0x03FF
#define GRID_COLLISION     0x0C00
#define PRIMARY_METATILES  512
#define TILE_FLAG_SURFABLE 0x02
#define MB_WATERFALL       0x13
#define MB_BRIDGE_FIRST    0x70   /* MB_BRIDGE_OVER_OCEAN .. MB_BRIDGE_OVER_POND_HIGH */
#define MB_BRIDGE_LAST     0x73

typedef struct {
    const GameMem* m;
    uint32_t grid;
    int32_t gw, lw, lh;
    uint32_t attrs[2];
    uint8_t bits[256];          /* sTileBitAttributes, indexed by behavior as the game does */
} FbMap;

void feebas_spot_ids(uint16_t seed, uint16_t out[FEEBAS_SPOT_COUNT]) {
    uint32_t state = seed;
    for (int i = 0; i < FEEBAS_SPOT_COUNT;) {
        state = state * 1103515245u + 12345u;
        uint16_t spot = (uint16_t)((uint16_t)(state >> 16) % NUM_FISHING_SPOTS);
        if (spot == 0) spot = NUM_FISHING_SPOTS;
        /* 1..3 are unfishable spots at the top of the map; the game draws again. */
        if (spot >= 4) out[i++] = spot;
    }
}

static bool map_init(const GameProfile* p, const GameMem* m, FbMap* f) {
    uint32_t w = 0, h = 0, grid = 0, layout = 0, lw = 0, lh = 0, prim = 0, sec = 0;
    f->m = m;
    if (!game_mem_read32(m, p->backup_map_layout, &w) || !game_mem_read32(m, p->backup_map_layout + 4, &h) ||
        !game_mem_read32(m, p->backup_map_layout + 8, &grid) || grid != p->backup_map_data ||
        w == 0 || h == 0 || w > VMAP_MAX_CELLS || h > VMAP_MAX_CELLS || w * h > VMAP_MAX_CELLS ||
        !game_mem_ok(m, grid, w * h * 2u))
        return false;
    if (!game_mem_read32(m, p->map_header + MAP_HEADER_LAYOUT, &layout) ||
        !game_mem_read32(m, layout + LAYOUT_WIDTH, &lw) || !game_mem_read32(m, layout + LAYOUT_HEIGHT, &lh) ||
        !game_mem_read32(m, layout + LAYOUT_PRIMARY, &prim) || !game_mem_read32(m, layout + LAYOUT_SECONDARY, &sec) ||
        !game_mem_read32(m, prim + TILESET_ATTRS, &f->attrs[0]) ||
        !game_mem_read32(m, sec + TILESET_ATTRS, &f->attrs[1]) ||
        !game_mem_copy(m, p->tile_bit_attributes, f->bits, sizeof(f->bits)))
        return false;
    /* The VMap must be this layout's (InitMapLayoutData); during a map change it may not be yet. */
    if (lw == 0 || lw > MAX_ROW || w != lw + MAP_OFFSET_W || h != lh + MAP_OFFSET_H) return false;
    f->grid = grid;
    f->gw = (int32_t)w;
    f->lw = (int32_t)lw;
    f->lh = (int32_t)lh;
    return true;
}

/* Grid word at map coords (x, y); the caller keeps them inside the layout. */
static bool block_at(const FbMap* f, int32_t x, int32_t y, uint16_t* block) {
    return game_mem_read16(f->m, f->grid + 2u * (uint32_t)((y + MAP_OFFSET) * f->gw + x + MAP_OFFSET), block);
}

/* GetMetatileAttributesById. An undefined cell never occurs inside Route 119; treat it as land. */
static uint8_t behavior_of(const FbMap* f, uint16_t block) {
    uint16_t id = block & 0x03FF, attr = 0xFF;
    if (id == GRID_UNDEFINED) return 0xFF;
    uint32_t table = id < PRIMARY_METATILES ? f->attrs[0] : f->attrs[1];
    uint32_t index = id < PRIMARY_METATILES ? id : (uint32_t)(id - PRIMARY_METATILES);
    if (!game_mem_read16(f->m, table + 2u * index, &attr)) return 0xFF;
    return (uint8_t)(attr & 0xFF);
}

/* MetatileBehavior_IsSurfableAndNotWaterfall: what GetFeebasFishingSpotId counts. */
static bool counted(const FbMap* f, uint8_t b) {
    return (f->bits[b] & TILE_FLAG_SURFABLE) && b != MB_WATERFALL;
}

static bool is_spot(const uint16_t spots[FEEBAS_SPOT_COUNT], uint16_t id) {
    for (int i = 0; i < FEEBAS_SPOT_COUNT; i++)
        if (spots[i] == id) return true;
    return false;
}

static void add_tile(FeebasState* s, int32_t x, int32_t y, uint16_t spot, bool fishable, bool bridge) {
    for (uint8_t i = 0; i < s->count; i++)
        if (s->tile[i].x == x && s->tile[i].y == y) return;
    if (s->count >= FEEBAS_MAX_TILES) return;
    s->tile[s->count++] = (FeebasTile){(int16_t)x, (int16_t)y, spot, fishable, bridge};
}

/* Water a surfer can sit on: counted and no collision (CanFish while surfing). */
static bool surf_water(const FbMap* f, int32_t x, int32_t y) {
    uint16_t block = 0;
    if (x < 0 || y < 0 || x >= f->lw || y >= f->lh || !block_at(f, x, y, &block)) return false;
    return (block & GRID_COLLISION) == 0 && counted(f, behavior_of(f, block));
}

/* GetFeebasFishingSpotId over one section, in the game's order. A bridge tile is not
 * counted, so it gets the section's fall-through id; it is only listed when a surfer
 * can sit next to it. */
static bool scan_section(const FbMap* f, const uint16_t sec[3], FeebasState* s) {
    uint16_t id = sec[2], block = 0;
    for (int32_t y = sec[0]; y <= sec[1]; y++)
        for (int32_t x = 0; x < f->lw; x++) {
            if (!block_at(f, x, y, &block)) return false;
            if (!counted(f, behavior_of(f, block))) continue;
            id++;
            if (is_spot(s->spots, id)) add_tile(s, x, y, id, (block & GRID_COLLISION) == 0, false);
        }
    uint16_t fall = (uint16_t)(id + 1);
    if (!is_spot(s->spots, fall)) return true;
    for (int32_t y = sec[0]; y <= sec[1]; y++)
        for (int32_t x = 0; x < f->lw; x++) {
            if (!block_at(f, x, y, &block)) return false;
            uint8_t b = behavior_of(f, block);
            if (b < MB_BRIDGE_FIRST || b > MB_BRIDGE_LAST) continue;
            if (surf_water(f, x - 1, y) || surf_water(f, x + 1, y) || surf_water(f, x, y - 1) ||
                surf_water(f, x, y + 1))
                add_tile(s, x, y, fall, true, true);
        }
    return true;
}

bool feebas_compute(const GameProfile* p, const GameMem* m, uint16_t seed, FeebasState* out) {
    static FbMap f;   /* 256-byte table; keep it off the stack */
    uint32_t sb1 = 0;
    uint8_t loc[2];
    uint16_t sec[SECTION_COUNT][3];
    FeebasState s;

    memset(out, 0, sizeof(*out));
    if (!p || !game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_in_ewram(sb1, SB1_TREND_SEED + 2) ||
        !game_mem_copy(m, sb1 + SB1_LOC_GROUP, loc, sizeof(loc)) || loc[0] != p->feebas_map_group ||
        loc[1] != p->feebas_map_num)
        return false;
    memset(&f, 0, sizeof(f));
    if (!map_init(p, m, &f)) return false;
    for (int i = 0; i < SECTION_COUNT; i++)
        for (int k = 0; k < 3; k++)
            if (!game_mem_read16(m, p->feebas_water_tiles + 2u * (uint32_t)(i * 3 + k), &sec[i][k])) return false;
    for (int i = 0; i < SECTION_COUNT; i++)
        if (sec[i][0] > sec[i][1] || sec[i][1] >= f.lh) return false;

    memset(&s, 0, sizeof(s));
    s.seed = seed;
    feebas_spot_ids(seed, s.spots);
    for (int i = 0; i < SECTION_COUNT; i++)
        if (!scan_section(&f, sec[i], &s)) return false;
    s.active = true;
    memcpy(out, &s, sizeof(s));
    return true;
}

bool feebas_read(const GameProfile* p, const GameMem* m, FeebasState* out) {
    uint32_t sb1 = 0;
    uint16_t seed = 0;
    memset(out, 0, sizeof(*out));
    if (!p || !game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_in_ewram(sb1, SB1_TREND_SEED + 2) ||
        !game_mem_read16(m, sb1 + SB1_TREND_SEED, &seed))
        return false;
    return feebas_compute(p, m, seed, out);
}

bool feebas_dump(const FeebasState* s, FILE* out) {
    if (fprintf(out, "[feebas]\nactive %d\n", s->active ? 1 : 0) < 0) return false;
    if (!s->active) return true;
    if (fprintf(out, "seed %04X\nspots %u,%u,%u,%u,%u,%u\n", (unsigned)s->seed, (unsigned)s->spots[0],
                (unsigned)s->spots[1], (unsigned)s->spots[2], (unsigned)s->spots[3], (unsigned)s->spots[4],
                (unsigned)s->spots[5]) < 0)
        return false;
    for (uint8_t i = 0; i < s->count; i++) {
        const FeebasTile* t = &s->tile[i];
        if (fprintf(out, "tile x=%d y=%d spot=%u fishable=%d bridge=%d\n", t->x, t->y, (unsigned)t->spot,
                    t->fishable ? 1 : 0, t->bridge ? 1 : 0) < 0)
            return false;
    }
    return true;
}
