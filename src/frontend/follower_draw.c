#include "frontend/follower_draw.h"
#include "game/emerald_layout.h"
#include <stdio.h>
#include <string.h>

#define FOLLOW_FOOT_PAD   4      /* HGSS sprites leave about 4 px under the feet */
#define GRASS_RUSTLE      10     /* frames per rustle frame */
#define GRASS_RUSTLE_END  (4 * GRASS_RUSTLE)
#define NATIONAL_UNOWN    201

static const uint8_t k_obj_h[3][4] = { {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64} };

/* HGSS Poke Ball burst (hgss-follow-mon ball_effect.c, Part 2): crop box inside the 32x32 texture. */
typedef struct { uint8_t src_x, src_y, w, h; } BurstCrop;
static const BurstCrop k_burst_crop[4] = { {0, 23, 8, 7}, {8, 19, 8, 7}, {16, 16, 8, 5}, {24, 12, 8, 5} };
#define BURST_CENTER_Y 17
/* Sparkle offsets from the burst centre: round(dist * (cos a, -sin a)) for a = 0, 45, ..., 315 degrees,
 * with dist 6.3, 11.0, 15.6, 20.2 per frame (diagonals 4.45, 7.78, 11.03, 14.28). */
static const int8_t k_burst_off[4][8][2] = {
    { {6, 0}, {4, -4}, {0, -6}, {-4, -4}, {-6, 0}, {-4, 4}, {0, 6}, {4, 4} },
    { {11, 0}, {8, -8}, {0, -11}, {-8, -8}, {-11, 0}, {-8, 8}, {0, 11}, {8, 8} },
    { {16, 0}, {11, -11}, {0, -16}, {-11, -11}, {-16, 0}, {-11, 11}, {0, 16}, {11, 11} },
    { {20, 0}, {14, -14}, {0, -20}, {-14, -14}, {-20, 0}, {-14, 14}, {0, 20}, {14, 14} },
};

void follower_view_init(FollowerView* v, HgssAssets* assets, const char* save_path) {
    memset(v, 0, sizeof(*v));
    v->assets = assets;
    v->rng = 0x2545F491u;
    if (assets && assets->fx_ok) {
        for (uint32_t f = 0; f < 4; f++) {
            const BurstCrop* c = &k_burst_crop[f];
            for (uint32_t y = 0; y < c->h; y++)
                memcpy(&v->burst_px[f][y * c->w], &assets->burst.px[(c->src_y + y) * 32 + c->src_x], c->w);
        }
    }
    if (save_path && save_path[0]) {
        int n = snprintf(v->mood_path, sizeof(v->mood_path), "%s.follow", save_path);
        if (n < 0 || (size_t)n >= sizeof(v->mood_path)) v->mood_path[0] = '\0';   /* too long: no sidecar */
    }
    follower_mood_load(v);
}

/* One 16x16 4bpp frame: 2x2 tiles of 8x8 (TL, TR, BL, BR), 32 bytes each, low nibble = left pixel. */
static void grass_decode(const uint8_t* src, uint8_t* dst) {
    for (uint32_t t = 0; t < 4; t++) {
        uint32_t ox = (t & 1) * 8, oy = (t >> 1) * 8;
        for (uint32_t i = 0; i < 32; i++) {
            uint8_t b = src[t * 32 + i];
            uint32_t y = oy + i / 4, x = ox + (i % 4) * 2;
            dst[y * 16 + x] = (uint8_t)(b & 0x0F);
            dst[y * 16 + x + 1] = (uint8_t)(b >> 4);
        }
    }
}

static void rom_load(FollowerView* v, const GameMem* m, const GameProfile* p) {
    for (uint32_t i = 0; i < 16; i++) {
        if (!game_mem_read8(m, p->elevation_to_priority + i, &v->elev_prio[i])) v->elev_prio[i] = 2;
    }
    uint8_t raw[128];
    for (uint32_t f = 0; f < 5; f++) {
        if (game_mem_copy(m, p->tall_grass_pic + f * 128, raw, sizeof(raw)))
            grass_decode(raw, v->grass_px[f]);
    }
    v->rom_loaded = true;
}

bool follower_view_sync(FollowerView* v, const GameMem* m, const GameProfile* p, FollowerFeed* fd) {
    if (!v->rom_loaded) rom_load(v, m, p);
    uint32_t pid = fd->sim.lead_pid;
    if (pid != 0 && pid == v->gfx_pid) return true;
    if (!v->assets || !v->assets->ok || !fd->in.lead_ok || pid == 0) return false;
    const MonData* mon = &fd->in.lead;
    if (mon->personality != pid) return false;
    if (pid == v->tried_pid) return false;   /* already failed for this lead */
    v->tried_pid = pid;
    v->gfx_pid = 0;

    uint16_t species = mon->species, national = species;
    if (species == 0) return false;
    if (species > 251 && !game_mem_read16(m, p->species_to_national + 2u * (species - 1u), &national))
        return false;
    uint8_t ratio = 0xFF;
    game_mem_read8(m, p->species_info + (uint32_t)species * SPECIES_INFO_SIZE + SI_GENDER_RATIO, &ratio);
    bool female = pokemon_gender(ratio, pid) == MON_GENDER_FEMALE;
    uint8_t form = 0;
    if (national == NATIONAL_UNOWN)
        form = (uint8_t)(((pid >> 18 & 0xC0) | (pid >> 12 & 0x30) | (pid >> 6 & 0x0C) | (pid & 3)) % 28);

    bool too_tall = false;
    if (!hgss_assets_follower(v->assets, national, female, form, &v->gfx, &too_tall)) return false;
    uint32_t ot = mon->ot_id;
    v->shiny = ((ot >> 16) ^ (ot & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8;
    v->gfx_pid = pid;
    fd->too_tall = too_tall;
    return true;
}

/* c + floor((t - c) * y / 16) per 5-bit channel (pokeemerald BlendPalette). */
static uint16_t blend(uint16_t c, uint16_t t, int32_t y) {
    uint16_t out = 0;
    for (uint32_t s = 0; s < 15; s += 5) {
        int32_t a = (c >> s) & 31, b = (t >> s) & 31;
        int32_t d = (b - a) * y;
        int32_t q = d >= 0 ? d / 16 : -((-d + 15) / 16);
        out = (uint16_t)(out | (uint16_t)((a + q) << s));
    }
    return out;
}

void follower_fade_palette(const uint16_t in[16], const FollowerInput* fi, uint16_t out[16]) {
    int32_t y = 0;
    uint16_t to = 0;
    if ((fi->fade_mask & 0xFFFF0000u) && fi->fade_y > 0) {
        y = fi->fade_y > 16 ? 16 : fi->fade_y;
        to = fi->fade_color;
    } else if (fi->weather_state == 1) {
        y = 16 - (fi->weather_counter > 16 ? 16 : fi->weather_counter);
        to = fi->weather_color;
    }
    for (uint32_t i = 0; i < 16; i++) out[i] = y ? blend(in[i], to, y) : in[i];
}

int16_t follower_oam_slot(const uint8_t* oam, uint8_t priority, int16_t feet_y) {
    for (int16_t i = 127; i >= 0; i--) {
        const uint8_t* e = oam + 8 * i;
        uint16_t a0 = (uint16_t)(e[0] | e[1] << 8);
        uint16_t a1 = (uint16_t)(e[2] | e[3] << 8);
        uint16_t a2 = (uint16_t)(e[4] | e[5] << 8);
        uint8_t shape = (uint8_t)BITS(a0, 15, 14);
        if (BITS(a0, 9, 8) == 2 || BITS(a0, 11, 10) == 2 || shape > 2) continue;
        if (BITS(a2, 11, 10) != priority) continue;
        int32_t h = k_obj_h[shape][BITS(a1, 15, 14)];
        if (h < 16) continue;
        int32_t y = a0 & 0xFF;
        if (y >= 160) y -= 256;
        if (y + h > feet_y) return i;
    }
    return -1;
}

static void obj_set(PpuExtraObj* o, int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t prio, int16_t slot,
                    const uint8_t* px, const uint16_t pal[16]) {
    o->on = true;
    o->x = x;
    o->y = y;
    o->w = w;
    o->h = h;
    o->priority = prio;
    o->slot = slot;
    o->pixels = px;
    memcpy(o->pal, pal, sizeof(o->pal));
}

/* Nearest-neighbour scale by 1/k around the feet point (bottom-centre, above the foot pad). */
static void shrink_frame(const uint8_t* src, uint8_t size, int32_t k, uint8_t* dst) {
    int32_t ax = size / 2, ay = size - FOLLOW_FOOT_PAD;
    for (int32_t y = 0; y < size; y++) {
        int32_t sy = ay + (y - ay) * k;
        for (int32_t x = 0; x < size; x++) {
            int32_t sx = ax + (x - ax) * k;
            bool in = sy >= 0 && sy < size && sx >= 0 && sx < size;
            dst[y * size + x] = in ? src[sy * size + sx] : 0;
        }
    }
}

static void ball_obj(PpuExtraObj* o, const HgssAssets* a, const FollowerInput* in, int16_t fx, int16_t fy,
                     uint8_t prio, int16_t slot) {
    uint16_t pal[16];
    follower_fade_palette(a->ball.pal, in, pal);
    obj_set(o, (int16_t)(fx - 8), (int16_t)(fy - 16), 16, 16, prio, slot, a->ball.px, pal);
}

void follower_view_objs(FollowerView* v, const FollowerFeed* fd, const uint8_t* oam,
                        PpuExtraObj out[PPU_EXTRA_OBJ_MAX]) {
    memset(out, 0, sizeof(PpuExtraObj) * PPU_EXTRA_OBJ_MAX);
    const Follower* s = &fd->sim;
    const FollowerInput* in = &fd->in;
    bool in_grass = in->fol_behavior == MB_TALL_GRASS;
    if (!in_grass || !v->grass_in || s->tx != v->grass_tx || s->ty != v->grass_ty) v->grass_t = 0;
    v->grass_in = in_grass;
    v->grass_tx = s->tx;
    v->grass_ty = s->ty;
    uint8_t grass_t = v->grass_t;
    if (in_grass && v->grass_t < GRASS_RUSTLE_END) v->grass_t++;

    uint8_t size = v->gfx.size;
    if (!v->enabled || v->gfx_pid == 0 || v->gfx_pid != s->lead_pid || s->vis != FOL_VISIBLE ||
        !in->overworld || in->in_transition || (size != 32 && size != 64))
        return;

    int16_t fx, fy;
    follower_feet(s, in, &fx, &fy);
    uint8_t prio = v->elev_prio[s->elevation & 15] & 3;
    int16_t slot = follower_oam_slot(oam, prio, fy);
    /* The ball sorts like the follower, but a sprite with feet on the same row (the player) wins the tie. */
    int16_t ball_slot = follower_oam_slot(oam, prio, (int16_t)(fy - 1));
    bool fx_ok = v->assets && v->assets->fx_ok;
    bool shrink = false, burst = false;
    if (s->ball_fx == FOL_BALL_IN && fx_ok) {
        if (s->ball_t >= FOL_BALL_IN_SHRINK) {
            ball_obj(&out[0], v->assets, in, fx, fy, prio, ball_slot);
            return;
        }
        shrink = true;
    } else if (s->ball_fx == FOL_BALL_OUT) {
        if (s->ball_t < FOL_BALL_OUT_BALL) {
            if (fx_ok) ball_obj(&out[0], v->assets, in, fx, fy, prio, ball_slot);
            return;
        }
        burst = fx_ok && s->ball_t < FOL_BALL_OUT_TOTAL;
    }

    Follower posed = *s;
    int16_t ox = 0, oy = 0;
    if (talk_active(&v->talk)) {
        ox = v->talk.off_x;
        oy = v->talk.off_y;
        if (v->talk.facing) posed.facing = v->talk.facing;
    }
    uint8_t frame = follower_frame(&posed);
    int16_t x = (int16_t)(fx - size / 2 + ox);
    int16_t y = (int16_t)(fy - size + FOLLOW_FOOT_PAD + oy);

    uint16_t pal[16];
    const uint8_t* px = v->gfx.px[frame];
    if (shrink) {
        static const uint16_t k_white[16] = { 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
                                              0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF };
        follower_fade_palette(k_white, in, pal);
        shrink_frame(px, size, s->ball_t / 2 + 1, v->shrink_px);
        px = v->shrink_px;
    } else {
        follower_fade_palette(v->gfx.pal[v->shiny ? 1 : 0], in, pal);
    }

    uint32_t n = 0;
    if (in->fol_behavior == MB_LONG_GRASS) {
        /* Bottom half at priority 3 so the BG grass covers the legs. */
        uint32_t half = (uint32_t)size * size / 2;
        memcpy(v->split_px[0], px, half);
        memcpy(v->split_px[1], px + half, half);
        uint8_t hh = (uint8_t)(size / 2);
        obj_set(&out[n++], x, y, size, hh, prio, slot, v->split_px[0], pal);
        obj_set(&out[n++], x, (int16_t)(y + hh), size, hh, 3, follower_oam_slot(oam, 3, fy), v->split_px[1], pal);
    } else {
        obj_set(&out[n++], x, y, size, size, prio, slot, px, pal);
        if (in_grass && v->grass_ok) {
            uint8_t gf = grass_t < GRASS_RUSTLE_END ? (uint8_t)(1 + grass_t / GRASS_RUSTLE) : 0;
            int16_t gx = (int16_t)((s->tx - in->origin.pos_x) * 16 + in->origin.cam_dx);
            int16_t gy = (int16_t)((s->ty - in->origin.pos_y) * 16 + in->origin.cam_dy);
            uint16_t gpal[16];
            follower_fade_palette(v->grass_pal, in, gpal);
            obj_set(&out[n++], gx, gy, 16, 16, prio, slot, v->grass_px[gf], gpal);
        }
    }

    if (burst) {
        uint32_t f = (uint32_t)(s->ball_t - FOL_BALL_OUT_BALL) / 4;
        const BurstCrop* c = &k_burst_crop[f];
        uint16_t bpal[16];
        follower_fade_palette(v->assets->burst.pal, in, bpal);
        int16_t cy = (int16_t)(fy - BURST_CENTER_Y);
        for (uint32_t i = 0; i < 8 && n < PPU_EXTRA_OBJ_MAX; i++) {
            int16_t sx = (int16_t)(fx + k_burst_off[f][i][0] - c->w / 2);
            int16_t sy = (int16_t)(cy + k_burst_off[f][i][1] - c->h / 2);
            obj_set(&out[n++], sx, sy, c->w, c->h, prio, -1, v->burst_px[f], bpal);
        }
    }
}

int8_t follower_mood_get(const FollowerView* v, uint32_t pid) {
    for (uint32_t i = 0; i < v->mood_n && i < FOLLOW_MOOD_SLOTS; i++)
        if (v->mood[i].pid == pid) return v->mood[i].mood;
    return 0;
}

void follower_mood_add(FollowerView* v, uint32_t pid, int16_t delta) {
    uint32_t n = v->mood_n < FOLLOW_MOOD_SLOTS ? v->mood_n : FOLLOW_MOOD_SLOTS;
    uint32_t i = 0;
    while (i < n && v->mood[i].pid != pid) i++;
    FollowMood e = { pid, 0 };
    if (i < n) {
        e = v->mood[i];
    } else if (n < FOLLOW_MOOD_SLOTS) {
        n++;
    } else {
        i = FOLLOW_MOOD_SLOTS - 1;   /* evict the least recently changed */
    }
    memmove(&v->mood[1], &v->mood[0], sizeof(FollowMood) * i);   /* move to front */
    int32_t m = e.mood + delta;
    if (m > 127) m = 127;
    if (m < -127) m = -127;
    e.mood = (int8_t)m;
    v->mood[0] = e;
    v->mood_n = (uint8_t)n;
}

bool follower_mood_save(const FollowerView* v) {
    if (!v->mood_path[0]) return false;
    FILE* f = fopen(v->mood_path, "w");
    if (!f) return false;
    bool ok = true;
    for (uint32_t i = 0; i < v->mood_n && i < FOLLOW_MOOD_SLOTS; i++)
        if (fprintf(f, "%08x %d\n", (unsigned)v->mood[i].pid, (int)v->mood[i].mood) < 0) ok = false;
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool follower_mood_load(FollowerView* v) {
    memset(v->mood, 0, sizeof(v->mood));
    v->mood_n = 0;
    if (!v->mood_path[0]) return false;
    FILE* f = fopen(v->mood_path, "r");
    if (!f) return false;
    FollowMood tmp[FOLLOW_MOOD_SLOTS];
    uint32_t n = 0;
    bool ok = true;
    char line[64];
    while (n < FOLLOW_MOOD_SLOTS && fgets(line, sizeof(line), f)) {
        unsigned pid;
        int mood;
        char extra;
        if (sscanf(line, "%8x %d %c", &pid, &mood, &extra) != 2 || mood < -127 || mood > 127) {
            ok = false;
            break;
        }
        tmp[n].pid = (uint32_t)pid;
        tmp[n].mood = (int8_t)mood;
        n++;
    }
    fclose(f);
    if (!ok) return false;
    memcpy(v->mood, tmp, sizeof(FollowMood) * n);
    v->mood_n = (uint8_t)n;
    return true;
}

static void blit_emote(uint16_t* fb, const HgssEmoteGfx* g, uint8_t frame, int32_t x0, int32_t y0) {
    for (int32_t y = 0; y < 32; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= SCREEN_HEIGHT) continue;
        for (int32_t x = 0; x < 32; x++) {
            int32_t sx = x0 + x;
            uint8_t c = g->px[frame][y * 32 + x];
            if (sx < 0 || sx >= SCREEN_WIDTH || c == 0) continue;
            fb[sy * SCREEN_WIDTH + sx] = g->pal[c & 15];
        }
    }
}

void follower_view_overlay(const FollowerView* v, const FollowerFeed* fd, uint16_t* fb) {
    const TalkPlayer* t = &v->talk;
    if (!v->assets || !v->assets->ok || !talk_active(t)) return;
    int8_t dy;
    uint8_t frame;
    if (t->emote >= 1 && t->emote <= HGSS_EMOTES && talk_emote_pose(t, &dy, &frame)) {
        int16_t fx, fy;
        follower_feet(&fd->sim, &fd->in, &fx, &fy);
        blit_emote(fb, &v->assets->emote[t->emote - 1], frame & 1, fx - 16, fy - v->gfx.size - 24 + dy);
    }
    if (t->phase == TALK_TEXT && t->page_i < t->page_n && t->page_i < HGSS_PAGE_MAX) {
        const HgssTextPage* pg = &t->pages[t->page_i];
        uint32_t len = 0;
        for (uint32_t i = 0; i < HGSS_TEXT_LINES; i++) len += pg->len[i];
        hgss_text_draw(fb, &v->assets->frame, &v->assets->font, pg, t->chars, t->chars >= len);
    }
}
