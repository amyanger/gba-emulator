#include "game/overworld.h"
#include <string.h>
#include "game/gen3_text.h"
#include "gba.h"

/* Struct offsets (pret/pokeemerald include/global.fieldmap.h, include/sprite.h,
 * include/global.h); see the overworld research docs for verification marks. */
#define OBJ_COUNT          16
#define OBJ_SIZE           0x24
#define OBJ_FLAGS0         0x00   /* bit0 active */
#define OBJ_FLAGS2         0x02   /* bit0 isPlayer */
#define OBJ_SPRITE         0x04
#define OBJ_TRAINER_TYPE   0x07
#define OBJ_LOCAL_ID       0x08
#define OBJ_MAP_NUM        0x09
#define OBJ_MAP_GROUP      0x0A
#define OBJ_ELEVATION      0x0B   /* low nibble: currentElevation */
#define OBJ_CUR            0x10   /* s16 x, y in VMap coords */
#define OBJ_PREV           0x14
#define OBJ_DIRS           0x18   /* low nibble: facingDirection 1..4 */
#define OBJ_RANGE          0x1D   /* trainerRange_berryTreeId */
#define OBJ_BEHAVIOR       0x1E   /* currentMetatileBehavior */
#define TRAINER_TYPE_NORMAL 1
#define TRAINER_TYPE_BURIED 3

#define SPR_COUNT          64
#define SPR_SIZE           0x44
#define SPR_X              0x20
#define SPR_Y              0x22
#define SPR_X2             0x24
#define SPR_Y2             0x26
#define SPR_CTC_Y          0x29   /* s8 centerToCornerVecY */

#define SB1_POS            0x00   /* s16 x, y: map coords of the screen's top-left tile */
#define SB1_LOC_GROUP      0x04
#define SB1_LOC_NUM        0x05
#define SB1_TEMPLATES      0xC70  /* ObjectEventTemplate[64] */
#define SB1_TEMPLATE_CAP   64
#define SB1_FLAGS          0x1270
#define SB1_OW_MIN_SIZE    (SB1_FLAGS + 300)

#define TEMPLATE_SIZE      0x18
#define TEMPLATE_SCRIPT    0x10
#define MAP_HEADER_SIZE    0x1C
#define MAP_HEADER_LAYOUT  0x00
#define MAP_HEADER_EVENTS  0x04
#define MAP_HEADER_CONNS   0x0C
#define LAYOUT_WIDTH       0x00
#define LAYOUT_HEIGHT      0x04
#define LAYOUT_PRIMARY     0x10
#define LAYOUT_SECONDARY   0x14
#define TILESET_ATTRS      0x10
#define EVENTS_OBJ_COUNT   0x00
#define EVENTS_BG_COUNT    0x03
#define EVENTS_OBJS        0x04
#define EVENTS_BGS         0x10
#define BG_SIZE            12
#define BG_KIND            0x05
#define BG_HIDDEN_ID       0x0A
#define BG_KIND_HIDDEN_ITEM 7
#define CONN_SIZE          12
#define CONN_CAP           8

#define FLAG_TRAINER_BASE  0x500
#define FLAG_HIDDEN_BASE   0x1F4
#define SCRIPT_TRAINERBATTLE 0x5C
#define TRAINER_SIZE       0x28
#define TRAINER_CLASS      0x01
#define CLASS_NAME_LEN     13
#define CHAR_MALE          0xB5
#define CHAR_FEMALE        0xB6

#define MAP_OFFSET         7
#define VMAP_MAX_CELLS     10240
#define GRID_UNDEFINED     0x03FF
#define GRID_COLLISION     0x0C00
#define PRIMARY_METATILES  512

/* Maps with their own trainer lookup (trainer_see.c): the Battle Pyramid floor
 * and the Trainer Hill floors (data/maps/map_groups.json). */
#define MAPGRP_FRONTIER    26
#define MAP_PYRAMID_FLOOR  26
#define MAP_HILL_FIRST     61
#define MAP_HILL_LAST      65

typedef struct {
    const GameProfile* p;
    const GameMem* m;
    uint32_t sb1;
    int16_t pos_x, pos_y;
    uint8_t loc_group, loc_num;
    int32_t cam_dx, cam_dy;          /* screen px added to (tile - pos) * 16 */
    int16_t spr_off_x, spr_off_y;    /* gSpriteCoordOffsetX/Y */
    bool grid_ok;
    int32_t grid_w, grid_h;
    uint32_t grid;
    uint32_t attrs[2];               /* primary, secondary metatile attribute tables */
    uint8_t objs[OBJ_COUNT][OBJ_SIZE];
    bool obj_ok[OBJ_COUNT];
} OwCtx;

static int16_t s16_at(const uint8_t* b) { return (int16_t)(b[0] | (b[1] << 8)); }

static bool flag_set(const OwCtx* c, uint32_t flag) {
    uint8_t v = 0;
    if (!game_mem_read8(c->m, c->sb1 + SB1_FLAGS + (flag >> 3), &v)) return false;
    return ((v >> (flag & 7)) & 1) != 0;
}

/* Sub-tile camera progress, from SetSpritePosToMapCoords (event_object_movement.c). */
static int32_t cam_adjust(int32_t f) { return -f + (f > 0 ? 16 : f < 0 ? -16 : 0); }

static void tile_to_screen(const OwCtx* c, int32_t vx, int32_t vy, int16_t* x, int16_t* y) {
    *x = (int16_t)((vx - c->pos_x) * 16 + c->cam_dx);
    *y = (int16_t)((vy - c->pos_y) * 16 + c->cam_dy);
}

static bool on_screen(int32_t x, int32_t y) {
    return x > -16 && x < SCREEN_WIDTH && y > -16 && y < SCREEN_HEIGHT;
}

static uint32_t map_header_rom(const OwCtx* c, uint8_t group, uint8_t num) {
    uint32_t list = 0, header = 0;
    if (group >= c->p->map_group_count ||
        !game_mem_read32(c->m, c->p->map_groups + 4u * group, &list) ||
        !game_mem_read32(c->m, list + 4u * num, &header) || header < 0x08000000u ||
        !game_mem_ok(c->m, header, MAP_HEADER_SIZE))
        return 0;
    return header;
}

/* GetObjectEventTemplateByLocalIdAndMap: SaveBlock1 copies for the current map,
 * the ROM header otherwise. Returns the script pointer, 0 when not found. */
static uint32_t template_script(const OwCtx* c, const uint8_t* o) {
    uint8_t lid = o[OBJ_LOCAL_ID], group = o[OBJ_MAP_GROUP], num = o[OBJ_MAP_NUM], n = 0;
    uint32_t events = 0, base = 0, count;
    if (group == c->loc_group && num == c->loc_num) {
        if (!game_mem_read32(c->m, c->p->map_header + MAP_HEADER_EVENTS, &events) ||
            !game_mem_read8(c->m, events + EVENTS_OBJ_COUNT, &n))
            return 0;
        base = c->sb1 + SB1_TEMPLATES;
        count = n < SB1_TEMPLATE_CAP ? n : SB1_TEMPLATE_CAP;
    } else {
        uint32_t header = map_header_rom(c, group, num);
        if (!header || !game_mem_read32(c->m, header + MAP_HEADER_EVENTS, &events) ||
            !game_mem_read8(c->m, events + EVENTS_OBJ_COUNT, &n) ||
            !game_mem_read32(c->m, events + EVENTS_OBJS, &base))
            return 0;
        count = n;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t t = base + i * TEMPLATE_SIZE, script = 0;
        uint8_t t_lid = 0;
        if (!game_mem_read8(c->m, t, &t_lid) || t_lid != lid) continue;
        return game_mem_read32(c->m, t + TEMPLATE_SCRIPT, &script) ? script : 0;
    }
    return 0;
}

static void class_name(const OwCtx* c, uint16_t id, char* out) {
    uint8_t cls = 0, raw[CLASS_NAME_LEN];
    if (id >= c->p->trainer_count ||
        !game_mem_read8(c->m, c->p->trainers + (uint32_t)id * TRAINER_SIZE + TRAINER_CLASS, &cls) ||
        cls >= c->p->trainer_class_count ||
        !game_mem_copy(c->m, c->p->trainer_class_names + (uint32_t)cls * CLASS_NAME_LEN, raw, CLASS_NAME_LEN))
        return;
    /* SWIMMER♂ and friends: the pill shows the class without the symbol. */
    for (int k = 0; k < CLASS_NAME_LEN; k++)
        if (raw[k] == CHAR_MALE || raw[k] == CHAR_FEMALE) raw[k] = 0xFF;
    gen3_decode(raw, CLASS_NAME_LEN, out, OW_CLASS_LEN);
}

static bool blocked_north(uint8_t b) { return b == 0x32 || b == 0x34 || b == 0x35 || b == 0xC0; }
static bool blocked_south(uint8_t b) { return b == 0x33 || b == 0x36 || b == 0x37 || b == 0xC0; }
static bool blocked_west(uint8_t b) { return b == 0x31 || b == 0x35 || b == 0x37 || b == 0xC1 || b == 0xBE; }
static bool blocked_east(uint8_t b) { return b == 0x30 || b == 0x34 || b == 0x36 || b == 0xC1 || b == 0xBE; }
typedef bool (*OwBlockFn)(uint8_t);
/* gOppositeDirectionBlockedMetatileFuncs / gDirectionBlockedMetatileFuncs, OwDir order. */
static const OwBlockFn k_own_blocks[OW_DIR_COUNT] = {blocked_south, blocked_north, blocked_west, blocked_east};
static const OwBlockFn k_dest_blocks[OW_DIR_COUNT] = {blocked_north, blocked_south, blocked_east, blocked_west};
static const int8_t k_dx[OW_DIR_COUNT] = {0, 0, -1, 1};
static const int8_t k_dy[OW_DIR_COUNT] = {1, -1, 0, 0};

static void grid_init(OwCtx* c) {
    uint32_t w = 0, h = 0, map = 0, layout = 0, prim = 0, sec = 0;
    const GameMem* m = c->m;
    c->grid_ok = false;
    if (!game_mem_read32(m, c->p->backup_map_layout, &w) || !game_mem_read32(m, c->p->backup_map_layout + 4, &h) ||
        !game_mem_read32(m, c->p->backup_map_layout + 8, &map))
        return;
    if (w == 0 || h == 0 || w > VMAP_MAX_CELLS || h > VMAP_MAX_CELLS || w * h > VMAP_MAX_CELLS ||
        map != c->p->backup_map_data || !game_mem_ok(m, map, w * h * 2u))
        return;
    if (!game_mem_read32(m, c->p->map_header + MAP_HEADER_LAYOUT, &layout) ||
        !game_mem_read32(m, layout + LAYOUT_PRIMARY, &prim) || !game_mem_read32(m, layout + LAYOUT_SECONDARY, &sec) ||
        !game_mem_read32(m, prim + TILESET_ATTRS, &c->attrs[0]) || !game_mem_read32(m, sec + TILESET_ATTRS, &c->attrs[1]))
        return;
    c->grid_w = (int32_t)w;
    c->grid_h = (int32_t)h;
    c->grid = map;
    c->grid_ok = true;
}

/* False outside the grid or on an undefined cell; the game treats both as walls. */
static bool grid_cell(const OwCtx* c, int32_t x, int32_t y, uint16_t* block) {
    if (x < 0 || y < 0 || x >= c->grid_w || y >= c->grid_h) return false;
    if (!game_mem_read16(c->m, c->grid + 2u * (uint32_t)(y * c->grid_w + x), block)) return false;
    return *block != GRID_UNDEFINED;
}

/* Strip cells use the current map's tilesets too, like GetMetatileAttributesById. */
static uint8_t behavior_of(const OwCtx* c, uint16_t block) {
    uint16_t id = block & 0x03FF, attr = 0xFF;
    uint32_t table = id < PRIMARY_METATILES ? c->attrs[0] : c->attrs[1];
    uint32_t index = id < PRIMARY_METATILES ? id : (uint32_t)(id - PRIMARY_METATILES);
    if (!game_mem_read16(c->m, table + 2u * index, &attr)) return 0xFF;
    return (uint8_t)(attr & 0xFF);
}

static bool object_blocks(const OwCtx* c, int self, int32_t x, int32_t y, uint8_t elev) {
    for (int i = 0; i < OBJ_COUNT; i++) {
        const uint8_t* o = c->objs[i];
        if (i == self || !c->obj_ok[i] || !(o[OBJ_FLAGS0] & 1) || (o[OBJ_FLAGS2] & 1)) continue;
        bool here = (s16_at(o + OBJ_CUR) == x && s16_at(o + OBJ_CUR + 2) == y) ||
                    (s16_at(o + OBJ_PREV) == x && s16_at(o + OBJ_PREV + 2) == y);
        uint8_t oe = o[OBJ_ELEVATION] & 0x0F;
        if (here && (elev == 0 || oe == 0 || elev == oe)) return true;
    }
    return false;
}

/* CheckPathBetweenTrainerAndPlayer, walked outward: tiles a player could be seen on. */
static uint8_t sight_clear(const OwCtx* c, int self, OwDir d, uint8_t range) {
    const uint8_t* o = c->objs[self];
    if (!c->grid_ok || k_own_blocks[d](o[OBJ_BEHAVIOR])) return 0;
    int32_t x = s16_at(o + OBJ_CUR), y = s16_at(o + OBJ_CUR + 2);
    uint8_t elev = o[OBJ_ELEVATION] & 0x0F;
    /* u32 counter: with range 255 a u8 would wrap and never end. */
    for (uint32_t k = 1; k <= range; k++) {
        int32_t tx = x + k_dx[d] * (int32_t)k, ty = y + k_dy[d] * (int32_t)k;
        uint16_t block = 0;
        if (!grid_cell(c, tx, ty, &block) || (block & GRID_COLLISION)) return (uint8_t)(k - 1);
        if (k_dest_blocks[d](behavior_of(c, block))) return (uint8_t)(k - 1);
        uint8_t te = (uint8_t)(block >> 12);
        if (elev != 0 && te != 0 && te != 15 && te != elev) return (uint8_t)(k - 1);
        if (object_blocks(c, self, tx, ty, elev)) return (uint8_t)(k - 1);
    }
    return range;
}

static void read_trainer(const OwCtx* c, int i, OverworldSnapshot* s) {
    const uint8_t* o = c->objs[i];
    uint8_t type = o[OBJ_TRAINER_TYPE], range = o[OBJ_RANGE], spr = o[OBJ_SPRITE];
    uint8_t sp[SPR_SIZE], head[4];
    OwTrainer t;

    if ((type != TRAINER_TYPE_NORMAL && type != TRAINER_TYPE_BURIED) || range == 0) return;
    if (spr >= SPR_COUNT || s->trainer_count >= OW_MAX_TRAINERS) return;
    if (!game_mem_copy(c->m, c->p->sprites + (uint32_t)spr * SPR_SIZE, sp, SPR_SIZE)) return;

    memset(&t, 0, sizeof(t));
    /* The sprite, not currentCoords: that is already the destination mid-step. */
    t.x = (int16_t)(s16_at(sp + SPR_X) + s16_at(sp + SPR_X2) + c->spr_off_x - 8);
    t.y = (int16_t)(s16_at(sp + SPR_Y) + s16_at(sp + SPR_Y2) + c->spr_off_y - (int8_t)sp[SPR_CTC_Y] - 16);

    uint8_t face = o[OBJ_DIRS] & 0x0F;
    t.facing = (face >= 1 && face <= 4) ? (uint8_t)(face - 1) : OW_DIR_DOWN;
    t.range = range;
    t.buried = type == TRAINER_TYPE_BURIED;
    for (int d = 0; d < OW_DIR_COUNT; d++)
        if (t.buried || d == t.facing) t.clear[d] = sight_clear(c, i, (OwDir)d, range);

    /* The game runs sight checks for objects a few tiles past the screen edge, so
     * keep a trainer whose cone reaches the screen even when its sprite does not. */
    int32_t x0 = t.x - 16 * t.clear[OW_DIR_LEFT], x1 = t.x + 16 * t.clear[OW_DIR_RIGHT];
    int32_t y0 = t.y - 16 * t.clear[OW_DIR_UP], y1 = t.y + 16 * t.clear[OW_DIR_DOWN];
    if (x1 <= -16 || x0 >= SCREEN_WIDTH || y1 <= -16 || y0 >= SCREEN_HEIGHT) return;

    uint32_t script = template_script(c, o);
    if (script >= 0x08000000u && game_mem_copy(c->m, script, head, sizeof(head)) &&
        head[0] == SCRIPT_TRAINERBATTLE) {
        t.known = true;
        t.id = (uint16_t)(head[2] | (head[3] << 8));
        t.beaten = flag_set(c, FLAG_TRAINER_BASE + t.id);
        class_name(c, t.id, t.class_name);
    }
    memcpy(&s->trainers[s->trainer_count++], &t, sizeof(t));
}

static bool field_visible(const OwCtx* c) {
    uint32_t cb2 = 0;
    uint8_t fade = 0, lock = 0;
    if (!game_mem_read32(c->m, c->p->main_callback2, &cb2) || cb2 != c->p->cb2_overworld) return false;
    if (!game_mem_read8(c->m, c->p->palette_fade_byte, &fade) || (fade & 0x80)) return false;
    if (!game_mem_read8(c->m, c->p->lock_field_controls, &lock) || lock) return false;
    if (c->loc_group == MAPGRP_FRONTIER &&
        (c->loc_num == MAP_PYRAMID_FLOOR || (c->loc_num >= MAP_HILL_FIRST && c->loc_num <= MAP_HILL_LAST)))
        return false;
    return true;
}

static void add_hidden(const OwCtx* c, uint32_t events, int32_t ox, int32_t oy, OverworldSnapshot* s) {
    uint8_t n = 0, ev[BG_SIZE];
    uint32_t bgs = 0;
    if (!game_mem_read8(c->m, events + EVENTS_BG_COUNT, &n) || !game_mem_read32(c->m, events + EVENTS_BGS, &bgs))
        return;
    for (uint32_t i = 0; i < n && s->hidden_count < OW_MAX_HIDDEN; i++) {
        int16_t x, y;
        if (!game_mem_copy(c->m, bgs + i * BG_SIZE, ev, BG_SIZE) || ev[BG_KIND] != BG_KIND_HIDDEN_ITEM) continue;
        if (flag_set(c, FLAG_HIDDEN_BASE + (uint32_t)(ev[BG_HIDDEN_ID] | (ev[BG_HIDDEN_ID + 1] << 8)))) continue;
        tile_to_screen(c, s16_at(ev) + ox, s16_at(ev + 2) + oy, &x, &y);
        if (!on_screen(x, y)) continue;
        s->hidden[s->hidden_count].x = x;
        s->hidden[s->hidden_count].y = y;
        s->hidden_count++;
    }
}

static bool layout_size(const OwCtx* c, uint32_t header, int32_t* w, int32_t* h) {
    uint32_t layout = 0, uw = 0, uh = 0;
    if (!game_mem_read32(c->m, header + MAP_HEADER_LAYOUT, &layout) ||
        !game_mem_read32(c->m, layout + LAYOUT_WIDTH, &uw) || !game_mem_read32(c->m, layout + LAYOUT_HEIGHT, &uh))
        return false;
    *w = (int32_t)uw;
    *h = (int32_t)uh;
    return true;
}

/* The current map's hidden items, plus those of connected maps shifted the way
 * the game stitches them into the VMap (fieldmap.c Fill*Connection). */
static void read_hidden(const OwCtx* c, OverworldSnapshot* s) {
    uint32_t events = 0, conns = 0, list = 0, count = 0;
    int32_t lw = 0, lh = 0;
    if (game_mem_read32(c->m, c->p->map_header + MAP_HEADER_EVENTS, &events))
        add_hidden(c, events, MAP_OFFSET, MAP_OFFSET, s);
    if (!layout_size(c, c->p->map_header, &lw, &lh) ||
        !game_mem_read32(c->m, c->p->map_header + MAP_HEADER_CONNS, &conns) || conns == 0 ||
        !game_mem_read32(c->m, conns, &count) || !game_mem_read32(c->m, conns + 4, &list))
        return;
    for (uint32_t i = 0; i < count && i < CONN_CAP; i++) {
        uint8_t conn[CONN_SIZE];
        int32_t nw = 0, nh = 0, ox, oy;
        uint32_t header, nev = 0;
        if (!game_mem_copy(c->m, list + i * CONN_SIZE, conn, CONN_SIZE)) return;
        header = map_header_rom(c, conn[8], conn[9]);
        if (!header || !layout_size(c, header, &nw, &nh) || !game_mem_read32(c->m, header + MAP_HEADER_EVENTS, &nev))
            continue;
        int32_t off = (int32_t)((uint32_t)conn[4] | ((uint32_t)conn[5] << 8) | ((uint32_t)conn[6] << 16) |
                                ((uint32_t)conn[7] << 24));
        switch (conn[0]) {
        case 1: ox = off + MAP_OFFSET; oy = lh + MAP_OFFSET; break;   /* south */
        case 2: ox = off + MAP_OFFSET; oy = MAP_OFFSET - nh; break;   /* north */
        case 3: ox = MAP_OFFSET - nw; oy = off + MAP_OFFSET; break;   /* west */
        case 4: ox = lw + MAP_OFFSET; oy = off + MAP_OFFSET; break;   /* east */
        default: continue;
        }
        add_hidden(c, nev, ox, oy, s);
    }
}

void overworld_read(const GameProfile* p, const GameMem* m, OverworldSnapshot* out) {
    OwCtx c;
    uint8_t sb[6];
    uint32_t fx = 0, fy = 0;
    uint16_t pan[2] = {0, 0}, off[2] = {0, 0};

    memset(out, 0, sizeof(*out));
    if (!p) return;
    memset(&c, 0, sizeof(c));
    c.p = p;
    c.m = m;
    if (!game_mem_read32(m, p->save_block1_ptr, &c.sb1) || !game_mem_in_ewram(c.sb1, SB1_OW_MIN_SIZE) ||
        !game_mem_copy(m, c.sb1 + SB1_POS, sb, sizeof(sb)))
        return;
    c.pos_x = s16_at(sb);
    c.pos_y = s16_at(sb + 2);
    c.loc_group = sb[SB1_LOC_GROUP];
    c.loc_num = sb[SB1_LOC_NUM];
    if (!field_visible(&c)) return;

    game_mem_read32(m, p->field_camera + 0x10, &fx);
    game_mem_read32(m, p->field_camera + 0x14, &fy);
    game_mem_read16(m, p->camera_pan, &pan[0]);
    game_mem_read16(m, p->camera_pan + 2, &pan[1]);
    game_mem_read16(m, p->sprite_coord_offset, &off[0]);
    game_mem_read16(m, p->sprite_coord_offset + 2, &off[1]);
    c.cam_dx = cam_adjust((int32_t)fx) - (int16_t)pan[0];
    c.cam_dy = cam_adjust((int32_t)fy) - (int16_t)pan[1] - 8;
    c.spr_off_x = (int16_t)off[0];
    c.spr_off_y = (int16_t)off[1];
    out->visible = true;
    grid_init(&c);

    for (int i = 0; i < OBJ_COUNT; i++)
        c.obj_ok[i] = game_mem_copy(m, p->object_events + (uint32_t)i * OBJ_SIZE, c.objs[i], OBJ_SIZE);
    for (int i = 0; i < OBJ_COUNT; i++)
        if (c.obj_ok[i] && (c.objs[i][OBJ_FLAGS0] & 1) && !(c.objs[i][OBJ_FLAGS2] & 1))
            read_trainer(&c, i, out);
    read_hidden(&c, out);
}

void overworld_feed_init(OverworldFeed* f, const GameProfile* p) {
    memset(f, 0, sizeof(*f));
    f->profile = p;
}

void overworld_feed_clear(OverworldFeed* f) {
    memset(&f->next, 0, sizeof(f->next));
    memset(&f->shown, 0, sizeof(f->shown));
}

void overworld_feed_capture(OverworldFeed* f, const GameMem* m) {
    /* memcpy, not assignment: padding bytes must stay zero for memcmp. */
    memcpy(&f->shown, &f->next, sizeof(f->shown));
    overworld_read(f->profile, m, &f->next);
}

void overworld_capture(const GBA* gba, void* feed) {
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    overworld_feed_capture((OverworldFeed*)feed, &m);
}

bool overworld_dump(const OverworldSnapshot* s, FILE* out) {
    static const char* const k_dir[OW_DIR_COUNT] = {"down", "up", "left", "right"};
    if (fprintf(out, "[overworld]\nvisible %d\n", s->visible ? 1 : 0) < 0) return false;
    for (uint8_t i = 0; i < s->trainer_count; i++) {
        const OwTrainer* t = &s->trainers[i];
        if (fprintf(out,
                    "trainer id=%u class=%s known=%d beaten=%d buried=%d facing=%s range=%u "
                    "clear=%u,%u,%u,%u x=%d y=%d\n",
                    (unsigned)t->id, t->class_name, t->known, t->beaten, t->buried,
                    k_dir[t->facing < OW_DIR_COUNT ? t->facing : 0], (unsigned)t->range,
                    (unsigned)t->clear[0], (unsigned)t->clear[1], (unsigned)t->clear[2],
                    (unsigned)t->clear[3], t->x, t->y) < 0)
            return false;
    }
    for (uint8_t i = 0; i < s->hidden_count; i++)
        if (fprintf(out, "hidden x=%d y=%d\n", s->hidden[i].x, s->hidden[i].y) < 0) return false;
    return true;
}
