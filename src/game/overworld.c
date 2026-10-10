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
#define OBJ_DIRS           0x18   /* low nibble: facingDirection 1..4 */
#define OBJ_RANGE          0x1D   /* trainerRange_berryTreeId */
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
#define MAP_HEADER_EVENTS  0x04
#define EVENTS_OBJ_COUNT   0x00
#define EVENTS_OBJS        0x04

#define FLAG_TRAINER_BASE  0x500
#define SCRIPT_TRAINERBATTLE 0x5C
#define TRAINER_SIZE       0x28
#define TRAINER_CLASS      0x01
#define CLASS_NAME_LEN     13
#define CHAR_MALE          0xB5
#define CHAR_FEMALE        0xB6

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
    if (!on_screen(t.x, t.y)) return;

    uint8_t face = o[OBJ_DIRS] & 0x0F;
    t.facing = (face >= 1 && face <= 4) ? (uint8_t)(face - 1) : OW_DIR_DOWN;
    t.range = range;
    t.buried = type == TRAINER_TYPE_BURIED;

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

    for (int i = 0; i < OBJ_COUNT; i++)
        c.obj_ok[i] = game_mem_copy(m, p->object_events + (uint32_t)i * OBJ_SIZE, c.objs[i], OBJ_SIZE);
    for (int i = 0; i < OBJ_COUNT; i++)
        if (c.obj_ok[i] && (c.objs[i][OBJ_FLAGS0] & 1) && !(c.objs[i][OBJ_FLAGS2] & 1))
            read_trainer(&c, i, out);
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
    (void)s;
    return fprintf(out, "[overworld]\n") >= 0;
}
