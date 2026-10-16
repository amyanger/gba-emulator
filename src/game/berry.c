#include "game/berry.h"
#include "game/emerald_layout.h"
#include "game/game.h"
#include "game/gen3_text.h"
#include <string.h>

/* pret/pokeemerald include/global.h, include/global.berry.h, src/berry.c;
 * research: docs/superpowers/specs/emerald-game-layer/overworld-layout-berries.md */
#define SB1_BERRY_TREES      0x169C
#define SB1_BERRY_MIN_SIZE   (SB1_BERRY_TREES + BERRY_TREE_COUNT * TREE_SIZE)
#define SB2_LAST_BERRY_UPDATE 0xA0
#define SB2_BERRY_MIN_SIZE   (SB2_LAST_BERRY_UPDATE + 8)
#define TREE_SIZE            8
#define TREE_STOP_GROWTH     0x80
#define STAGE_BERRIES        5
#define BERRY_COUNT          43
#define BERRY_INFO_SIZE      28
#define BI_MAX_YIELD         0x0A
#define BI_MIN_YIELD         0x0B
#define BI_STAGE_HOURS       0x14
#define REGROWTH_LIMIT       10
#define WIPE_STAGES          71

/* struct MapHeader / MapEvents / ObjectEventTemplate (include/global.fieldmap.h) */
#define TEMPLATE_MOVEMENT    0x09
#define TEMPLATE_TREE_ID     0x0E
#define MOVEMENT_BERRY_TREE  0x0C

static uint8_t place_for_sec(BerryPlaces* bp, const GameProfile* p, const GameMem* m, uint8_t sec) {
    for (uint8_t i = 0; i < bp->count; i++)
        if (bp->place[i].sec == sec) return i;
    if (bp->count >= BERRY_PLACE_MAX) return BERRY_PLACE_NONE;
    BerryPlace* pl = &bp->place[bp->count];
    pl->sec = sec;
    pl->spots = 0;
    game_mapsec_name(m, p, sec, pl->name);
    return bp->count++;
}

static void add_map_trees(BerryPlaces* bp, const GameProfile* p, const GameMem* m, uint32_t header) {
    uint32_t events = 0, objs = 0;
    uint8_t count = 0, sec = 0;
    if (!game_mem_read32(m, header + MAP_HEADER_EVENTS, &events) || !game_mem_read8(m, header + MAP_HEADER_REGION_MAP_SEC, &sec) ||
        !game_mem_read8(m, events + EVENTS_OBJ_COUNT, &count) || !game_mem_read32(m, events + EVENTS_OBJS, &objs))
        return;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t t = objs + i * TEMPLATE_SIZE;
        uint8_t movement = 0;
        uint16_t id = 0;
        if (!game_mem_read8(m, t + TEMPLATE_MOVEMENT, &movement) || movement != MOVEMENT_BERRY_TREE ||
            !game_mem_read16(m, t + TEMPLATE_TREE_ID, &id) || id >= BERRY_TREE_COUNT ||
            bp->place_of[id] != BERRY_PLACE_NONE)
            continue;
        uint8_t pl = place_for_sec(bp, p, m, sec);
        if (pl == BERRY_PLACE_NONE) continue;
        bp->place_of[id] = pl;
        bp->place[pl].spots++;
    }
}

void berry_places_build(BerryPlaces* bp, const GameProfile* p, const GameMem* m) {
    memset(bp, 0, sizeof(*bp));
    memset(bp->place_of, BERRY_PLACE_NONE, sizeof(bp->place_of));
    if (!p || !p->map_groups) return;
    /* Same walk as overworld_gift_audit: group arrays are contiguous and the last one ends
     * where gMapGroups starts. */
    for (uint32_t g = 0; g < p->map_group_count; g++) {
        uint32_t list = 0, next = p->map_groups;
        if (!game_mem_read32(m, p->map_groups + 4u * g, &list)) return;
        if (g + 1 < p->map_group_count && !game_mem_read32(m, p->map_groups + 4u * (g + 1), &next)) return;
        if (next <= list) continue;
        for (uint32_t n = 0; n < (next - list) / 4u; n++) {
            uint32_t header = 0;
            if (game_mem_read32(m, list + 4u * n, &header)) add_map_trees(bp, p, m, header);
        }
    }
}

void berry_yield_band(uint8_t min, uint8_t max, uint8_t water, uint8_t* lo, uint8_t* hi) {
    if (water == 0 || max <= min) {
        *lo = *hi = min;
        return;
    }
    /* CalcBerryYieldInternal: rand in [(max-min)(w-1), (max-min)w], extra = rand / 4 rounded half up. */
    uint32_t r0 = (uint32_t)(max - min) * (water - 1u), r1 = (uint32_t)(max - min) * water;
    *lo = (uint8_t)(min + r0 / 4 + (r0 % 4 >= 2));
    *hi = (uint8_t)(min + r1 / 4 + (r1 % 4 >= 2));
}

/* BerryTreeGrow without the yield roll; false when the tree can't grow. */
static bool tree_grow(uint8_t t[TREE_SIZE]) {
    uint8_t stage = t[1] & 0x7F;
    if (t[1] & TREE_STOP_GROWTH) return false;
    if (stage == 0) return false;
    if (stage < STAGE_BERRIES) {
        t[1] = (uint8_t)(t[1] + 1);
        return true;
    }
    uint8_t regrowth = (uint8_t)((t[5] & 0x0F) + 1);
    if (regrowth == REGROWTH_LIMIT) {
        memset(t, 0, TREE_SIZE);
        return true;
    }
    t[1] = 2;
    t[4] = 0;
    t[5] = regrowth;
    return true;
}

void berry_project(uint8_t t[TREE_SIZE], uint16_t stage_min, uint32_t minutes) {
    if (t[0] == 0 || (t[1] & 0x7F) == 0 || (t[1] & TREE_STOP_GROWTH)) return;
    if (minutes >= (uint32_t)stage_min * WIPE_STAGES) {
        memset(t, 0, TREE_SIZE);
        return;
    }
    uint32_t time = minutes;
    while (time != 0) {
        uint16_t next = (uint16_t)(t[2] | (t[3] << 8));
        if (next > time) {
            next = (uint16_t)(next - time);
            t[2] = (uint8_t)next;
            t[3] = (uint8_t)(next >> 8);
            break;
        }
        time -= next;
        next = stage_min;
        t[2] = (uint8_t)next;
        t[3] = (uint8_t)(next >> 8);
        if (!tree_grow(t)) break;
        if ((t[1] & 0x7F) == STAGE_BERRIES) {
            next = (uint16_t)(next * 4u);
            t[2] = (uint8_t)next;
            t[3] = (uint8_t)(next >> 8);
        }
    }
}

/* Linear, so RtcCalcTimeDifference's borrows cancel out of the total. */
static int64_t local_seconds(const MirageClock* c, const MirageTime* off) {
    int64_t d = (int64_t)mirage_day_count(c->year, c->month, c->day) - off->days;
    int64_t h = (int64_t)c->hour - off->hours, mi = (int64_t)c->minute - off->minutes;
    int64_t s = (int64_t)c->second - off->seconds;
    return d * 86400 + h * 3600 + mi * 60 + s;
}

int32_t berry_local_minutes(const MirageClock* c, const MirageTime* off) {
    int64_t s = local_seconds(c, off);
    return (int32_t)(s >= 0 ? s / 60 : -((-s + 59) / 60));
}

static bool read_time(const GameMem* m, uint32_t a, MirageTime* t) {
    uint8_t b[5];
    if (!game_mem_copy(m, a, b, sizeof(b))) return false;
    t->days = (int16_t)(b[0] | (b[1] << 8));
    t->hours = (int8_t)b[2];
    t->minutes = (int8_t)b[3];
    t->seconds = (int8_t)b[4];
    return true;
}

/* Minutes the game hasn't applied yet, or -1 when the clock can't say. */
static int64_t pending_minutes(const GameMem* m, uint32_t sb2, const MirageClock* clk) {
    MirageTime off, last;
    if (!clk || !clk->valid || !read_time(m, sb2 + SB2_LOCAL_TIME_OFFSET, &off) ||
        !read_time(m, sb2 + SB2_LAST_BERRY_UPDATE, &last))
        return -1;
    int64_t now = local_seconds(clk, &off);
    int64_t then = (int64_t)last.days * 86400 + last.hours * 3600 + last.minutes * 60 + last.seconds;
    return now >= then ? (now - then) / 60 : -1;
}

static uint8_t popcount4(uint8_t v) {
    return (uint8_t)((v & 1) + ((v >> 1) & 1) + ((v >> 2) & 1) + ((v >> 3) & 1));
}

static bool fill_tree(const GameProfile* p, const GameMem* m, uint8_t id, uint8_t t[TREE_SIZE],
                      int64_t pending, BerryTree* out) {
    uint8_t berry = t[0], stage = t[1] & 0x7F;
    uint8_t info[BERRY_INFO_SIZE];
    if (berry == 0 || berry > BERRY_COUNT || stage == 0 || stage > STAGE_BERRIES ||
        !game_mem_copy(m, p->berries + (uint32_t)(berry - 1) * BERRY_INFO_SIZE, info, sizeof(info)))
        return false;
    uint16_t stage_min = (uint16_t)(info[BI_STAGE_HOURS] * 60u);
    if (pending > 0) berry_project(t, stage_min, (uint32_t)(pending > UINT32_MAX ? UINT32_MAX : pending));
    if (t[0] == 0) return false;   /* the projection wiped it */

    memset(out, 0, sizeof(*out));
    out->id = id;
    out->berry = berry;
    out->stage = stage = t[1] & 0x7F;
    out->paused = (t[1] & TREE_STOP_GROWTH) != 0;
    out->watered = (uint8_t)(t[5] >> 4);
    out->regrowth = t[5] & 0x0F;
    out->next_min = (uint16_t)(t[2] | (t[3] << 8));
    out->ripe_min = stage < STAGE_BERRIES ? out->next_min + (uint32_t)(4 - stage) * stage_min : 0;

    if (stage == STAGE_BERRIES && t[4] != 0) {
        out->yield_lo = out->yield_hi = t[4];
    } else {
        /* Best still possible: stages already watered, plus the current and later ones if not passed. */
        uint8_t water = popcount4(out->watered);
        for (uint8_t s = stage; s < STAGE_BERRIES; s++)
            if (!(out->watered & (1u << (s - 1)))) water++;
        berry_yield_band(info[BI_MIN_YIELD], info[BI_MAX_YIELD], water, &out->yield_lo, &out->yield_hi);
    }

    if (stage == STAGE_BERRIES)
        out->status = !out->paused && out->next_min < BERRY_FALLING_MIN ? BERRY_FALLING : BERRY_RIPE;
    else if (out->paused)
        out->status = BERRY_PAUSED;
    else if (!(out->watered & (1u << (stage - 1))))
        out->status = BERRY_WATER;
    else
        out->status = BERRY_GROWING;

    uint32_t n = 0;
    while (n < BERRY_NAME_LEN - 1 && info[n] != 0xFF) n++;
    gen3_decode(info, n, out->name, BERRY_NAME_LEN);
    return true;
}

void berry_read(const BerryPlaces* bp, const GameProfile* p, const GameMem* m, const MirageClock* clk,
                BerryView* out) {
    uint32_t sb1 = 0, sb2 = 0;
    uint8_t flag = 0, sec = 0, in_battle = 0;
    memset(out, 0, sizeof(*out));
    out->here = BERRY_PLACE_NONE;
    if (!p || !game_mem_read32(m, p->save_block1_ptr, &sb1) || !game_mem_read32(m, p->save_block2_ptr, &sb2) ||
        !game_mem_in_ewram(sb1, SB1_BERRY_MIN_SIZE) || !game_mem_in_ewram(sb2, SB2_BERRY_MIN_SIZE))
        return;
    out->valid = true;
    out->place_count = bp->count;
    memcpy(out->place, bp->place, sizeof(out->place));
    out->clock_set = game_mem_read8(m, sb1 + p->rules->sb1_flags + (FLAG_SYS_CLOCK_SET >> 3), &flag) &&
                     (flag & (1u << (FLAG_SYS_CLOCK_SET & 7)));
    out->in_battle = game_mem_read8(m, p->main_in_battle, &in_battle) && (in_battle & 0x02);
    if (game_mem_read8(m, p->map_header + MAP_HEADER_REGION_MAP_SEC, &sec))
        for (uint8_t i = 0; i < bp->count; i++)
            if (bp->place[i].sec == sec) out->here = i;

    /* The game only steps the timers on map loads and every ~136 s, never before the clock is set. */
    int64_t pending = out->clock_set ? pending_minutes(m, sb2, clk) : -1;
    out->projected = pending >= 0;
    for (uint32_t id = 0; id < BERRY_TREE_COUNT && out->count < BERRY_VIEW_MAX; id++) {
        uint8_t t[TREE_SIZE];
        if (!game_mem_copy(m, sb1 + SB1_BERRY_TREES + id * TREE_SIZE, t, TREE_SIZE)) continue;
        BerryTree* tree = &out->tree[out->count];
        if (!fill_tree(p, m, (uint8_t)id, t, pending, tree)) continue;
        tree->place = bp->place_of[id];
        if (tree->place != BERRY_PLACE_NONE) out->growing[tree->place]++;
        out->count++;
    }
}
