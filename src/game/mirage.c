#include "game/mirage.h"
#include "game/emerald_layout.h"
#include "game/game.h"
#include "game/pokemon.h"
#include <stdio.h>
#include <string.h>

/* SaveBlock1.vars[] (include/global.h), indexed by var id - 0x4000. */
#define SB1_VAR(p, id)     ((p)->rules->sb1_vars + 2u * ((uint32_t)(id) - 0x4000u))
#define VAR_MIRAGE_RND_H   0x4024
#define VAR_MIRAGE_RND_L   0x4025
#define VAR_DAYS           0x4040

#define BOX_SLOTS      30
#define BOX_REFRESH    60          /* snapshots between PC box reads */
#define LOCAL_DAYS_MAX 32767       /* gLocalTime.days is s16 */

uint32_t mirage_step(uint32_t v) {
    return 1103515245u * v + 12345u;
}

static bool leap_year(uint32_t y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

uint16_t mirage_day_count(uint8_t year, uint8_t month, uint8_t day) {
    static const uint8_t k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t n = 0;
    for (uint32_t y = 0; y < year; y++) n += leap_year(y) ? 366u : 365u;
    for (uint32_t i = 0; i + 1 < month && i < 12; i++) n += k_days[i];
    if (month > 2 && leap_year(year)) n++;
    return (uint16_t)(n + day);
}

int32_t mirage_local_days(const MirageClock* c, const MirageTime* off) {
    int32_t s = (int32_t)c->second - off->seconds;
    int32_t mi = (int32_t)c->minute - off->minutes;
    int32_t h = (int32_t)c->hour - off->hours;
    int32_t d = (int32_t)mirage_day_count(c->year, c->month, c->day) - off->days;
    if (s < 0) mi--;
    if (mi < 0) h--;
    if (h < 0) d--;
    return d;
}

uint8_t mirage_search(uint32_t value, const MirageMon* owned, uint16_t count, uint32_t k_from,
                      uint32_t k_to, MirageMatch* out, uint8_t max) {
    static uint8_t bits[65536 / 8];
    uint8_t n = 0;
    memset(bits, 0, sizeof(bits));
    for (uint16_t i = 0; i < count; i++) bits[owned[i].pid_lo >> 3] |= (uint8_t)(1u << (owned[i].pid_lo & 7));
    uint32_t v = value;
    for (uint32_t k = 0; k <= k_to && n < max; k++, v = mirage_step(v)) {
        uint16_t hi = (uint16_t)(v >> 16);
        if (k < k_from || !(bits[hi >> 3] & (1u << (hi & 7)))) continue;
        for (uint16_t i = 0; i < count; i++) {
            if (owned[i].pid_lo != hi) continue;
            out[n].k = k;
            out[n].index = i;
            n++;
            break;
        }
    }
    return n;
}

static void add_mon(MirageMon* list, uint16_t* count, uint16_t cap, const MonData* d,
                    MirageWhere where, uint8_t slot) {
    if (*count >= cap) return;
    MirageMon* o = &list[(*count)++];
    memset(o, 0, sizeof(*o));
    o->pid_lo = (uint16_t)d->personality;
    o->species = d->species;
    o->egg = d->is_egg;
    o->where = where;
    o->slot = slot;
}

/* Party every call; boxes now and then (420 decodes), kept between reads. */
static void read_owned(MirageCache* mc, const GameProfile* p, const GameMem* m) {
    if (mc->box_age == 0) {
        uint32_t base;
        mc->box_count = 0;
        if (game_mem_read32(m, p->pokemon_storage_ptr, &base) &&
            game_mem_in_ewram(base, STORAGE_BOXES + MIRAGE_BOX_MONS * BOX_MON_SIZE)) {
            for (uint32_t i = 0; i < MIRAGE_BOX_MONS; i++) {
                uint8_t raw[BOX_MON_SIZE];
                MonData d;
                if (game_mem_copy(m, base + STORAGE_BOXES + i * BOX_MON_SIZE, raw, sizeof(raw)) &&
                    pokemon_decode(raw, false, &d) && !d.bad)
                    add_mon(mc->boxes, &mc->box_count, MIRAGE_BOX_MONS, &d, MIRAGE_BOX,
                            (uint8_t)(i / BOX_SLOTS + 1));
            }
        }
    }
    mc->box_age = (uint8_t)((mc->box_age + 1) % BOX_REFRESH);

    /* The game checks all six slots (IsMirageIslandPresent), not just the party count. */
    mc->owned_count = 0;
    for (uint8_t i = 0; i < 6; i++) {
        uint8_t raw[MON_PARTY_SIZE];
        MonData d;
        if (game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, sizeof(raw)) &&
            pokemon_decode(raw, true, &d) && !d.bad)
            add_mon(mc->owned, &mc->owned_count, MIRAGE_OWNED_MAX, &d, MIRAGE_PARTY, i);
    }
    for (uint16_t i = 0; i < mc->box_count && mc->owned_count < MIRAGE_OWNED_MAX; i++)
        mc->owned[mc->owned_count++] = mc->boxes[i];
}

static uint32_t fnv(uint32_t h, uint32_t v) {
    for (int i = 0; i < 4; i++) h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    return h;
}

static void fill_hit(const GameProfile* p, const GameMem* m, const MirageMon* mon, uint16_t days,
                     MirageHit* h) {
    char name[GAME_NAME_LEN];
    h->where = mon->where;
    h->slot = mon->slot;
    h->days = days;
    if (mon->egg)
        snprintf(h->name, sizeof(h->name), "EGG");
    else {
        game_species_name(m, p, mon->species, name);
        snprintf(h->name, sizeof(h->name), "%s", name);
    }
}

void mirage_read(MirageCache* mc, const GameProfile* p, const GameMem* m, const MirageClock* clk,
                 uint32_t sb1, uint32_t sb2, MirageInfo* out) {
    uint16_t hi, lo, var_days;
    uint8_t flags;
    memset(out, 0, sizeof(*out));
    if (!game_mem_read16(m, sb1 + SB1_VAR(p, VAR_MIRAGE_RND_H), &hi) ||
        !game_mem_read16(m, sb1 + SB1_VAR(p, VAR_MIRAGE_RND_L), &lo) ||
        !game_mem_read16(m, sb1 + SB1_VAR(p, VAR_DAYS), &var_days) ||
        !game_mem_read8(m, sb1 + p->rules->sb1_flags + (FLAG_SYS_CLOCK_SET >> 3), &flags))
        return;
    out->valid = true;
    out->clock_set = (flags >> (FLAG_SYS_CLOCK_SET & 7)) & 1;
    uint32_t raw = ((uint32_t)hi << 16) | lo;
    read_owned(mc, p, m);

    /* deficit: days the next map load applies (UpdatePerDay); negative when the clock went
     * back, and then the game waits until the local day passes VAR_DAYS again. */
    int32_t deficit = 0, cap = 0;
    uint8_t ob[5];
    if (out->clock_set && clk->valid && game_mem_copy(m, sb2 + SB2_LOCAL_TIME_OFFSET, ob, sizeof(ob))) {
        MirageTime off = { (int16_t)(ob[0] | (ob[1] << 8)), (int8_t)ob[2], (int8_t)ob[3], (int8_t)ob[4] };
        int32_t local = mirage_local_days(clk, &off);
        if (local >= 0 && local <= LOCAL_DAYS_MAX) {
            int32_t rtc_left = (int32_t)mirage_day_count(99, 12, 31) -
                               (int32_t)mirage_day_count(clk->year, clk->month, clk->day);
            out->clock_known = true;
            deficit = local - (int32_t)var_days;
            cap = LOCAL_DAYS_MAX - local < rtc_left ? LOCAL_DAYS_MAX - local : rtc_left;
        }
    }
    uint32_t base_k = deficit > 0 ? (uint32_t)deficit : 0;
    out->pending = (uint16_t)base_k;
    out->value = raw;
    for (uint32_t k = 0; k < base_k; k++) out->value = mirage_step(out->value);

    uint32_t key = 2166136261u;
    key = fnv(key, raw);
    key = fnv(key, (uint32_t)deficit);
    key = fnv(key, (uint32_t)cap);
    key = fnv(key, (uint32_t)out->clock_known);
    for (uint16_t i = 0; i < mc->owned_count; i++) {
        const MirageMon* o = &mc->owned[i];
        key = fnv(key, o->pid_lo | ((uint32_t)o->species << 16));
        key = fnv(key, (uint32_t)o->egg | ((uint32_t)o->where << 8) | ((uint32_t)o->slot << 16));
    }
    if (!mc->have || mc->key != key) {
        mc->today_n = mirage_search(raw, mc->owned, mc->owned_count, base_k, base_k, &mc->today_m, 1);
        mc->forecast_n = 0;
        if (out->clock_known && cap > 0 && (int64_t)cap + deficit > (int64_t)base_k)
            mc->forecast_n = mirage_search(raw, mc->owned, mc->owned_count, base_k + 1,
                                           (uint32_t)(cap + deficit), mc->forecast_m,
                                           MIRAGE_FORECAST_MAX);
        mc->key = key;
        mc->have = true;
    }

    if (mc->today_n) fill_hit(p, m, &mc->owned[mc->today_m.index], 0, &out->today);
    for (uint8_t i = 0; i < mc->forecast_n; i++)
        fill_hit(p, m, &mc->owned[mc->forecast_m[i].index],
                 (uint16_t)((int32_t)mc->forecast_m[i].k - deficit), &out->forecast[i]);
    out->forecast_count = mc->forecast_n;
}
