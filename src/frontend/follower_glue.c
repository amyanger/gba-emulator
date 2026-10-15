#include "frontend/follower_glue.h"
#include "gba.h"
#include "game/emerald_layout.h"
#include "game/encounters.h"
#include "game/follower_ctx.h"
#include "game/follower_places.h"
#include <string.h>
#include <time.h>

/* Following Pokemon (F1) glue between the emulated frame, the follower view and the talk.
 * Struct offsets from pret/pokeemerald (global.h, pokemon.h, item.h, global.fieldmap.h). */
#define FOL_ITEM_POCKET       0x1A
#define FOL_SPECIES_NAME_LEN  11
#define FOL_ITEM_NAME_LEN     14
#define FOL_PLAYER_NAME_LEN   8      /* 7 characters and 0xFF */
#define FOL_AVATAR_OBJ_ID     0x05
#define FOL_PAL_TAG_GRASS     0x1005 /* FLDEFF_PAL_TAG_GENERAL_1, the tall grass palette */
#define FOL_FAINT_MOOD        (-80)

static uint8_t s_fol_rules[HGSS_RULES][HGSS_RULE_SIZE];
static HgssStrVars s_fol_vars;
static uint16_t s_fol_line[HGSS_MSG_MAX];
static uint16_t s_fol_text[HGSS_MSG_MAX];

/* Ends a talk without applying its mood change. talk_abort leaves want_cry set. */
void follower_glue_talk_stop(FollowerView* v) {
    talk_abort(&v->talk);
    v->talk.want_cry = false;
    v->talk.phase = TALK_IDLE;
}

static bool follower_national(const GameProfile* p, const GameMem* m, uint16_t species, uint16_t* national) {
    *national = species;
    if (species == 0) return false;
    if (species > 251 && !game_mem_read16(m, p->species_to_national + 2u * (species - 1u), national)) return false;
    return *national != 0;
}

/* Copies the tall grass palette from its OBJ palette slot once Emerald has loaded it. The
 * hardware colours are faded during fades, so those frames keep the last clean copy. */
static void follower_grass_palette(FollowerView* v, const GBA* gba, const GameMem* m, const FollowerFeed* fd) {
    const FollowerInput* in = &fd->in;
    if (!in->ok || in->fade_y != 0 || in->fade_active || in->weather_state == 1) return;
    for (uint32_t slot = 0; slot < 16; slot++) {
        uint16_t tag = 0;
        if (!game_mem_read16(m, fd->profile->sprite_palette_tags + 2u * slot, &tag) || tag != FOL_PAL_TAG_GRASS)
            continue;
        const uint8_t* pal = gba->ppu.palette_ram + 0x200 + 32u * slot;
        for (uint32_t i = 0; i < 16; i++) v->grass_pal[i] = (uint16_t)(pal[2 * i] | pal[2 * i + 1] << 8);
        v->grass_ok = true;
        return;
    }
}

static uint32_t follower_rand(void* ud) {
    FollowerView* v = (FollowerView*)ud;
    uint32_t x = v->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rng = x;
    return x;
}

/* Interactions we can play: no yes/no prompt, fashion item, leaf, or Johto-only line. */
static bool follower_script_ok(uint16_t result, void* ud) {
    FollowerView* v = (FollowerView*)ud;
    HgssTalkScript s;
    if (!hgss_assets_script(v->assets, result, &s) || s.yes_no || s.fashion != 0 || s.leaf != 0) return false;
    for (uint32_t i = 0; i < s.step_count && i < HGSS_STEPS; i++) {
        uint16_t msg = s.step[i].msg;
        if (msg == 0) continue;
        uint32_t n = 0;
        if (follower_place_msg_denied(msg) || !hgss_assets_line(v->assets, msg, s_fol_line, HGSS_MSG_MAX, &n) ||
            hgss_msg_has_yesno(s_fol_line, n))
            return false;
    }
    return true;
}

static uint8_t follower_adjacent_objects(const GameProfile* p, const GameMem* m, const FollowerInput* in) {
    uint8_t player = 0xFF, count = 0;
    game_mem_read8(m, p->player_avatar + FOL_AVATAR_OBJ_ID, &player);
    for (uint32_t i = 0; i < OBJ_COUNT; i++) {
        uint8_t obj[OBJ_CUR + 4];
        if (i == player || !game_mem_copy(m, p->object_events + i * OBJ_SIZE, obj, sizeof(obj)) || !(obj[0] & 1))
            continue;
        int32_t dx = (int16_t)(obj[OBJ_CUR] | obj[OBJ_CUR + 1] << 8) - in->cur_x;
        int32_t dy = (int16_t)(obj[OBJ_CUR + 2] | obj[OBJ_CUR + 3] << 8) - in->cur_y;
        if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) count++;
    }
    return count;
}

/* Grass, cave floor and water tiles of a map that has land or water encounters. An
 * approximation of Emerald's per-metatile encounter attribute; it only picks lines. */
static bool follower_encounter_tile(const GameProfile* p, const GameMem* m, const FollowerInput* in) {
    uint8_t b = in->fol_behavior;
    bool land = b == 0x02 || b == 0x03 || b == 0x08 || b == 0x0B || b == 0x24;
    bool water = b == 0x10 || b == 0x11 || b == 0x12 || b == 0x14 || b == 0x15;
    if (!land && !water) return false;
    EncounterList enc[ENC_METHOD_COUNT];
    if (!encounters_find(m, p->wild_mon_headers, p->wild_header_cap, p->species_count, in->loc_group, in->loc_num,
                         enc))
        return false;
    return land ? enc[ENC_LAND].count > 0 : enc[ENC_WATER].count > 0;
}

/* The game's clock: RTC time minus the save's local time offset (pokeemerald RtcCalcLocalTime). */
static uint8_t follower_hour(const GBA* gba, const GameProfile* p, const GameMem* m) {
    time_t now = time(NULL) + (time_t)gba->cart.rtc.offset_secs;
    struct tm* tm = localtime(&now);
    int32_t minutes = tm ? tm->tm_hour * 60 + tm->tm_min : 12 * 60;
    uint32_t sb2 = 0;
    uint8_t h = 0, mi = 0;
    if (game_mem_read32(m, p->save_block2_ptr, &sb2) && game_mem_read8(m, sb2 + SB2_LOCAL_TIME_OFFSET + 2, &h) &&
        game_mem_read8(m, sb2 + SB2_LOCAL_TIME_OFFSET + 3, &mi))
        minutes -= (int8_t)h * 60 + (int8_t)mi;
    minutes %= 24 * 60;
    if (minutes < 0) minutes += 24 * 60;
    return (uint8_t)(minutes / 60);
}

static void follower_strvar(const GameMem* m, uint32_t addr, uint32_t len, uint16_t* dst) {
    uint8_t buf[HGSS_STRVAR_MAX];
    if (len > sizeof(buf) || !game_mem_copy(m, addr, buf, len)) return;
    hgss_str_from_gen3(buf, len, dst, HGSS_STRVAR_MAX);
}

/* Nickname, species, player, place and held item, as HGSS text. */
static void follower_strvars(const GameProfile* p, const GameMem* m, const FollowerInput* in) {
    const MonData* mon = &in->lead;
    for (uint32_t i = 0; i < HGSS_STRVARS; i++) s_fol_vars.s[i][0] = HGSS_CH_END;
    hgss_str_from_gen3(mon->nickname_raw, sizeof(mon->nickname_raw), s_fol_vars.s[0], HGSS_STRVAR_MAX);
    if (mon->species < p->species_count)
        follower_strvar(m, p->species_names + (uint32_t)mon->species * FOL_SPECIES_NAME_LEN, FOL_SPECIES_NAME_LEN,
                        s_fol_vars.s[1]);
    uint32_t sb2 = 0, name = 0;
    if (game_mem_read32(m, p->save_block2_ptr, &sb2))
        follower_strvar(m, sb2, FOL_PLAYER_NAME_LEN, s_fol_vars.s[2]);
    if (in->mapsec < p->region_map_count &&
        game_mem_read32(m, p->region_map_entries + 8u * in->mapsec + 4, &name))
        follower_strvar(m, name, HGSS_STRVAR_MAX, s_fol_vars.s[3]);
    if (mon->held_item != 0 && mon->held_item < p->item_count)
        follower_strvar(m, p->items + (uint32_t)mon->held_item * ITEM_SIZE, FOL_ITEM_NAME_LEN, s_fol_vars.s[4]);
}

/* Animation and text pages for the talk's current step. */
static void follower_talk_step(Frontend* fe) {
    static TalkStepData d;
    TalkPlayer* t = &fe->follower.talk;
    HgssAssets* a = fe->follower.assets;
    memset(&d, 0, sizeof(d));
    fe->fol_step = t->step;
    if (t->step < t->script.step_count && t->step < HGSS_STEPS) {
        const HgssTalkStep* s = &t->script.step[t->step];
        d.anim_n = hgss_assets_anim(a, s->anim, d.anim);
        if (d.anim_n > HGSS_ANIM_FRAMES) d.anim_n = HGSS_ANIM_FRAMES;
        uint32_t n = 0;
        if (s->msg && hgss_assets_line(a, s->msg, s_fol_line, HGSS_MSG_MAX, &n)) {
            uint32_t len = hgss_msg_expand(s_fol_line, n, &s_fol_vars, s_fol_text, HGSS_MSG_MAX);
            d.page_n = hgss_text_layout(&a->font, s_fol_text, len, d.pages, HGSS_PAGE_MAX);
            if (d.page_n > HGSS_PAGE_MAX) d.page_n = HGSS_PAGE_MAX;
        }
    }
    talk_set_step_data(t, &d);
}

/* A on the follower: pick an HGSS interaction for the lead and the place, and start it. */
static void follower_talk_begin(Frontend* fe, const GBA* gba, const GameMem* m) {
    FollowerView* v = &fe->follower;
    FollowerFeed* fd = fe->fol_feed;
    const GameProfile* p = fd->profile;
    const FollowerInput* in = &fd->in;
    const MonData* mon = &in->lead;
    HgssAssets* a = v->assets;
    uint16_t national = 0;
    if (!a || !a->ok || !in->lead_ok || !follower_national(p, m, mon->species, &national)) return;

    FollowerCtxExtra x;
    memset(&x, 0, sizeof(x));
    uint32_t info = p->species_info + (uint32_t)mon->species * SPECIES_INFO_SIZE;
    x.gender_ratio = 0xFF;
    game_mem_read8(m, info + SI_TYPE1, &x.type1);
    game_mem_read8(m, info + SI_TYPE2, &x.type2);
    game_mem_read8(m, info + SI_GENDER_RATIO, &x.gender_ratio);
    if (mon->held_item != 0 && mon->held_item < p->item_count)
        game_mem_read8(m, p->items + (uint32_t)mon->held_item * ITEM_SIZE + FOL_ITEM_POCKET, &x.item_pocket);
    if (national <= sizeof(a->species_class_tbl)) x.species_class = a->species_class_tbl[national - 1];
    x.adjacent_objects = follower_adjacent_objects(p, m, in);
    if (fe->overworld_xray && fe->ow_feed)
        for (uint32_t i = 0; i < fe->ow_feed->shown.marker_count && i < OW_MAX_MARKERS; i++)
            if (fe->ow_feed->shown.markers[i].kind == OW_MARK_HIDDEN) x.hidden_items++;
    x.encounter_tile = follower_encounter_tile(p, m, in);
    x.hour = follower_hour(gba, p, m);
    x.mood = follower_mood_get(v, mon->personality);
    static const uint8_t k_opposite[5] = {1, 2, 1, 4, 3};   /* game facing: 1 S, 2 N, 3 W, 4 E */
    uint8_t face = k_opposite[in->facing <= 4 ? in->facing : 0];
    x.follower_facing = face;

    HgssTalkCtx ctx;
    follower_ctx_build(in, &x, &ctx);
    hgss_assets_rules(a, follower_place_mapsec(in->map_type, in->mapsec, in->fol_behavior), s_fol_rules);
    int32_t pick = hgss_rule_pick(&s_fol_rules[0][0], &ctx, follower_rand, follower_script_ok, v);
    HgssTalkScript script;
    if (pick < 0 || !hgss_assets_script(a, hgss_rule_result(s_fol_rules[pick]), &script)) return;
    follower_strvars(p, m, in);
    talk_start(&v->talk, &script);
    follower_talk_step(fe);
    fd->sim.facing = face;   /* turn to the player */
}

static void follower_play_cry(FollowerView* v, const GBA* gba, const FollowerFeed* fd) {
    GameMem m = {gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size};
    uint32_t cry = 0, wave = 0;
    if (!fd->in.lead_ok || !cry_species_index(fd->profile, &m, fd->in.lead.species, &cry) ||
        !game_mem_read32(&m, fd->profile->cry_table + 12u * cry + 4, &wave) || wave < 0x08000000u ||
        wave - 0x08000000u >= gba->cart.rom_size)
        return;
    uint32_t off = wave - 0x08000000u;
    if (cry_decode(gba->cart.rom + off, gba->cart.rom_size - off, v->cry.pcm, CRY_MAX_SAMPLES, &v->cry.len,
                   &v->cry.rate))
        cry_start(&v->cry);
    else
        LOG_WARN("Couldn't decode cry %u for species %u", (unsigned)cry, (unsigned)fd->in.lead.species);
}

void follower_glue_frame_begin(Frontend* fe, GBA* gba) {
    FollowerView* v = &fe->follower;
    FollowerFeed* fd = fe->fol_feed;
    if (!v->enabled || !fd || !fd->profile) {
        memset(gba->ppu.extra_obj, 0, sizeof(gba->ppu.extra_obj));
        return;
    }
    GameMem m = {gba->bus.ewram, gba->bus.iwram, gba->cart.rom, gba->cart.rom_size};
    follower_view_sync(v, &m, fd->profile, fd);
    follower_grass_palette(v, gba, &m, fd);
    bool shown = fd->sim.vis == FOL_VISIBLE && fd->in.overworld && !fd->in.in_transition && v->gfx_pid != 0 &&
                 v->gfx_pid == fd->sim.lead_pid;
    if (talk_active(&v->talk) && !shown)
        follower_glue_talk_stop(v);
    else if (fd->sim.talk_request && shown && !talk_active(&v->talk))
        follower_talk_begin(fe, gba, &m);
    follower_view_objs(v, fd, gba->ppu.oam, gba->ppu.extra_obj);
}

void follower_glue_frame_end(Frontend* fe, const GBA* gba, uint16_t real_keys) {
    FollowerView* v = &fe->follower;
    FollowerFeed* fd = fe->fol_feed;
    if (!v->enabled || !fd) return;
    uint16_t held = (uint16_t)(~real_keys & 0x03FF);
    uint16_t pressed = (uint16_t)(held & ~fe->fol_prev_keys);
    fe->fol_prev_keys = held;

    TalkPlayer* t = &v->talk;
    if (talk_active(t)) {
        talk_tick(t, pressed, held);
        if (t->phase == TALK_ANIM && t->step != fe->fol_step) follower_talk_step(fe);
        if (t->want_cry) {
            t->want_cry = false;
            follower_play_cry(v, gba, fd);
        }
    }
    if (t->phase == TALK_DONE) {
        if (t->mood_delta) follower_mood_add(v, fd->sim.lead_pid, t->mood_delta);
        follower_mood_save(v);
        t->phase = TALK_IDLE;
    }

    const Follower* s = &fd->sim;
    if (s->faint_pid) follower_mood_add(v, s->faint_pid, FOL_FAINT_MOOD);
    if (s->mood_delta && s->lead_pid) follower_mood_add(v, s->lead_pid, s->mood_delta);
    if (s->steps && s->vis == FOL_VISIBLE && s->lead_pid) {
        int32_t mood = follower_mood_get(v, s->lead_pid);
        int32_t n = s->steps < (mood < 0 ? -mood : mood) ? s->steps : (mood < 0 ? -mood : mood);
        if (n) follower_mood_add(v, s->lead_pid, (int16_t)(mood > 0 ? -n : n));
    }
}
