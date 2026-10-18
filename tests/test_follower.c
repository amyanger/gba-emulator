#include "test_harness.h"
#include "game_fixture.h"
#include "game/follower.h"

static FollowerInput base_in(void) {
    FollowerInput in;
    memset(&in, 0, sizeof(in));
    in.ok = in.overworld = in.free = true;
    in.avatar_flags = 0x01;
    in.cur_x = in.prev_x = 10;
    in.cur_y = in.prev_y = 10;
    in.facing = 1;
    in.move_action = 0x08;
    in.loc_group = 0; in.loc_num = 33;
    in.lead_ok = true;
    in.lead.personality = 0x1234;
    in.lead.level = 20;
    in.lead.hp = 30;
    return in;
}

static void walk(Follower* f, FollowerInput* in, int dx, int dy, uint8_t action, int frames) {
    in->prev_x = in->cur_x; in->prev_y = in->cur_y;
    in->cur_x = (int16_t)(in->cur_x + dx); in->cur_y = (int16_t)(in->cur_y + dy);
    in->move_action = action;
    for (int i = 0; i < frames; i++) follower_step(f, in, false);
}

TEST(reveal_on_first_step) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.reveal_pending);
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 10);
    ASSERT_EQ(f.ty, 10);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
}

TEST(follows_previous_tile_x_first) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* reveal at (10,10), player (11,10) */
    walk(&f, &in, 0, 1, 0x08, 16);      /* player (11,11), target (11,10) */
    ASSERT_EQ(f.tx, 11);
    ASSERT_EQ(f.ty, 10);
    ASSERT_EQ(f.facing, 4);             /* moved east */
    walk(&f, &in, 0, 1, 0x35, 1);       /* run: 8 frames per tile */
    ASSERT_EQ(f.step_len, 8);
}

TEST(walking_into_follower_swaps) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* follower (10,10), player (11,10) */
    walk(&f, &in, -1, 0, 0x0A, 16);     /* player steps onto (10,10) */
    ASSERT_EQ(f.tx, 11);
    ASSERT_EQ(f.ty, 10);
}

TEST(turn_in_place_ignored) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);
    in.move_action = 0x21; in.facing = 2;
    for (int i = 0; i < 8; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.tx, 10);
    ASSERT_EQ(f.step_t, f.step_len);
}

TEST(ledge_catch_up_hop) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 0, 1, 0x08, 16);      /* follower (10,10), player (10,11) */
    /* Emerald JUMP_2 moves currentCoords one tile at frame 0 and one at frame 16. */
    walk(&f, &in, 0, 1, 0x0C, 16);      /* player (10,12) */
    walk(&f, &in, 0, 1, 0x0C, 16);      /* player (10,13): same jump, not a new step */
    ASSERT_EQ(f.ty, 11);                /* walked to pre-jump tile */
    ASSERT_TRUE(f.jump_pending);
    walk(&f, &in, 0, 1, 0x08, 1);       /* next step: catch-up hop */
    ASSERT_TRUE(!f.jump_pending);
    ASSERT_EQ(f.ty, 13);
}

TEST(connection_rebase) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);
    in.cam_active = true; in.cam_x = 40; in.cam_y = -140;
    in.prev_x = (int16_t)(in.prev_x - 40); in.cur_x = (int16_t)(in.cur_x - 40);
    in.prev_y = (int16_t)(in.prev_y + 140); in.cur_y = (int16_t)(in.cur_y + 140);
    follower_step(&f, &in, false);
    ASSERT_EQ(f.tx, 10 - 40);
    ASSERT_EQ(f.ty, 10 + 140);
}

TEST(connection_crossing_keeps_follower) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* follower (10,10), player (11,10) */
    walk(&f, &in, 1, 0, 0x0B, 1);       /* player steps to (12,10), follower to (11,10) */
    in.loc_num = 34;                    /* location rewritten first */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 11);
    in.cam_active = true; in.cam_x = 40; in.cam_y = 0;   /* next frame: coords rebased */
    in.prev_x = (int16_t)(in.prev_x - 40); in.cur_x = (int16_t)(in.cur_x - 40);
    for (int i = 0; i < 14; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 11 - 40);
    ASSERT_EQ(f.ty, 10);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* player (13-40,10) */
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 12 - 40);
}

TEST(warp_after_fade_hides) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);
    in.fade_active = true; in.loc_num = 34; in.cur_x = 3; in.cur_y = 4;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.reveal_pending);
    ASSERT_EQ(f.tx, 3);
}

TEST(lead_change_clears_stale_jump) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 0, 1, 0x08, 16);      /* follower (10,10), player (10,11) */
    walk(&f, &in, 0, 1, 0x0C, 16);      /* ledge jump arms the catch-up hop */
    walk(&f, &in, 0, 1, 0x0C, 16);      /* player (10,13) */
    ASSERT_TRUE(f.jump_pending);
    in.lead.personality = 0x9999;       /* lead swapped */
    follower_step(&f, &in, false);
    ASSERT_TRUE(!f.jump_pending);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* reveal at (10,13), player (11,13) */
    walk(&f, &in, 1, 0, 0x0B, 16);      /* player (12,13) */
    ASSERT_EQ(f.tx, 11);
    ASSERT_EQ(f.ty, 13);
}

TEST(reset_hides_and_arms_reveal) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 4);       /* mid-step */
    follower_reset(&f);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.reveal_pending);
    ASSERT_EQ(f.step_t, 0);
    ASSERT_TRUE(!f.p_valid);
}

TEST(bike_and_too_tall_indoors_use_ball) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);
    in.avatar_flags = 0x02;
    for (int i = 0; i <= FOL_BALL_IN_TOTAL; i++) follower_step(&f, &in, false);
    ASSERT_TRUE(f.in_ball);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    in.avatar_flags = 0x01;
    follower_step(&f, &in, false);
    ASSERT_TRUE(f.reveal_pending);
    in.map_type = 8;
    follower_step(&f, &in, true);
    ASSERT_TRUE(f.in_ball);
}

TEST(no_lead_hides) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.lead_ok = false;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_OFF);
}

TEST(talk_ignores_a_that_unlocked_controls) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* follower (10,10) west of player (11,10) */
    in.facing = 3;                      /* face west */
    in.free = false; in.lock = 1;       /* text box open */
    follower_step(&f, &in, false);
    in.free = true; in.lock = 0; in.new_keys = 1;   /* A closes it and unlocks this frame */
    follower_step(&f, &in, false);
    ASSERT_TRUE(!f.talk_request);
    in.new_keys = 0;
    follower_step(&f, &in, false);
    in.new_keys = 1;
    follower_step(&f, &in, false);
    ASSERT_TRUE(f.talk_request);
}

TEST(mood_events) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    in.lead.level = 21;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.mood_delta, 40);
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.steps, 1);
}

TEST(battle_hides_then_restores_in_place) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);
    in.overworld = false;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.p_in_battle);
    in.overworld = true;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 10);
    ASSERT_TRUE(!f.p_in_battle);
}

TEST(lead_faint_in_battle_sets_faint_pid) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    in.overworld = false;
    in.party_hp_zero = 0x01;            /* the lead (slot 0) faints */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.faint_pid, 0);
    in.overworld = true;
    in.party_hp_zero = 0x00;            /* revived before the battle ended */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.faint_pid, 0);
    in.overworld = false;
    in.party_hp_zero = 0x03;            /* lead and slot 1 faint; slot 2 leads afterwards */
    follower_step(&f, &in, false);
    in.overworld = true;
    in.lead.personality = 0x5678;
    in.lead_slot = 2;
    follower_step(&f, &in, false);
    ASSERT_EQ_HEX(f.faint_pid, 0x1234);
    ASSERT_EQ(f.mood_delta, 0);
    ASSERT_TRUE(f.lead_changed);
    follower_step(&f, &in, false);
    ASSERT_EQ(f.faint_pid, 0);          /* one frame only */
}

TEST(transition_hides_but_locked_controls_do_not) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);
    in.free = false; in.lock = 1;       /* text box or start menu */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    in.in_transition = true;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_EQ(f.tx, 10);
    in.in_transition = false; in.free = true; in.lock = 0;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 10);
}

TEST(feet_interpolate_and_frame) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.origin.pos_x = 3; in.origin.pos_y = 5; in.origin.cam_dy = -8;
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* follower (10,10) */
    walk(&f, &in, 1, 0, 0x08, 4);       /* follower stepping east, 4 of 16 frames */
    int16_t x, y;
    follower_feet(&f, &in, &x, &y);
    ASSERT_EQ(x, (11 - 3) * 16 + 8 - 12);
    ASSERT_EQ(y, (10 - 5) * 16 - 8 + 16);
    ASSERT_EQ(follower_frame(&f) & ~1, 6);
}

/* follower_read fixture */
static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x1000];

#define SB1       0x02010000u
#define OBJS      0x02001000u
#define PARTY     0x02002000u
#define AVATAR    0x02003000u
#define CAMERA    0x02003100u
#define FADE      0x02003200u
#define WEATHER   0x02004000u
#define MAP_HDR   0x02003300u
#define TRANS     0x02003400u
#define CB2       0x03000400u
#define LOCK      0x03000500u
#define SCRIPT_ST 0x03000600u
#define KEYS      0x03000700u
/* ObjectEventUpdateElevation: tiles with elevation 0 or 15 keep the previous one. */
TEST(elevation_keeps_previous_on_0_and_15) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.player_elevation = 3;
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* reveal at (10,10) with the player's elevation */
    ASSERT_EQ(f.elevation, 3);
    in.fol_x = 10; in.fol_y = 10;
    in.fol_elevation = 15;              /* bridge edge / multi-level tile */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.elevation, 3);
    in.fol_elevation = 0;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.elevation, 3);
    in.fol_elevation = 4;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.elevation, 4);
    in.fol_x = 9;                       /* read at some other tile: ignored */
    in.fol_elevation = 6;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.elevation, 4);
}

/* Fly to the current town or a door on the same map: no location change, a coordinate jump. */
TEST(same_map_warp_hides) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* follower (10,10), player (11,10) */
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    in.overworld = false;               /* fly map screen */
    follower_step(&f, &in, false);
    in.overworld = true;
    in.cur_x = in.prev_x = 30; in.cur_y = in.prev_y = 5;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.reveal_pending);
    ASSERT_EQ(f.tx, 30);
    ASSERT_EQ(f.ty, 5);
    walk(&f, &in, 1, 0, 0x0B, 1);       /* reappears behind the player */
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 30);
}

/* A failed read reports cam_active false; it must not arm a camera rebase. */
TEST(failed_read_keeps_camera_edge) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.cam_active = true;
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 16);      /* follower (10,10) */
    FollowerInput bad;
    memset(&bad, 0, sizeof(bad));
    follower_step(&f, &bad, false);
    in.cam_x = 40;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.tx, 10);
}

#define CB2_FIELD 0x08000101u

static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules,
    .save_block1_ptr = 0x03000000,
    .player_party = PARTY,
    .map_header = MAP_HDR,
    .object_events = OBJS,
    .field_camera = 0x03000100, .camera_pan = 0x03000200, .sprite_coord_offset = 0x03000210,
    .backup_map_layout = 0x03000300, .backup_map_data = 0x02020000,
    .main_callback2 = CB2, .cb2_overworld = CB2_FIELD,
    .palette_fade_byte = FADE + 7, .lock_field_controls = LOCK,
    .player_avatar = AVATAR, .camera = CAMERA, .palette_fade = FADE, .weather = WEATHER,
    .script_status = SCRIPT_ST, .main_new_keys = KEYS, .transition_data = TRANS,
    .approaching_trainers = 0x03000800, .last_talked = 0x02005000, .species_info = 0x08000000,
};

static uint8_t* at(uint32_t a) {
    if (a >= 0x08000000u) return s_rom + (a - 0x08000000u);
    if (a >= 0x03000000u) return s_iwram + (a - 0x03000000u);
    return s_ewram + (a - 0x02000000u);
}
static void put8(uint32_t a, uint8_t v) { *at(a) = v; }
static void put16(uint32_t a, uint16_t v) { fixture_put16(at(a), v); }
static void put32(uint32_t a, uint32_t v) { fixture_put32(at(a), v); }

static void put_mon(int slot, uint32_t pid, uint8_t flags, uint8_t level, uint16_t hp) {
    uint8_t plain[4][12], raw[100];
    fixture_plain_mon(plain, 25);
    fixture_encode_mon(raw, pid, 0x11112222u, plain, flags);
    raw[0x54] = level;
    fixture_put16(raw + 0x56, hp);
    fixture_put16(raw + 0x58, 40);
    memcpy(at(PARTY + 100u * (uint32_t)slot), raw, 100);
}

static void read_scene(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    put32(0x03000000, SB1);
    put16(SB1 + 0, 10); put16(SB1 + 2, 20);
    put8(SB1 + 4, 0); put8(SB1 + 5, 33);
    put32(CB2, CB2_FIELD);
    put8(AVATAR + 0, 0x01); put8(AVATAR + 3, 2); put8(AVATAR + 5, 0);
    put8(OBJS + 0x00, 0x01);
    put8(OBJS + 0x0B, 0x34);              /* current elevation 4, previous 3 */
    put16(OBJS + 0x10, 17); put16(OBJS + 0x12, 27);
    put16(OBJS + 0x14, 16); put16(OBJS + 0x16, 27);
    put8(OBJS + 0x18, 0x44);
    put8(OBJS + 0x1C, 0x0B);
    put8(MAP_HDR + 0x14, 0x10); put8(MAP_HDR + 0x17, 3);
    put8(CAMERA + 0, 1); put32(CAMERA + 4, 0xFFFFFFFEu); put32(CAMERA + 8, 3);
    put16(KEYS, 1);
    put8(WEATHER + 0x6D0, 2); put16(WEATHER + 0x6C4, 0x7FFF);
    put_mon(0, 0xAAAA0001u, 0x02 | 0x04, 5, 0);   /* egg */
    put_mon(1, 0xAAAA0002u, 0x02, 12, 0);         /* fainted */
    put_mon(2, 0xAAAA0003u, 0x02, 30, 25);
}

TEST(read_lead_skips_egg_and_fainted) {
    FollowerInput in;
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    read_scene();
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(in.ok && in.overworld && in.free);
    ASSERT_TRUE(in.lead_ok);
    ASSERT_EQ(in.lead_slot, 2);
    ASSERT_EQ_HEX(in.lead.personality, 0xAAAA0003u);
    ASSERT_EQ(in.lead.level, 30);
    ASSERT_EQ(in.party_hp_zero, 0x02);
    ASSERT_EQ(in.cur_x, 17); ASSERT_EQ(in.cur_y, 27);
    ASSERT_EQ(in.prev_x, 16); ASSERT_EQ(in.prev_y, 27);
    ASSERT_EQ(in.facing, 4);
    ASSERT_EQ(in.move_action, 0x0B);
    ASSERT_EQ(in.tile_state, 2);
    ASSERT_EQ(in.loc_num, 33);
    ASSERT_EQ(in.mapsec, 0x10); ASSERT_EQ(in.map_type, 3);
    ASSERT_TRUE(in.cam_active); ASSERT_EQ(in.cam_x, -2); ASSERT_EQ(in.cam_y, 3);
    ASSERT_EQ(in.new_keys, 1);
    ASSERT_EQ(in.weather, 2); ASSERT_EQ_HEX(in.weather_color, 0x7FFF);
    ASSERT_EQ(in.origin.pos_x, 10); ASSERT_EQ(in.origin.pos_y, 20);
    ASSERT_EQ(in.player_elevation, 4);
    ASSERT_EQ(in.fol_x, 16); ASSERT_EQ(in.fol_y, 27);
}

TEST(read_free_needs_no_fade) {
    FollowerInput in;
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    read_scene();
    put16(FADE + 4, 16 << 6);
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_EQ(in.fade_y, 16);
    ASSERT_TRUE(!in.fade_active);
    ASSERT_TRUE(!in.free);
    put16(FADE + 4, 0);
    put16(FADE + 6, 0x8000 | 0x1234);
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(in.fade_active);
    ASSERT_EQ_HEX(in.fade_color, 0x1234);
    ASSERT_TRUE(!in.free);
    put16(FADE + 6, 0);
    put32(TRANS, 0x02030000u);
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(in.in_transition);
    ASSERT_TRUE(!in.free);
}

TEST(read_npc_on_tile_and_invisible_player) {
    FollowerInput in;
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    read_scene();
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(!in.fol_blocked);        /* the player's own previous tile does not count */
    ASSERT_TRUE(!in.player_invisible);
    put8(OBJS + 0x24, 0x01);
    put16(OBJS + 0x24 + 0x10, 16); put16(OBJS + 0x24 + 0x12, 27);
    put8(OBJS + 0x25, 0x20);             /* invisible NPC */
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(!in.fol_blocked);
    put8(OBJS + 0x25, 0x00);
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(in.fol_blocked);
    put8(OBJS + 0x01, 0x20);
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_TRUE(in.player_invisible);
}

TEST(read_event_inputs) {
    FollowerInput in;
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    read_scene();
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_EQ(in.talked_gfx, 0xFF);
    ASSERT_EQ_HEX(in.sb1, SB1);
    put8(0x03000800, 2);
    put16(0x02005000, 3);
    put8(WEATHER + 0x6D1, 5);
    put8(0x08000000 + 25 * 28 + 6, 10); put8(0x08000000 + 25 * 28 + 7, 2);
    put8(OBJS + 0x24, 0x01);             /* local id 3 on this map, graphics 0x2A */
    put8(OBJS + 0x24 + 0x05, 0x2A);
    put8(OBJS + 0x24 + 0x08, 3); put8(OBJS + 0x24 + 0x09, 33); put8(OBJS + 0x24 + 0x0A, 0);
    put8(OBJS + 0x48, 0x01);             /* same local id, other map */
    put8(OBJS + 0x48 + 0x05, 0x11);
    put8(OBJS + 0x48 + 0x08, 3); put8(OBJS + 0x48 + 0x09, 34);
    uint32_t fl = SB1 + 0x1270;
    put8(fl + (0x1F4 >> 3), 0xFF);       /* bits below 0x1F4 are not item flags */
    put8(fl + (0x263 >> 3), 0xFF);
    put8(fl + (0x3E8 >> 3), 0x01);
    put8(fl + (0x492 >> 3), 0xFF);
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_EQ(in.trainers, 2);
    ASSERT_EQ(in.last_talked, 3);
    ASSERT_EQ(in.talked_gfx, 0x2A);
    ASSERT_EQ(in.next_weather, 5);
    put8(OBJS + 0x24 + 0x06, 0x0C);      /* berry tree with a berry: graphicsId is the berry's */
    ASSERT_TRUE(follower_read(&k_profile, &m, 16, 27, &in));
    ASSERT_EQ(in.talked_gfx, OBJ_EVENT_GFX_BERRY_TREE);
    put8(OBJS + 0x24 + 0x06, 0);
    ASSERT_EQ(in.lead_type1, 10); ASSERT_EQ(in.lead_type2, 2);
    ASSERT_EQ_HEX(in.item_flags[0], 0xF0);
    ASSERT_EQ_HEX(in.item_flags[14], 0x0F);
    ASSERT_EQ_HEX(in.item_flags[15], 0x01);
    ASSERT_EQ_HEX(in.item_flags[ITEM_FLAG_BYTES - 1], 0x07);
}

static void settle(Follower* f, FollowerInput* in) {
    follower_step(f, in, false);
    walk(f, in, 1, 0, 0x0B, 16);        /* follower (10,10), player (11,10) */
}

TEST(mount_from_visible_plays_ball_in) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    in.avatar_flags = 0x02;
    for (int i = 0; i <= FOL_BALL_IN_TOTAL; i++) {
        follower_step(&f, &in, false);
        if (i < FOL_BALL_IN_TOTAL) {
            ASSERT_EQ(f.ball_fx, FOL_BALL_IN);
            ASSERT_EQ(f.ball_t, i);
            ASSERT_EQ(f.vis, FOL_VISIBLE);
            ASSERT_TRUE(!f.in_ball);
            ASSERT_EQ(f.tx, 10);
            ASSERT_EQ(f.ty, 10);
        }
    }
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.in_ball);
    ASSERT_EQ(f.tx, 11);
}

TEST(follower_does_not_step_during_ball_in) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.avatar_flags = 0x02;
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 2);       /* player keeps moving on the bike */
    ASSERT_EQ(f.ball_fx, FOL_BALL_IN);
    ASSERT_EQ(f.tx, 10);
    ASSERT_EQ(f.ty, 10);
}

TEST(mount_while_hidden_has_no_effect) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);      /* hidden, reveal pending */
    in.avatar_flags = 0x02;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
    ASSERT_TRUE(f.in_ball);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
}

TEST(dismount_plays_ball_out) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.avatar_flags = 0x02;
    for (int i = 0; i <= FOL_BALL_IN_TOTAL; i++) follower_step(&f, &in, false);
    in.avatar_flags = 0x01;
    follower_step(&f, &in, false);
    ASSERT_TRUE(f.reveal_pending);
    ASSERT_TRUE(f.reveal_from_ball);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
    walk(&f, &in, 1, 0, 0x0B, 1);       /* first step reveals */
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_TRUE(!f.reveal_from_ball);
    for (int i = 0; i < FOL_BALL_OUT_TOTAL; i++) {
        ASSERT_EQ(f.ball_fx, FOL_BALL_OUT);
        ASSERT_EQ(f.ball_t, i);
        follower_step(&f, &in, false);
    }
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
}

TEST(warp_and_lead_change_reveal_have_no_effect) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.overworld = false;
    follower_step(&f, &in, false);
    in.overworld = true;
    in.cur_x = in.prev_x = 30; in.cur_y = in.prev_y = 5;
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);

    in.lead.personality = 0x9999;
    follower_step(&f, &in, false);
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
}

TEST(warp_during_bike_ride_drops_ball_reveal) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    follower_step(&f, &in, false);
    in.avatar_flags = 0x02;
    follower_step(&f, &in, false);
    in.avatar_flags = 0x01;
    in.fade_active = true; in.loc_num = 34; in.cur_x = 3; in.cur_y = 4;
    follower_step(&f, &in, false);      /* dismounts and warps in one frame */
    ASSERT_TRUE(!f.reveal_from_ball);
    in.fade_active = false;
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
}

TEST(reset_mid_effect_clears_it) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.avatar_flags = 0x02;
    for (int i = 0; i < 5; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.ball_fx, FOL_BALL_IN);
    follower_reset(&f);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
    ASSERT_EQ(f.ball_t, 0);
    ASSERT_TRUE(!f.reveal_from_ball);
}

TEST(new_map_mid_effect_clears_it) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.avatar_flags = 0x02;
    for (int i = 0; i < 5; i++) follower_step(&f, &in, false);
    in.fade_active = true; in.loc_num = 34; in.cur_x = 3; in.cur_y = 4;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
}

TEST(talk_ignored_during_ball_effect) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.facing = 3;
    in.new_keys = 0;
    follower_step(&f, &in, false);
    in.avatar_flags = 0x02;
    follower_step(&f, &in, false);      /* ball in starts */
    in.new_keys = 1;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.ball_fx, FOL_BALL_IN);
    ASSERT_TRUE(!f.talk_request);
}

TEST(heal_recalls_until_next_step) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.pokecenter_heals = 1;            /* the nurse takes the party */
    for (int i = 0; i <= FOL_BALL_IN_TOTAL; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.in_ball);
    for (int i = 0; i < 60; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);       /* stays in until the player moves */
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.ball_fx, FOL_BALL_OUT);
}

TEST(npc_on_follower_tile_recalls) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.fol_x = f.tx; in.fol_y = f.ty;
    in.fol_blocked = true;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.ball_fx, FOL_BALL_IN);
    in.fol_blocked = false;
    for (int i = 0; i < FOL_BALL_IN_TOTAL; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    walk(&f, &in, 1, 0, 0x0B, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    ASSERT_EQ(f.tx, 11);                /* back out behind the player, off the NPC */
}

TEST(stale_blocked_tile_is_ignored) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.fol_x = (int16_t)(f.tx + 1); in.fol_y = f.ty;
    in.fol_blocked = true;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
}

TEST(player_into_door_follower_walks_in_and_hides) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.player_invisible = true;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.tx, 11);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
    for (int i = 0; i < 16; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.reveal_pending);
    ASSERT_EQ(f.ball_fx, FOL_BALL_NONE);
}

/* Mr. Briney's boat hides the player and moves it along the water with the boat. */
TEST(invisible_player_moving_keeps_follower_hidden) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    settle(&f, &in);
    in.player_invisible = true;
    for (int i = 0; i < 17; i++) follower_step(&f, &in, false);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    for (int i = 0; i < 5; i++) walk(&f, &in, 0, -1, 0x0A, 8);
    ASSERT_EQ(f.vis, FOL_HIDDEN);
    ASSERT_TRUE(f.reveal_pending);
    in.player_invisible = false;  /* stepped off the boat */
    walk(&f, &in, 0, -1, 0x0A, 1);
    ASSERT_EQ(f.vis, FOL_VISIBLE);
}

static void steps(Follower* f, const FollowerInput* in, int n) {
    for (int i = 0; i < n; i++) follower_step(f, in, false);
}

TEST(ev_trainer_rise) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    steps(&f, &in, 2);
    in.trainers = 1;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_TRAINER);
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.trainers = 0;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.trainers = 1;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_TRAINER);
}

TEST(ev_npc_talk) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.talked_gfx = 0x2A;
    steps(&f, &in, 2);
    in.lock = 1; in.free = false; in.last_talked = 3;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NPC_TALK);
    ASSERT_EQ(f.event_arg, 0x2A);
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.lock = 0; in.free = true; in.last_talked = 0;
    steps(&f, &in, 2);
    in.lock = 1; in.free = false;               /* sign: no talked object */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.lock = 0; in.free = true; in.last_talked = 3;
    steps(&f, &in, 2);
    in.lock = 1; in.free = false; in.trainers = 1;   /* trainer sighting, not a talk */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_TRAINER);
}

TEST(ev_item_flag) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.sb1 = 0x02025A00;
    in.item_flags[3] = 0x01;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);            /* first frame: no baseline */
    in.lock = 1; in.free = false;
    follower_step(&f, &in, false);
    in.item_flags[20] |= 0x10;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_ITEM);            /* allowed while locked */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.item_flags[5] = 0x02; in.sb1 = 0x02025B00;   /* save block moved */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.item_flags[6] = 0x02; in.loc_num = 34;   /* location changed */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    follower_reset(&f);
    in.item_flags[7] = 0x04;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.overworld = false;                       /* battle, then back */
    follower_step(&f, &in, false);
    in.overworld = true; in.item_flags[8] = 0x08;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
}

TEST(ev_weather) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.weather = in.next_weather = 2;
    steps(&f, &in, 61);
    in.weather = in.next_weather = 3;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_WEATHER);
    ASSERT_EQ(f.event_arg, 3);
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.weather = 5; in.next_weather = 4;        /* not settled yet */
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.next_weather = 5;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_WEATHER);
    ASSERT_EQ(f.event_arg, 5);
    in.loc_num = 34;                            /* connection: weather lags the map */
    steps(&f, &in, 30);
    in.weather = in.next_weather = 2;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    steps(&f, &in, 61);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.free = false; in.lock = 1; in.weather = in.next_weather = 8;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.free = true; in.lock = 0;
    steps(&f, &in, 70);
    ASSERT_EQ(f.event, FOL_EV_NONE);
}

TEST(ev_weather_slow_connection) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    in.weather = in.next_weather = 3;
    steps(&f, &in, 61);
    in.loc_num = 34;                            /* leaving rain finishes after the settle window */
    steps(&f, &in, 10);
    in.next_weather = 2;
    steps(&f, &in, 60);
    ASSERT_EQ(f.event, FOL_EV_NONE);
    in.weather = 2;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
}

TEST(ev_cleared_off_overworld) {
    Follower f;
    follower_init(&f);
    FollowerInput in = base_in();
    steps(&f, &in, 2);
    in.trainers = 1;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_TRAINER);
    in.overworld = false;
    follower_step(&f, &in, false);
    ASSERT_EQ(f.event, FOL_EV_NONE);
}

void run_follower_tests(void) {
    RUN_TEST(reveal_on_first_step);
    RUN_TEST(follows_previous_tile_x_first);
    RUN_TEST(walking_into_follower_swaps);
    RUN_TEST(turn_in_place_ignored);
    RUN_TEST(ledge_catch_up_hop);
    RUN_TEST(connection_rebase);
    RUN_TEST(connection_crossing_keeps_follower);
    RUN_TEST(warp_after_fade_hides);
    RUN_TEST(lead_change_clears_stale_jump);
    RUN_TEST(reset_hides_and_arms_reveal);
    RUN_TEST(bike_and_too_tall_indoors_use_ball);
    RUN_TEST(no_lead_hides);
    RUN_TEST(talk_ignores_a_that_unlocked_controls);
    RUN_TEST(mood_events);
    RUN_TEST(battle_hides_then_restores_in_place);
    RUN_TEST(lead_faint_in_battle_sets_faint_pid);
    RUN_TEST(transition_hides_but_locked_controls_do_not);
    RUN_TEST(feet_interpolate_and_frame);
    RUN_TEST(elevation_keeps_previous_on_0_and_15);
    RUN_TEST(same_map_warp_hides);
    RUN_TEST(failed_read_keeps_camera_edge);
    RUN_TEST(read_lead_skips_egg_and_fainted);
    RUN_TEST(read_free_needs_no_fade);
    RUN_TEST(read_npc_on_tile_and_invisible_player);
    RUN_TEST(mount_from_visible_plays_ball_in);
    RUN_TEST(follower_does_not_step_during_ball_in);
    RUN_TEST(mount_while_hidden_has_no_effect);
    RUN_TEST(dismount_plays_ball_out);
    RUN_TEST(warp_and_lead_change_reveal_have_no_effect);
    RUN_TEST(warp_during_bike_ride_drops_ball_reveal);
    RUN_TEST(reset_mid_effect_clears_it);
    RUN_TEST(new_map_mid_effect_clears_it);
    RUN_TEST(talk_ignored_during_ball_effect);
    RUN_TEST(heal_recalls_until_next_step);
    RUN_TEST(npc_on_follower_tile_recalls);
    RUN_TEST(stale_blocked_tile_is_ignored);
    RUN_TEST(player_into_door_follower_walks_in_and_hides);
    RUN_TEST(invisible_player_moving_keeps_follower_hidden);
    RUN_TEST(ev_trainer_rise);
    RUN_TEST(ev_npc_talk);
    RUN_TEST(ev_item_flag);
    RUN_TEST(ev_weather);
    RUN_TEST(ev_weather_slow_connection);
    RUN_TEST(ev_cleared_off_overworld);
    RUN_TEST(read_event_inputs);
}
