#include "test_harness.h"
#include "game_fixture.h"
#include "game/overworld.h"

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x8000];

#define SB1        0x02010000u
#define OBJS       0x02001000u
#define SPRITES    0x02002000u
#define SPR_OFF    0x02003000u
#define FADE       0x02003100u
#define MAP_HDR    0x02003200u   /* RAM copy of the current MapHeader */
#define GRID       0x02020000u
#define CAMERA     0x03000100u
#define PAN        0x03000200u
#define BACKUP     0x03000300u
#define CB2        0x03000400u
#define LOCK       0x03000500u
#define CB2_FIELD  0x08000101u
#define TRAINERS   0x08000400u
#define CLASSES    0x08000800u
#define GROUPS     0x08000C00u
#define LAYOUT     0x08001000u
#define TILESET1   0x08001100u
#define TILESET2   0x08001120u
#define ATTRS1     0x08001200u   /* u16[512] */
#define ATTRS2     0x08001600u
#define EVENTS     0x08001A00u
#define BG_EVENTS  0x08001B00u
#define SCRIPT     0x08002000u
#define ITEMS      0x08004800u   /* gItems, 44-byte stride */

static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .map_header = MAP_HDR,
    .object_events = OBJS, .sprites = SPRITES, .sprite_coord_offset = SPR_OFF,
    .field_camera = CAMERA, .camera_pan = PAN,
    .backup_map_layout = BACKUP, .backup_map_data = GRID,
    .main_callback2 = CB2, .cb2_overworld = CB2_FIELD,
    .palette_fade_byte = FADE, .lock_field_controls = LOCK,
    .trainers = TRAINERS, .trainer_count = 16,
    .trainer_class_names = CLASSES, .trainer_class_count = 4,
    .map_groups = GROUPS, .map_group_count = 2,
    .items = ITEMS, .item_count = 80,
};

static uint8_t* at(uint32_t a) {
    if (a >= 0x08000000u) return s_rom + (a - 0x08000000u);
    if (a >= 0x03000000u) return s_iwram + (a - 0x03000000u);
    return s_ewram + (a - 0x02000000u);
}
static void put8(uint32_t a, uint8_t v) { *at(a) = v; }
static void put16(uint32_t a, uint16_t v) { fixture_put16(at(a), v); }
static void put32(uint32_t a, uint32_t v) { fixture_put32(at(a), v); }
/* ASCII uppercase, digits and spaces -> Gen 3 text, 0xFF-terminated. */
static void put_name(uint32_t a, const char* s) {
    uint8_t* p = at(a);
    for (; *s; s++, p++)
        *p = *s == ' ' ? 0x00 : (*s >= '0' && *s <= '9') ? (uint8_t)(0xA1 + (*s - '0')) : (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}
static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

#define GRID_W 40
#define GRID_H 40
static void set_cell(int x, int y, uint16_t block) { put16(GRID + 2u * (uint32_t)(y * GRID_W + x), block); }

/* Object slot i. cur/prev are VMap coords. dir is the game's 1..4. */
static void put_obj(int i, bool player, uint8_t trainer_type, uint8_t local_id, uint8_t group,
                    uint8_t num, int16_t x, int16_t y, uint8_t dir, uint8_t range, uint8_t sprite) {
    uint32_t o = OBJS + 0x24u * (uint32_t)i;
    memset(at(o), 0, 0x24);
    put8(o + 0x00, 0x01);
    put8(o + 0x02, player ? 0x01 : 0x00);
    put8(o + 0x04, sprite);
    put8(o + 0x07, trainer_type);
    put8(o + 0x08, local_id);
    put8(o + 0x09, num);
    put8(o + 0x0A, group);
    put8(o + 0x0B, 0x33);                 /* elevation 3 */
    put16(o + 0x10, (uint16_t)x); put16(o + 0x12, (uint16_t)y);
    put16(o + 0x14, (uint16_t)x); put16(o + 0x16, (uint16_t)y);
    put16(o + 0x18, dir);
    put8(o + 0x1D, range);
}
/* Sprite whose tile top-left lands on screen (x, y) with zero camera offsets
 * (16x32 sprite: centerToCornerVecY = -16). */
static void put_sprite(uint8_t i, int16_t x, int16_t y) {
    uint32_t s = SPRITES + 0x44u * i;
    memset(at(s), 0, 0x44);
    put16(s + 0x20, (uint16_t)(x + 8));
    put16(s + 0x22, (uint16_t)y);
    put8(s + 0x29, 0xF0);
}

/* Camera at map pos (10, 20), no scroll in progress, default pans 0/32.
 * A tile at VMap (vx, vy) is drawn at ((vx-10)*16, (vy-20)*16 - 40).
 * Player at VMap (17, 27) = screen (112, 72). Youngster (trainer 10, class 2)
 * at VMap (14, 24) = screen (64, 24), facing south, range 4. */
static void scene(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    put32(0x03000000, SB1);
    put16(SB1 + 0x00, 10); put16(SB1 + 0x02, 20);     /* pos */
    put8(SB1 + 0x04, 0); put8(SB1 + 0x05, 1);         /* location group 0, num 1 */
    put32(CB2, CB2_FIELD);
    put16(PAN + 0, 0); put16(PAN + 2, 32);
    put32(BACKUP + 0, GRID_W); put32(BACKUP + 4, GRID_H); put32(BACKUP + 8, GRID);
    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++) set_cell(x, y, 0x3001);   /* elevation 3, metatile 1 */
    put32(MAP_HDR + 0x00, LAYOUT);
    put32(MAP_HDR + 0x04, EVENTS);
    put32(LAYOUT + 0x00, GRID_W - 15); put32(LAYOUT + 0x04, GRID_H - 14);
    put32(LAYOUT + 0x10, TILESET1); put32(LAYOUT + 0x14, TILESET2);
    put32(TILESET1 + 0x10, ATTRS1); put32(TILESET2 + 0x10, ATTRS2);
    put8(EVENTS + 0x00, 1);                            /* one object template */
    put32(EVENTS + 0x10, BG_EVENTS);
    /* Player and Youngster. */
    put_obj(0, true, 0, 0xFF, 0, 1, 17, 27, 1, 0, 0);
    put_sprite(0, 112, 72);
    put_obj(1, false, 1, 3, 0, 1, 14, 24, 1, 4, 5);
    put_sprite(5, 64, 24);
    /* SaveBlock1 template 0: localId 3, script -> trainerbattle id 10. */
    put8(SB1 + 0xC70, 3);
    put32(SB1 + 0xC70 + 0x10, SCRIPT);
    put8(SCRIPT + 0, 0x5C); put8(SCRIPT + 1, 0x00); put16(SCRIPT + 2, 10);
    put8(TRAINERS + 10 * 0x28 + 1, 2);                 /* gTrainers[10].trainerClass = 2 */
    put_name(CLASSES + 2 * 13, "YOUNGSTER");
}

static const OwTrainer* first_trainer(const OverworldSnapshot* s) {
    return s->trainer_count ? &s->trainers[0] : NULL;
}

TEST(ow_null_profile_gives_empty_snapshot) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    memset(&s, 0xAA, sizeof(s));
    overworld_read(NULL, &m, &s);
    ASSERT_TRUE(!s.visible);
    ASSERT_EQ(s.trainer_count, 0);
    ASSERT_EQ(s.marker_count, 0);
}

TEST(ow_feed_capture_shifts_next_to_shown) {
    static OverworldFeed f;
    GameMem m = mem();
    scene();
    overworld_feed_init(&f, &k_profile);
    overworld_feed_capture(&f, &m);
    ASSERT_TRUE(f.next.visible);
    ASSERT_TRUE(!f.shown.visible);
    overworld_feed_capture(&f, &m);
    ASSERT_TRUE(f.shown.visible);
}

TEST(ow_feed_clear_empties_both_slots) {
    static OverworldFeed f;
    static OverworldSnapshot zero;
    GameMem m = mem();
    scene();
    overworld_feed_init(&f, &k_profile);
    overworld_feed_capture(&f, &m);
    overworld_feed_capture(&f, &m);
    overworld_feed_clear(&f);
    ASSERT_MEM_EQ(&f.next, &zero, sizeof(zero));
    ASSERT_MEM_EQ(&f.shown, &zero, sizeof(zero));
    ASSERT_TRUE(f.profile == &k_profile);
}

TEST(ow_visible_on_field) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(s.visible);
}

TEST(ow_hidden_when_callback_not_overworld) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put32(CB2, 0x08000201);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(!s.visible);
    ASSERT_EQ(s.trainer_count, 0);
}

TEST(ow_hidden_during_fade) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(FADE, 0x80);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(!s.visible);
    put8(FADE, 0x21);   /* other bits of that byte are not the fade flag */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(s.visible);
}

TEST(ow_hidden_while_controls_locked) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(LOCK, 1);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(!s.visible);
}

TEST(ow_hidden_in_pyramid_and_trainer_hill) {
    OverworldSnapshot s;
    GameMem m = mem();
    const uint8_t nums[] = {26, 61, 65};
    for (int i = 0; i < 3; i++) {
        scene();
        put8(SB1 + 0x04, 26); put8(SB1 + 0x05, nums[i]);
        overworld_read(&k_profile, &m, &s);
        ASSERT_TRUE(!s.visible);
    }
    scene();
    put8(SB1 + 0x04, 26); put8(SB1 + 0x05, 60);   /* Trainer Hill entrance is a normal map */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(s.visible);
}

TEST(ow_hidden_without_save_block) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put32(0x03000000, 0);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(!s.visible);
}

TEST(ow_trainer_resolved_from_save_template) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    overworld_read(&k_profile, &m, &s);
    const OwTrainer* t = first_trainer(&s);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ(s.trainer_count, 1);          /* the player is not a trainer */
    ASSERT_TRUE(t->known);
    ASSERT_EQ(t->id, 10);
    ASSERT_STR_EQ(t->class_name, "YOUNGSTER");
    ASSERT_EQ(t->x, 64);
    ASSERT_EQ(t->y, 24);
    ASSERT_EQ(t->facing, OW_DIR_DOWN);
    ASSERT_EQ(t->range, 4);
    ASSERT_TRUE(!t->beaten && !t->buried);
}

TEST(ow_trainer_beaten_flag) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(SB1 + 0x1270 + ((0x500 + 10) >> 3), (uint8_t)(1u << ((0x500 + 10) & 7)));
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_TRUE(first_trainer(&s)->beaten);
}

TEST(ow_trainer_id_past_table_is_not_beaten) {
    /* An id the trainer table does not hold must not read FLAG_TRAINER_BASE + id
     * out of unrelated save data. */
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put16(SCRIPT + 2, 0xFFFF);
    put8(SB1 + 0x1270 + ((0x500 + 0xFFFF) >> 3), 0xFF);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_TRUE(!first_trainer(&s)->beaten);
}

TEST(ow_trainer_position_comes_from_sprite) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put16(OBJS + 0x24 + 0x12, 25);   /* mid-step: currentCoords already one tile on */
    put_sprite(5, 64, 30);           /* sprite still 6 px into the step */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->y, 30);
}

TEST(ow_unknown_script_still_listed) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(SCRIPT, 0x0F);                     /* not trainerbattle */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 1);
    ASSERT_TRUE(!first_trainer(&s)->known);
    ASSERT_STR_EQ(first_trainer(&s)->class_name, "");
    scene();
    put32(SB1 + 0xC70 + 0x10, 0);           /* NULL script */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 1);
    ASSERT_TRUE(!first_trainer(&s)->known);
}

TEST(ow_skips_range0_type2_and_offscreen) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(OBJS + 0x24 + 0x1D, 0);
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 0);
    scene();
    put8(OBJS + 0x24 + 0x07, 2);            /* SEE_ALL_DIRECTIONS: never used by the game */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 0);
    scene();
    put_sprite(5, 300, 24);
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 0);
}

TEST(ow_buried_trainer_flagged) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(OBJS + 0x24 + 0x07, 3);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_TRUE(first_trainer(&s)->buried);
}

TEST(ow_class_name_drops_gender_symbol) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_name(CLASSES + 2 * 13, "SWIMMER");
    put8(CLASSES + 2 * 13 + 7, 0xB5);       /* SWIMMER♂ */
    put8(CLASSES + 2 * 13 + 8, 0xFF);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_STR_EQ(first_trainer(&s)->class_name, "SWIMMER");
}

TEST(ow_template_from_rom_header_for_other_map) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    /* Object left over from map 0/2 after crossing a connection. */
    put8(OBJS + 0x24 + 0x09, 2);
    put32(GROUPS + 0, 0x08003000);           /* group 0 header list */
    put32(0x08003000 + 4 * 2, 0x08003100);   /* map 0/2 header */
    put32(0x08003100 + 0x04, 0x08003200);    /* its events */
    put8(0x08003200 + 0x00, 1);
    put32(0x08003200 + 0x04, 0x08003300);    /* its object templates */
    put8(0x08003300 + 0x00, 3);              /* localId 3 */
    put32(0x08003300 + 0x10, 0x08003400);
    put8(0x08003400, 0x5C); put16(0x08003402, 11);
    put8(TRAINERS + 11 * 0x28 + 1, 2);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_TRUE(first_trainer(&s)->known);
    ASSERT_EQ(first_trainer(&s)->id, 11);
}

TEST(ow_sight_clear_full_range) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 4);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_UP], 0);   /* normal trainers look one way */
}

TEST(ow_sight_stops_at_collision_and_undefined) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    set_cell(14, 27, 0x3401);                          /* collision bit, 3rd tile */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 2);
    scene();
    set_cell(14, 25, 0x03FF);
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 0);
}

TEST(ow_sight_elevation_rules) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    set_cell(14, 26, 0x4001);                          /* elevation 4 vs trainer 3 */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 1);
    scene();
    set_cell(14, 26, 0xF001);                          /* 15 = multi-level, never mismatches */
    set_cell(14, 27, 0x0001);                          /* 0 = transition, never mismatches */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 4);
}

TEST(ow_sight_blocked_by_npc_not_player) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_obj(2, false, 0, 7, 0, 1, 14, 26, 1, 0, 9);    /* NPC on the 2nd tile */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 1);
    scene();
    put16(OBJS + 0x10, 14); put16(OBJS + 0x12, 26);    /* move the player there instead */
    put16(OBJS + 0x14, 14); put16(OBJS + 0x16, 26);
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 4);
}

TEST(ow_sight_one_way_tiles) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put16(ATTRS1 + 2 * 2, 0x32);                       /* metatile 2 = IMPASSABLE_NORTH */
    set_cell(14, 25, 0x3002);
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 0);
    scene();
    put8(OBJS + 0x24 + 0x1E, 0x33);                    /* trainer stands on IMPASSABLE_SOUTH */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 0);
    scene();
    put16(ATTRS1 + 2 * 3, 0x3B);                       /* MB_JUMP_SOUTH: ledges do not block */
    set_cell(14, 25, 0x3003);
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 4);
}

TEST(ow_sight_secondary_tileset) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put16(ATTRS2 + 2 * 5, 0x32);                       /* metatile 512 + 5 */
    set_cell(14, 26, 0x3000 | (512 + 5));
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 1);
}

TEST(ow_buried_looks_all_ways) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(OBJS + 0x24 + 0x07, 3);
    put8(OBJS + 0x24 + 0x1D, 1);
    set_cell(13, 24, 0x3401);                          /* wall to the west */
    overworld_read(&k_profile, &m, &s);
    const OwTrainer* t = first_trainer(&s);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ(t->clear[OW_DIR_DOWN], 1);
    ASSERT_EQ(t->clear[OW_DIR_UP], 1);
    ASSERT_EQ(t->clear[OW_DIR_LEFT], 0);
    ASSERT_EQ(t->clear[OW_DIR_RIGHT], 1);
}

TEST(ow_bad_grid_omits_cones_keeps_trainer) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put32(BACKUP + 0, 200); put32(BACKUP + 4, 200);   /* 40000 cells > 10240 */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 1);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 0);
    scene();
    put32(BACKUP + 8, GRID + 2);                       /* not the game's buffer */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 0);
    scene();
    put32(BACKUP + 0, 0xFFFFFFF0u);                    /* negative width */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 0);
}

/* Hidden item bg event i at map coords (x, y) with hiddenItemId id; the item id is id too. */
static void put_hidden(uint32_t table, int i, uint16_t x, uint16_t y, uint8_t kind, uint16_t id) {
    uint32_t e = table + 12u * (uint32_t)i;
    put16(e + 0, x); put16(e + 2, y); put8(e + 5, kind); put16(e + 0x08, id); put16(e + 0x0A, id);
}

TEST(ow_hidden_item_on_screen) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(EVENTS + 0x03, 3);
    put_hidden(BG_EVENTS, 0, 5, 17, 7, 3);     /* VMap (12, 24) -> screen (32, 24) */
    put_hidden(BG_EVENTS, 1, 6, 17, 0, 4);     /* a sign, not an item */
    put_hidden(BG_EVENTS, 2, 60, 17, 7, 5);    /* off screen */
    put_name(ITEMS + 3 * 44, "HEART SCALE");
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 1);
    ASSERT_EQ(s.markers[0].x, 32);
    ASSERT_EQ(s.markers[0].y, 24);
    ASSERT_EQ(s.markers[0].kind, OW_MARK_HIDDEN);
    ASSERT_EQ(s.markers[0].name_count, 1);
    ASSERT_STR_EQ(s.markers[0].name[0], "HEART SCALE");
}

TEST(ow_hidden_item_collected_is_skipped) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(EVENTS + 0x03, 1);
    put_hidden(BG_EVENTS, 0, 5, 17, 7, 3);
    put8(SB1 + 0x1270 + ((0x1F4 + 3) >> 3), (uint8_t)(1u << ((0x1F4 + 3) & 7)));
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 0);
}

TEST(ow_hidden_item_follows_camera_scroll) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put8(EVENTS + 0x03, 1);
    put_hidden(BG_EVENTS, 0, 5, 17, 7, 3);
    put32(CAMERA + 0x10, 5);                    /* dx = -5 + 16 = 11 */
    put32(CAMERA + 0x14, (uint32_t)-3);         /* dy = 3 - 16 = -13 */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 1);
    ASSERT_EQ(s.markers[0].x, 32 + 11);
    ASSERT_EQ(s.markers[0].y, 24 - 13);
}

TEST(ow_hidden_item_in_south_connection) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put16(SB1 + 0x00, 5); put16(SB1 + 0x02, 28);   /* camera near the south edge */
    put32(MAP_HDR + 0x0C, 0x08004000);              /* connections */
    put32(0x08004000 + 0, 1); put32(0x08004000 + 4, 0x08004010);
    put8(0x08004010 + 0, 1);                        /* SOUTH */
    put32(0x08004010 + 4, 0);                       /* offset 0 */
    put8(0x08004010 + 8, 0); put8(0x08004010 + 9, 2);
    put32(GROUPS + 0, 0x08003000);
    put32(0x08003000 + 4 * 2, 0x08003100);
    put32(0x08003100 + 0x00, 0x08003180);           /* neighbour layout 25 x 10 */
    put32(0x08003180 + 0, 25); put32(0x08003180 + 4, 10);
    put32(0x08003100 + 0x04, 0x08003200);
    put8(0x08003200 + 0x03, 1); put32(0x08003200 + 0x10, 0x08003500);
    put_hidden(0x08003500, 0, 2, 0, 7, 9);          /* VMap (9, 26 + 7) */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 1);
    ASSERT_EQ(s.markers[0].x, (9 - 5) * 16);
    ASSERT_EQ(s.markers[0].y, (33 - 28) * 16 - 40);
}

TEST(ow_dump_lists_trainers_and_items) {
    OverworldSnapshot s;
    GameMem m = mem();
    char buf[512];
    scene();
    put8(EVENTS + 0x03, 1);
    put_hidden(BG_EVENTS, 0, 5, 17, 7, 3);
    put_name(ITEMS + 3 * 44, "HEART SCALE");
    overworld_read(&k_profile, &m, &s);
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(overworld_dump(&s, f));
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_TRUE(strstr(buf, "[overworld]\nvisible 1\n") != NULL);
    ASSERT_TRUE(strstr(buf, "trainer id=10 class=YOUNGSTER known=1 beaten=0 buried=0 facing=down "
                            "range=4 clear=4,0,0,0 x=64 y=24\n") != NULL);
    ASSERT_TRUE(strstr(buf, "marker kind=hidden x=32 y=24 name=HEART SCALE\n") != NULL);
}

TEST(ow_sight_range_255_terminates) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    /* A wide but legal grid (300 x 34 = 10200 cells) with 255+ clear tiles east. */
    put32(BACKUP + 0, 300); put32(BACKUP + 4, 34);
    for (uint32_t i = 0; i < 300u * 34u; i++) put16(GRID + 2u * i, 0x3001);
    put16(OBJS + 0x24 + 0x18, 4);     /* facing east */
    put8(OBJS + 0x24 + 0x1D, 255);    /* garbage range: a u8 counter would wrap forever */
    overworld_read(&k_profile, &m, &s);
    ASSERT_TRUE(first_trainer(&s) != NULL);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_RIGHT], 255);
}

TEST(ow_offscreen_trainer_kept_when_cone_reaches_screen) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_sprite(5, 64, -40);              /* 2.5 tiles above the screen, looking down 4 tiles */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 1);
    ASSERT_EQ(first_trainer(&s)->y, -40);
    ASSERT_EQ(first_trainer(&s)->clear[OW_DIR_DOWN], 4);
    scene();
    put_sprite(5, 64, -100);             /* cone ends at -36: never on screen */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.trainer_count, 0);
}

/* Item ball: object slot 3, localId 8, template 1 with a finditem script. */
static void put_ball(uint16_t item) {
    put8(EVENTS + 0x00, 2);
    put_obj(3, false, 0, 8, 0, 1, 18, 25, 1, 0, 6);
    put_sprite(6, 128, 40);
    put8(SB1 + 0xC70 + 0x18, 8);
    put32(SB1 + 0xC70 + 0x18 + 0x10, 0x08002100);
    const uint8_t bytes[12] = {0x1A, 0x00, 0x80, (uint8_t)item, (uint8_t)(item >> 8),
                               0x1A, 0x01, 0x80, 0x01, 0x00, 0x09, 0x01};
    memcpy(at(0x08002100), bytes, sizeof(bytes));
}

TEST(ow_item_ball_named) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_ball(68);
    put_name(ITEMS + 68 * 44, "RARE CANDY");
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 1);
    ASSERT_EQ(s.markers[0].kind, OW_MARK_ITEM_BALL);
    ASSERT_EQ(s.markers[0].x, 128);
    ASSERT_EQ(s.markers[0].y, 40);
    ASSERT_STR_EQ(s.markers[0].name[0], "RARE CANDY");
}

TEST(ow_ball_sprite_with_other_script_not_tagged) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_ball(68);
    put8(0x08002100 + 11, 0x00);          /* callstd 0 (a gift), not finditem */
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 0);
}

/* Gift NPC: object slot 2, localId 7, template 1, script at 0x08002200 giving item 5 under flag 0x123. */
static void put_gift_npc(void) {
    put8(EVENTS + 0x00, 2);
    put_obj(2, false, 0, 7, 0, 1, 20, 25, 1, 0, 7);
    put_sprite(7, 160, 40);
    put8(SB1 + 0xC70 + 0x18, 7);
    put32(SB1 + 0xC70 + 0x18 + 0x10, 0x08002200);
    const uint8_t bytes[] = {0x1A, 0x00, 0x80, 0x05, 0x00, 0x1A, 0x01, 0x80, 0x01, 0x00, 0x09, 0x00,
                             0x29, 0x23, 0x01, 0x02};
    memcpy(at(0x08002200), bytes, sizeof(bytes));
    put_name(ITEMS + 5 * 44, "TM36");
}

TEST(ow_gift_tag_shown_until_received) {
    static OwGiftCache cache;
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_gift_npc();
    memset(&cache, 0, sizeof(cache));
    overworld_read_cached(&k_profile, &m, &cache, &s);
    ASSERT_EQ(s.marker_count, 1);
    ASSERT_EQ(s.markers[0].kind, OW_MARK_GIFT);
    ASSERT_EQ(s.markers[0].x, 160);
    ASSERT_STR_EQ(s.markers[0].name[0], "TM36");
    put8(SB1 + 0x1270 + (0x123 >> 3), (uint8_t)(1u << (0x123 & 7)));
    overworld_read_cached(&k_profile, &m, &cache, &s);
    ASSERT_EQ(s.marker_count, 0);
}

TEST(ow_no_gift_tags_without_cache) {
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_gift_npc();
    overworld_read(&k_profile, &m, &s);
    ASSERT_EQ(s.marker_count, 0);
}

TEST(ow_gift_cache_shared_script_walks_once) {
    static OwGiftCache cache;
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_gift_npc();
    memset(&cache, 0, sizeof(cache));
    overworld_read_cached(&k_profile, &m, &cache, &s);
    ASSERT_EQ(cache.count, 2);              /* the trainer's script and the gift script */
    put16(0x08002200 + 13, 0x930);          /* a daily flag: a re-walk would drop the tag */
    overworld_read_cached(&k_profile, &m, &cache, &s);
    ASSERT_EQ(cache.count, 2);
    ASSERT_EQ(s.marker_count, 1);           /* served from the cache */
}

TEST(ow_gift_cache_resets_on_map_change) {
    static OwGiftCache cache;
    OverworldSnapshot s;
    GameMem m = mem();
    scene();
    put_gift_npc();
    memset(&cache, 0, sizeof(cache));
    overworld_read_cached(&k_profile, &m, &cache, &s);
    put8(SB1 + 0x05, 2);                    /* now on map 0/2: objects' templates no longer resolve */
    overworld_read_cached(&k_profile, &m, &cache, &s);
    ASSERT_EQ(cache.num, 2);
    ASSERT_EQ(cache.count, 0);
    ASSERT_EQ(s.marker_count, 0);
}

TEST(ow_feed_clear_empties_gift_cache) {
    static OverworldFeed f;
    GameMem m = mem();
    scene();
    put_gift_npc();
    overworld_feed_init(&f, &k_profile);
    overworld_feed_capture(&f, &m);
    ASSERT_TRUE(f.gifts.count > 0);
    overworld_feed_clear(&f);
    ASSERT_EQ(f.gifts.count, 0);
    ASSERT_TRUE(!f.gifts.valid);
}

/* ---- Feebas ---- */

#define FB_SECTIONS 0x08006000u
#define FB_TILEBITS 0x08006100u
#define FB_WILD     0x08006200u
#define FB_SPECIES  0x08006300u
#define FB_TASKS    0x03001000u
#define FB_FISHING  0x08000201u

/* k_profile plus the Feebas fields; the scene's map (0.1) plays Route 119. */
static const GameProfile k_fb_profile = {
    .name = "test", .rules = &g_emerald_rules,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .map_header = MAP_HDR,
    .object_events = OBJS, .sprites = SPRITES, .sprite_coord_offset = SPR_OFF,
    .field_camera = CAMERA, .camera_pan = PAN,
    .backup_map_layout = BACKUP, .backup_map_data = GRID,
    .main_callback2 = CB2, .cb2_overworld = CB2_FIELD,
    .palette_fade_byte = FADE, .lock_field_controls = LOCK,
    .trainers = TRAINERS, .trainer_count = 16,
    .trainer_class_names = CLASSES, .trainer_class_count = 4,
    .map_groups = GROUPS, .map_group_count = 2,
    .items = ITEMS, .item_count = 80,
    .species_names = FB_SPECIES, .species_count = 8,
    .wild_feebas = FB_WILD,
    .feebas_map_group = 0, .feebas_map_num = 1,
    .feebas_water_tiles = FB_SECTIONS, .tile_bit_attributes = FB_TILEBITS,
    .tasks = FB_TASKS, .task_fishing = FB_FISHING,
};

/* Locks field controls; with a rod out, task 3 runs Task_Fishing. */
static void fb_lock(bool rod_out) {
    put8(LOCK, 1);
    put32(FB_TASKS + 0x28 * 3, rod_out ? FB_FISHING : 0x08000301u);
    put8(FB_TASKS + 0x28 * 3 + 4, 1);
}

/* Map coords: VMap minus 7. Metatile 2 is ocean water. */
static void fb_water(int mx, int my, uint16_t collision) {
    set_cell(mx + 7, my + 7, (uint16_t)(0x1002 | (collision << 10)));
}

/* Layout 25x26 (sections rows 0-8, 9-17, 18-25), seed 0: spots 447,149,291,351,369,78.
 * Water at map (5,16) = section 1's first tile = 149, drawn at screen (32, 8).
 * Water at map (20,1) = section 0's first tile = 78, off screen. */
static void fb_scene(bool on_screen_tile, bool off_screen_tile) {
    static const uint16_t sec[9] = {0, 8, 77, 9, 17, 148, 18, 25, 400};
    scene();
    for (int i = 0; i < 9; i++) put16(FB_SECTIONS + 2u * (uint32_t)i, sec[i]);
    put16(ATTRS1 + 2 * 2, 0x15);
    put8(FB_TILEBITS + 0x15, 0x03);
    put16(FB_WILD + 2, 3);
    put_name(FB_SPECIES + 3 * 11, "FEEBAS");
    put16(SB1 + 0x2E6A, 0x0000);
    if (on_screen_tile) fb_water(5, 16, 0);
    if (off_screen_tile) fb_water(20, 1, 0);
}

static const OwMarker* fb_marker(const OverworldSnapshot* s) {
    for (uint8_t i = 0; i < s->marker_count; i++)
        if (s->markers[i].kind == OW_MARK_FEEBAS || s->markers[i].kind == OW_MARK_FEEBAS_BLOCKED)
            return &s->markers[i];
    return NULL;
}

TEST(ow_feebas_markers_on_screen_only) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(true, true);
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(s.visible);
    ASSERT_TRUE(s.feebas_visible);
    ASSERT_EQ(s.marker_count, 1);
    const OwMarker* mk = fb_marker(&s);
    ASSERT_TRUE(mk != NULL);
    ASSERT_EQ(mk->kind, OW_MARK_FEEBAS);
    ASSERT_EQ(mk->x, 32);
    ASSERT_EQ(mk->y, 8);
    ASSERT_EQ(mk->spot, 149);
    ASSERT_STR_EQ(mk->name[0], "FEEBAS");
    ASSERT_TRUE(!s.hint.show);   /* a fishable tile is on screen */
}

TEST(ow_feebas_survive_fishing_lock) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(true, true);
    fb_lock(true);
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(!s.visible);
    ASSERT_TRUE(s.feebas_visible);
    ASSERT_EQ(s.trainer_count, 0);
    ASSERT_EQ(s.marker_count, 1);
    ASSERT_EQ(s.markers[0].kind, OW_MARK_FEEBAS);
    ASSERT_TRUE(!s.hint.show);
}

TEST(ow_feebas_hidden_in_other_locks) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(true, true);
    fb_lock(false);   /* a menu or dialog, not fishing */
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(!s.visible);
    ASSERT_TRUE(!s.feebas_visible);
    ASSERT_EQ(s.marker_count, 0);
    ASSERT_TRUE(!s.hint.show);
}

TEST(ow_feebas_skips_message_box_while_fishing) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(false, false);
    fb_water(5, 22, 0);               /* section 2's first tile: base 400 + 1, not a spot */
    put16(FB_SECTIONS + 2 * 8, 290);  /* now 291, a spot; drawn at y = 104, into the box */
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(fb_marker(&s) != NULL);
    fb_lock(true);
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(s.feebas_visible);
    ASSERT_TRUE(fb_marker(&s) == NULL);
}

TEST(ow_feebas_sliver_on_screen_still_hints) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(false, false);
    fb_water(2, 16, 0);               /* 149 at VMap (9, 23) */
    put32(CAMERA + 0x10, 12);         /* scrolling: tiles shift 4 px right, this one sits at x = -12 */
    overworld_read(&k_fb_profile, &m, &s);
    const OwMarker* mk = fb_marker(&s);
    ASSERT_TRUE(mk != NULL);
    ASSERT_EQ(mk->x, -12);
    ASSERT_TRUE(s.hint.show);         /* 4 px of it is not enough to see */
}

TEST(ow_feebas_hint_when_none_on_screen) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(false, true);
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(fb_marker(&s) == NULL);
    ASSERT_TRUE(s.hint.show);
    ASSERT_EQ(s.hint.px, 112);        /* player VMap (17, 27) */
    ASSERT_EQ(s.hint.py, 72);
    ASSERT_EQ(s.hint.tx, 272);        /* VMap (27, 8) */
    ASSERT_EQ(s.hint.ty, -232);
    ASSERT_STR_EQ(s.hint.name, "FEEBAS");
}

TEST(ow_feebas_blocked_on_screen_still_hints) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(false, true);
    fb_water(5, 16, 1);               /* 149 on screen, but blocked */
    overworld_read(&k_fb_profile, &m, &s);
    const OwMarker* mk = fb_marker(&s);
    ASSERT_TRUE(mk != NULL && mk->kind == OW_MARK_FEEBAS_BLOCKED);
    ASSERT_TRUE(s.hint.show);
    ASSERT_EQ(s.hint.tx, 272);
}

TEST(ow_feebas_none_off_route) {
    OverworldSnapshot s;
    GameMem m = mem();
    fb_scene(true, true);
    put8(SB1 + 0x05, 2);
    overworld_read(&k_fb_profile, &m, &s);
    ASSERT_TRUE(s.visible);
    ASSERT_TRUE(!s.feebas_visible);
    ASSERT_TRUE(fb_marker(&s) == NULL);
    ASSERT_TRUE(!s.hint.show);
}

TEST(ow_dump_feebas_lines) {
    OverworldSnapshot s;
    GameMem m = mem();
    char buf[1024];
    fb_scene(true, false);
    overworld_read(&k_fb_profile, &m, &s);
    FILE* f = tmpfile();
    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(overworld_dump(&s, f));
    rewind(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    ASSERT_TRUE(strstr(buf, "[overworld]\nvisible 1\nfeebas_visible 1\n") != NULL);
    ASSERT_TRUE(strstr(buf, "marker kind=feebas x=32 y=8 name=FEEBAS spot=149 bridge=0\n") != NULL);
}

void run_overworld_tests(void) {
    RUN_TEST(ow_feebas_markers_on_screen_only);
    RUN_TEST(ow_feebas_survive_fishing_lock);
    RUN_TEST(ow_feebas_hidden_in_other_locks);
    RUN_TEST(ow_feebas_skips_message_box_while_fishing);
    RUN_TEST(ow_feebas_sliver_on_screen_still_hints);
    RUN_TEST(ow_feebas_hint_when_none_on_screen);
    RUN_TEST(ow_feebas_blocked_on_screen_still_hints);
    RUN_TEST(ow_feebas_none_off_route);
    RUN_TEST(ow_dump_feebas_lines);
    RUN_TEST(ow_gift_tag_shown_until_received);
    RUN_TEST(ow_no_gift_tags_without_cache);
    RUN_TEST(ow_gift_cache_shared_script_walks_once);
    RUN_TEST(ow_gift_cache_resets_on_map_change);
    RUN_TEST(ow_feed_clear_empties_gift_cache);
    RUN_TEST(ow_item_ball_named);
    RUN_TEST(ow_ball_sprite_with_other_script_not_tagged);
    RUN_TEST(ow_offscreen_trainer_kept_when_cone_reaches_screen);
    RUN_TEST(ow_sight_range_255_terminates);
    RUN_TEST(ow_hidden_item_on_screen);
    RUN_TEST(ow_hidden_item_collected_is_skipped);
    RUN_TEST(ow_hidden_item_follows_camera_scroll);
    RUN_TEST(ow_hidden_item_in_south_connection);
    RUN_TEST(ow_dump_lists_trainers_and_items);
    RUN_TEST(ow_sight_clear_full_range);
    RUN_TEST(ow_sight_stops_at_collision_and_undefined);
    RUN_TEST(ow_sight_elevation_rules);
    RUN_TEST(ow_sight_blocked_by_npc_not_player);
    RUN_TEST(ow_sight_one_way_tiles);
    RUN_TEST(ow_sight_secondary_tileset);
    RUN_TEST(ow_buried_looks_all_ways);
    RUN_TEST(ow_bad_grid_omits_cones_keeps_trainer);
    RUN_TEST(ow_trainer_resolved_from_save_template);
    RUN_TEST(ow_trainer_beaten_flag);
    RUN_TEST(ow_trainer_id_past_table_is_not_beaten);
    RUN_TEST(ow_trainer_position_comes_from_sprite);
    RUN_TEST(ow_unknown_script_still_listed);
    RUN_TEST(ow_skips_range0_type2_and_offscreen);
    RUN_TEST(ow_buried_trainer_flagged);
    RUN_TEST(ow_class_name_drops_gender_symbol);
    RUN_TEST(ow_template_from_rom_header_for_other_map);
    RUN_TEST(ow_null_profile_gives_empty_snapshot);
    RUN_TEST(ow_feed_capture_shifts_next_to_shown);
    RUN_TEST(ow_feed_clear_empties_both_slots);
    RUN_TEST(ow_visible_on_field);
    RUN_TEST(ow_hidden_when_callback_not_overworld);
    RUN_TEST(ow_hidden_during_fade);
    RUN_TEST(ow_hidden_while_controls_locked);
    RUN_TEST(ow_hidden_in_pyramid_and_trainer_hill);
    RUN_TEST(ow_hidden_without_save_block);
}
