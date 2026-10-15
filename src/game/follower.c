#include "game/follower.h"
#include "game/emerald_layout.h"
#include <string.h>
#include "gba.h"

/* Struct offsets from pret/pokeemerald (global.fieldmap.h, palette.h, field_weather.h). */
#define AVATAR_FLAGS       0x00
#define AVATAR_TILE_STATE  0x03
#define AVATAR_OBJ_ID      0x05
#define OBJ_ACTION         0x1C
#define MAP_HEADER_TYPE    0x17
#define CAM_X              0x04
#define CAM_Y              0x08
#define FADE_Y             0x04   /* u16, bits 6-10 */
#define FADE_COLOR         0x06   /* u16, bits 0-14 color, bit 15 active */
#define WEATHER_COLOR      0x6C4
#define WEATHER_STATE      0x6C6
#define WEATHER_COUNTER    0x6C7
#define WEATHER_CURRENT    0x6D0
#define PARTY_SLOTS        6

#define AVATAR_ON_BIKE_OR_WATER (0x02 | 0x04 | 0x08 | 0x10)
#define MAP_TYPE_INDOOR    8
#define MAP_TYPE_SECRET_BASE 9
#define HOP_PEAK           12

static int16_t s16_at(const uint8_t* b) { return (int16_t)(b[0] | (b[1] << 8)); }

static bool read_s32(const GameMem* m, uint32_t addr, int32_t* out) {
    uint32_t v = 0;
    if (!game_mem_read32(m, addr, &v)) return false;
    *out = (int32_t)v;
    return true;
}

static void read_party(const GameProfile* p, const GameMem* m, FollowerInput* out) {
    for (uint8_t i = 0; i < PARTY_SLOTS; i++) {
        uint8_t raw[MON_PARTY_SIZE];
        MonData mon;
        if (!game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, sizeof(raw)) ||
            !pokemon_decode(raw, true, &mon) || mon.bad || mon.is_egg)
            continue;
        if (mon.hp == 0) {
            out->party_hp_zero |= (uint8_t)(1u << i);
        } else if (!out->lead_ok) {
            out->lead_ok = true;
            out->lead_slot = i;
            out->lead = mon;
        }
    }
}

bool follower_read(const GameProfile* p, const GameMem* m, int16_t fol_x, int16_t fol_y, FollowerInput* out) {
    uint32_t cb2 = 0, sb1 = 0, transition = 0;
    uint8_t av[6], obj[OBJ_SIZE], cam_active = 0;
    uint16_t fade_y = 0, fade_color = 0;

    memset(out, 0, sizeof(*out));
    if (!p || !game_mem_read32(m, p->main_callback2, &cb2) || !game_mem_copy(m, p->player_avatar, av, sizeof(av)) ||
        av[AVATAR_OBJ_ID] >= OBJ_COUNT ||
        !game_mem_copy(m, p->object_events + (uint32_t)av[AVATAR_OBJ_ID] * OBJ_SIZE, obj, sizeof(obj)) ||
        !game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_read8(m, sb1 + SB1_LOC_GROUP, &out->loc_group) ||
        !game_mem_read8(m, sb1 + SB1_LOC_NUM, &out->loc_num) || !overworld_screen_origin(p, m, &out->origin))
        return false;

    out->overworld = cb2 == p->cb2_overworld;
    out->avatar_flags = av[AVATAR_FLAGS];
    out->tile_state = av[AVATAR_TILE_STATE];
    out->obj_flags0 = obj[0];
    out->cur_x = s16_at(obj + OBJ_CUR);
    out->cur_y = s16_at(obj + OBJ_CUR + 2);
    out->prev_x = s16_at(obj + OBJ_PREV);
    out->prev_y = s16_at(obj + OBJ_PREV + 2);
    out->facing = obj[OBJ_DIRS] & 0x0F;
    out->move_action = obj[OBJ_ACTION];
    out->player_elevation = obj[OBJ_ELEVATION] & 0x0F;

    game_mem_read8(m, p->map_header + MAP_HEADER_REGION_MAP_SEC, &out->mapsec);
    game_mem_read8(m, p->map_header + MAP_HEADER_TYPE, &out->map_type);
    game_mem_read8(m, p->camera, &cam_active);
    out->cam_active = cam_active != 0;
    read_s32(m, p->camera + CAM_X, &out->cam_x);
    read_s32(m, p->camera + CAM_Y, &out->cam_y);
    game_mem_read16(m, p->main_new_keys, &out->new_keys);
    game_mem_read8(m, p->lock_field_controls, &out->lock);
    game_mem_read8(m, p->script_status, &out->script_status);
    game_mem_read32(m, p->transition_data, &transition);

    game_mem_read32(m, p->palette_fade, &out->fade_mask);
    game_mem_read16(m, p->palette_fade + FADE_Y, &fade_y);
    game_mem_read16(m, p->palette_fade + FADE_COLOR, &fade_color);
    out->fade_y = (uint8_t)((fade_y >> 6) & 0x1F);
    out->fade_color = fade_color & 0x7FFF;
    out->fade_active = (fade_color & 0x8000) != 0;

    game_mem_read16(m, p->weather + WEATHER_COLOR, &out->weather_color);
    game_mem_read8(m, p->weather + WEATHER_STATE, &out->weather_state);
    game_mem_read8(m, p->weather + WEATHER_COUNTER, &out->weather_counter);
    game_mem_read8(m, p->weather + WEATHER_CURRENT, &out->weather);

    out->fol_x = fol_x;
    out->fol_y = fol_y;
    overworld_tile_info(p, m, fol_x, fol_y, &out->fol_behavior, &out->fol_elevation);
    read_party(p, m, out);

    out->in_transition = transition != 0;
    out->free = out->overworld && out->lock == 0 && !out->fade_active && out->fade_y == 0 && !out->in_transition;
    out->ok = true;
    return true;
}

void follower_init(Follower* f) {
    memset(f, 0, sizeof(*f));
    f->vis = FOL_OFF;
    f->facing = 1;
}

void follower_reset(Follower* f) {
    /* Keep the lead so a savestate load or toggle is not mistaken for a lead change. */
    uint32_t pid = f->lead_pid;
    uint8_t level = f->lead_level, facing = f->facing;
    follower_init(f);
    f->lead_pid = pid;
    f->lead_level = level;
    f->facing = facing;
    f->vis = FOL_HIDDEN;
    f->reveal_pending = true;
}

static void clear_ball_fx(Follower* f) {
    f->ball_fx = FOL_BALL_NONE;
    f->ball_t = 0;
    f->reveal_from_ball = false;
}

/* Frames per tile for the player's movement action (event_object_movement.c). */
static uint8_t action_frames(uint8_t a) {
    if (a >= 0x04 && a <= 0x07) return 32;   /* WALK_SLOW */
    if (a >= 0x08 && a <= 0x0B) return 16;   /* WALK_NORMAL */
    if (a >= 0x15 && a <= 0x18) return 8;    /* WALK_FAST */
    if (a >= 0x29 && a <= 0x2C) return 6;    /* water current / Acro */
    if (a >= 0x2D && a <= 0x30) return 4;    /* WALK_FASTER */
    if (a >= 0x35 && a <= 0x38) return 8;    /* PLAYER_RUN */
    return 16;                               /* incl. JUMP_2, 16 per tile */
}

static bool is_jump(uint8_t a) { return a >= 0x0C && a <= 0x0F; }

static uint8_t faster_tier(uint8_t frames) {
    if (frames >= 32) return 16;
    if (frames >= 16) return 8;
    return 4;
}

static void dir_delta(uint8_t dir, int16_t* dx, int16_t* dy) {
    *dx = dir == 3 ? -1 : dir == 4 ? 1 : 0;
    *dy = dir == 1 ? 1 : dir == 2 ? -1 : 0;
}

static void place(Follower* f, const FollowerInput* in, int16_t x, int16_t y) {
    f->elevation = in->player_elevation;
    f->tx = f->from_x = x;
    f->ty = f->from_y = y;
    f->step_t = f->step_len = 0;
    f->hop_y = 0;
    f->jump_pending = false;
    f->jump_dir = 0;
}

static void start_step(Follower* f, int16_t nx, int16_t ny, uint8_t len) {
    int16_t dx = (int16_t)(nx - f->tx), dy = (int16_t)(ny - f->ty);
    f->facing = dx > 0 ? 4 : dx < 0 ? 3 : dy > 0 ? 1 : 2;
    f->from_x = f->tx;
    f->from_y = f->ty;
    f->tx = nx;
    f->ty = ny;
    f->step_len = len;
    f->step_t = 0;
}

static void on_player_step(Follower* f, const FollowerInput* in) {
    f->steps++;
    if (f->reveal_pending && !f->in_ball) {
        place(f, in, in->prev_x, in->prev_y);
        f->vis = FOL_VISIBLE;
        f->reveal_pending = false;
        if (f->reveal_from_ball) {
            f->ball_fx = FOL_BALL_OUT;
            f->ball_t = 0;
        }
        f->reveal_from_ball = false;
        return;
    }
    if (f->vis != FOL_VISIBLE || f->ball_fx == FOL_BALL_IN) return;
    if (f->jump_pending && is_jump(in->move_action)) return;   /* second half of the same jump */
    if (f->jump_pending) {
        int16_t dx, dy;
        dir_delta(f->jump_dir, &dx, &dy);
        f->jump_pending = false;
        f->jump_dir = 0;
        if (dx || dy) {
            /* step_len covers both tiles of the hop */
            start_step(f, (int16_t)(f->tx + 2 * dx), (int16_t)(f->ty + 2 * dy),
                       (uint8_t)(2 * faster_tier(action_frames(in->move_action))));
            return;
        }
    }
    if (is_jump(in->move_action)) {
        f->jump_pending = true;
        f->jump_dir = in->facing;
    }
    if (f->tx == in->prev_x && f->ty == in->prev_y) return;
    int16_t nx = f->tx, ny = f->ty;
    if (f->tx != in->prev_x)
        nx = (int16_t)(f->tx + (in->prev_x > f->tx ? 1 : -1));
    else
        ny = (int16_t)(f->ty + (in->prev_y > f->ty ? 1 : -1));
    start_step(f, nx, ny, is_jump(in->move_action) ? 32 : action_frames(in->move_action));
}

static bool is_hop(const Follower* f) {
    int32_t dx = f->tx - f->from_x, dy = f->ty - f->from_y;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) == 2;
}

static void store_edges(Follower* f, const FollowerInput* in) {
    f->p_valid = true;
    f->p_free = in->free;
    f->p_cam_active = in->cam_active;
    f->p_in_battle = false;
    f->p_cur_x = in->cur_x;
    f->p_cur_y = in->cur_y;
    f->p_loc_group = in->loc_group;
    f->p_loc_num = in->loc_num;
    f->p_party_hp_zero = in->party_hp_zero;
    if (in->lead_ok) f->p_lead_slot = in->lead_slot;
}

void follower_step(Follower* f, const FollowerInput* in, bool too_tall) {
    f->talk_request = false;
    f->mood_delta = 0;
    f->faint_pid = 0;
    f->steps = 0;
    f->lead_changed = false;

    /* Off the overworld: keep the tile, coordinates and party mask from the last
     * overworld frame so a warp or a faint is seen when the field returns. */
    if (!in->ok || !in->overworld) {
        if (f->vis == FOL_VISIBLE) f->vis = FOL_HIDDEN;
        if (in->ok) {
            f->p_in_battle = true;
            f->p_cam_active = in->cam_active;
        }
        f->p_free = false;
        return;
    }

    if (!in->lead_ok) {
        f->vis = FOL_OFF;
        store_edges(f, in);
        return;
    }
    uint32_t prev_lead_pid = f->lead_pid;
    if (in->lead.personality != f->lead_pid || f->vis == FOL_OFF) {
        f->lead_changed = in->lead.personality != f->lead_pid;
        f->lead_pid = in->lead.personality;
        f->lead_level = in->lead.level;
        f->reveal_pending = true;
        clear_ball_fx(f);
        f->vis = FOL_HIDDEN;
        place(f, in, in->cur_x, in->cur_y);
    } else if (in->lead.level > f->lead_level) {
        f->mood_delta += 40;
        f->lead_level = in->lead.level;
    }

    uint8_t fainted = (uint8_t)(in->party_hp_zero & ~f->p_party_hp_zero);
    if (f->p_in_battle && (fainted & (1u << f->p_lead_slot))) f->faint_pid = prev_lead_pid;

    /* Walking across a map connection rewrites the location while the field stays up
     * with no fade; the coords are rebased below when gCamera.active rises. Warps
     * always pass through a fade or leave CB2_Overworld first. A warp to the same map
     * (Fly to the current town, some doors) keeps the location but jumps the player
     * more than one tile without a camera rebase. */
    bool rebase = in->cam_active && !f->p_cam_active;
    bool loc_changed = in->loc_group != f->p_loc_group || in->loc_num != f->p_loc_num;
    int32_t jx = in->cur_x - f->p_cur_x, jy = in->cur_y - f->p_cur_y;
    bool jumped = !rebase && (jx < 0 ? -jx : jx) + (jy < 0 ? -jy : jy) > 1;
    bool new_map = !f->p_valid || (loc_changed && (f->p_in_battle || in->fade_active)) || jumped;
    if (new_map) {
        f->vis = FOL_HIDDEN;
        f->reveal_pending = true;
        clear_ball_fx(f);
        place(f, in, in->cur_x, in->cur_y);
    }

    /* Crossing into a connected map shifts every coordinate by the camera delta. */
    if (rebase) {
        int16_t cx = (int16_t)in->cam_x, cy = (int16_t)in->cam_y;
        f->tx = (int16_t)(f->tx - cx);
        f->ty = (int16_t)(f->ty - cy);
        f->from_x = (int16_t)(f->from_x - cx);
        f->from_y = (int16_t)(f->from_y - cy);
        f->p_cur_x = (int16_t)(f->p_cur_x - cx);
        f->p_cur_y = (int16_t)(f->p_cur_y - cy);
    }

    bool ball = (in->avatar_flags & AVATAR_ON_BIKE_OR_WATER) ||
                (too_tall && (in->map_type == MAP_TYPE_INDOOR || in->map_type == MAP_TYPE_SECRET_BASE));
    if (f->ball_fx != FOL_BALL_NONE) {
        if (f->ball_fx == FOL_BALL_IN && !ball) {
            clear_ball_fx(f);                 /* dismounted before the ball closed */
        } else if (++f->ball_t >= (f->ball_fx == FOL_BALL_IN ? FOL_BALL_IN_TOTAL : FOL_BALL_OUT_TOTAL)) {
            bool was_in = f->ball_fx == FOL_BALL_IN;
            clear_ball_fx(f);
            if (was_in) {
                f->in_ball = true;
                f->vis = FOL_HIDDEN;
                f->jump_pending = false;
                place(f, in, in->cur_x, in->cur_y);
            }
        }
    }
    if (ball && !f->in_ball) {
        if (f->vis == FOL_VISIBLE && !in->in_transition) {
            if (f->ball_fx != FOL_BALL_IN) {
                f->ball_fx = FOL_BALL_IN;
                f->ball_t = 0;
                f->jump_pending = false;
            }
        } else {
            clear_ball_fx(f);
            f->in_ball = true;
            f->vis = FOL_HIDDEN;
            f->jump_pending = false;
            place(f, in, in->cur_x, in->cur_y);
        }
    } else if (!ball && f->in_ball) {
        f->in_ball = false;
        f->reveal_pending = true;
        f->reveal_from_ball = !new_map;
    }
    /* Hidden only by a battle transition or a trip off the overworld (battle, menu):
     * show it where it was once that ends. */
    if (in->in_transition) {
        if (f->vis == FOL_VISIBLE) f->vis = FOL_HIDDEN;
    } else if (f->vis == FOL_HIDDEN && !f->reveal_pending && !f->in_ball) {
        f->vis = FOL_VISIBLE;
    }

    if (!new_map && (in->cur_x != f->p_cur_x || in->cur_y != f->p_cur_y)) on_player_step(f, in);

    /* fol_* was read at the tile the follower stood on before this frame's move. */
    if (in->fol_x == f->tx && in->fol_y == f->ty && in->fol_elevation != 0 && in->fol_elevation != 15)
        f->elevation = in->fol_elevation;

    if (f->step_t < f->step_len) f->step_t++;
    f->hop_y = 0;
    if (f->step_t < f->step_len && is_hop(f)) {
        int32_t t = f->step_t, l = f->step_len;
        f->hop_y = (int8_t)(HOP_PEAK * 4 * t * (l - t) / (l * l));
    }
    f->anim_tick++;

    if (f->p_free && in->free && (in->new_keys & 1) && (in->tile_state == 0 || in->tile_state == 2) &&
        !(in->avatar_flags & AVATAR_SURFING) && f->vis == FOL_VISIBLE && f->ball_fx == FOL_BALL_NONE &&
        f->step_t >= f->step_len) {
        int16_t dx, dy;
        dir_delta(in->facing, &dx, &dy);
        if ((dx || dy) && in->cur_x + dx == f->tx && in->cur_y + dy == f->ty) f->talk_request = true;
    }

    store_edges(f, in);
}

void follower_feet(const Follower* f, const FollowerInput* in, int16_t* x, int16_t* y) {
    int32_t px = (f->tx - in->origin.pos_x) * 16 + in->origin.cam_dx + 8;
    int32_t py = (f->ty - in->origin.pos_y) * 16 + in->origin.cam_dy + 16;
    if (f->step_t < f->step_len) {
        int32_t left = f->step_len - f->step_t;
        px -= (f->tx - f->from_x) * 16 * left / f->step_len;
        py -= (f->ty - f->from_y) * 16 * left / f->step_len;
    }
    *x = (int16_t)px;
    *y = (int16_t)(py - f->hop_y);
}

uint8_t follower_frame(const Follower* f) {
    static const uint8_t k_base[5] = {2, 2, 0, 4, 6};   /* by game facing; 0 treated as south */
    bool moving = f->step_t < f->step_len;
    uint8_t base = k_base[f->facing <= 4 ? f->facing : 0];
    return (uint8_t)(base + ((f->anim_tick / (moving ? 8 : 16)) & 1));
}

void follower_feed_init(FollowerFeed* fd, const GameProfile* p) {
    memset(fd, 0, sizeof(*fd));
    fd->profile = p;
    follower_init(&fd->sim);
}

void follower_capture(const GBA* gba, void* feed) {
    FollowerFeed* fd = (FollowerFeed*)feed;
    GameMem m = { gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size };
    if (!fd->profile) return;
    follower_read(fd->profile, &m, fd->sim.tx, fd->sim.ty, &fd->in);
    follower_step(&fd->sim, &fd->in, fd->too_tall);
}
