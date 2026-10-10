#include "game/overworld.h"
#include <string.h>
#include "gba.h"

/* Struct offsets (pret/pokeemerald include/global.fieldmap.h, include/sprite.h,
 * include/global.h); see the overworld research docs for verification marks. */
#define SB1_POS            0x00   /* s16 x, y: map coords of the screen's top-left tile */
#define SB1_LOC_GROUP      0x04
#define SB1_LOC_NUM        0x05
#define SB1_FLAGS          0x1270
#define SB1_OW_MIN_SIZE    (SB1_FLAGS + 300)

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
} OwCtx;

static int16_t s16_at(const uint8_t* b) { return (int16_t)(b[0] | (b[1] << 8)); }

/* Sub-tile camera progress, from SetSpritePosToMapCoords (event_object_movement.c). */
static int32_t cam_adjust(int32_t f) { return -f + (f > 0 ? 16 : f < 0 ? -16 : 0); }

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
