#include "game/runlog.h"
#include "game/logline.h"
#include <stdio.h>
#include <string.h>

#define NEW_GAME_PT (60u * 60u * 60u) /* first hour of play, in frames */

static const char* const k_type_names[RUN_EV_COUNT] = {
    "start", "join", "evolve", "faint", "whiteout", "badge", "story", "battle", "reload",
};
static const char* const k_how_names[RUN_HOW_COUNT] = {
    "caught", "gift", "hatched", "traded", "backfill",
};
#define RUN_KIND_COUNT 3
static const char* const k_kind_names[RUN_KIND_COUNT] = { "wild", "trainer", "other" };

void run_log_init(RunLog* log) {
    memset(log, 0, sizeof(*log));
    log->low_permille = log->folded_low = 1000;
}

int run_find_member(const RunLog* log, NuzKey key) {
    for (int i = 0; i < log->member_count; i++)
        if (nuz_key_eq(log->members[i].key, key)) return i;
    return -1;
}

static int find_folded(const RunLog* log, NuzKey key) {
    for (int i = 0; i < log->folded_ko_count; i++)
        if (nuz_key_eq(log->folded_kos[i].key, key)) return i;
    return -1;
}

/* ---- Aggregates: one per-event function for live apply and rebuild ---- */

/* Battle counters that survive folding: added on apply, subtracted for live battles
 * before a rebuild, so what remains is the folded part. */
static void battle_counters(RunLog* log, const RunEvent* ev, int sign) {
    if (ev->kind == RUN_BATTLE_WILD && ev->foe < NUZ_SPECIES_MAX)
        log->wild_count[ev->foe] = (uint16_t)(log->wild_count[ev->foe] + sign);
    if (ev->shiny) log->shinies_seen = (uint16_t)(log->shinies_seen + sign);
    if (ev->upset) log->upsets = (uint16_t)(log->upsets + sign);
    log->indirect = (uint16_t)(log->indirect + sign * ev->indirect);
}

static void fold_one(RunLog* log, const RunEvent* ev) {
    int m;
    switch (ev->type) {
    case RUN_EV_START:
        log->active = true;
        log->tid = ev->tid;
        log->start_pt = ev->pt;
        log->started_new_game = ev->pt < NEW_GAME_PT;
        for (int i = 0; i < log->member_count; i++)
            if (log->members[i].how == RUN_HOW_BACKFILL) log->started_new_game = false;
        break;
    case RUN_EV_JOIN: {
        if (ev->how == RUN_HOW_BACKFILL) log->started_new_game = false;
        if (run_find_member(log, ev->key) >= 0 || log->member_count >= RUN_MEMBERS_MAX) break;
        RunMember* mb = &log->members[log->member_count++];
        memset(mb, 0, sizeof(*mb));
        mb->key = ev->key;
        mb->species = mb->join_species = ev->species;
        mb->join_level = mb->level = ev->level;
        mb->sec = ev->sec;
        mb->how = ev->how;
        mb->ball = ev->ball;
        mb->shiny = ev->shiny;
        mb->live = ev->how != RUN_HOW_BACKFILL && !ev->untimed;
        mb->join_pt = ev->pt;
        memcpy(mb->nick, ev->nick, sizeof(mb->nick));
        /* KOs from folded battles are immune to truncation, so they come back with the member. */
        /* The level is approximate: the folded battles' last KO level, if higher than the
         * join level, stands in for "latest known" since their order against EVOLVE is lost. */
        int f = find_folded(log, ev->key);
        if (f >= 0) {
            mb->kos = log->folded_kos[f].count;
            if (log->folded_kos[f].level > mb->level) mb->level = log->folded_kos[f].level;
        }
        if (ev->shiny && ev->how == RUN_HOW_CAUGHT) log->shinies_caught++;
        break;
    }
    case RUN_EV_EVOLVE:
        m = run_find_member(log, ev->key);
        if (m >= 0) {
            RunMember* mb = &log->members[m];
            mb->species = mb->evo_species = ev->species;
            mb->evo_pt = ev->pt;
            if (ev->level) mb->level = ev->level;
        }
        break;
    case RUN_EV_FAINT:
        log->faints++;
        m = run_find_member(log, ev->key);
        if (m >= 0) log->members[m].faints++;
        break;
    case RUN_EV_WHITEOUT:
        log->whiteouts++;
        break;
    case RUN_EV_BADGE:
        if (ev->id >= 1 && ev->id <= RUN_BADGES && !log->badge[ev->id - 1]) {
            log->badge[ev->id - 1] = true;
            log->badge_untimed[ev->id - 1] = ev->untimed;
            log->badge_pt[ev->id - 1] = ev->pt;
        }
        break;
    case RUN_EV_STORY:
        if (ev->id < RUN_STORY_COUNT && !log->story[ev->id]) {
            log->story[ev->id] = true;
            log->story_untimed[ev->id] = ev->untimed;
            log->story_pt[ev->id] = ev->pt;
        }
        break;
    case RUN_EV_BATTLE:
        battle_counters(log, ev, 1);
        for (uint8_t i = 0; i < ev->ko_count && i < RUN_KO_SLOTS; i++) {
            m = run_find_member(log, ev->kos[i].key);
            if (m < 0) continue;
            log->members[m].kos = (uint16_t)(log->members[m].kos + ev->kos[i].count);
            if (ev->kos[i].level) log->members[m].level = ev->kos[i].level;
        }
        if (ev->low < log->low_permille) {
            log->low_permille = ev->low;
            log->low_foe = ev->foe;
            log->low_key = ev->low_by;
        }
        break;
    default:
        break;
    }
}

/* R9: move the oldest wild battle into the folded totals. False if there is none. */
static bool fold_oldest_wild(RunLog* log) {
    int w = 0;
    while (w < log->bt_count && log->bt[w].kind != RUN_BATTLE_WILD) w++;
    if (w == log->bt_count) return false;
    const RunEvent* b = &log->bt[w];
    for (uint8_t i = 0; i < b->ko_count && i < RUN_KO_SLOTS; i++) {
        /* Keep only credit the member really got: it joined before this battle. */
        int m = run_find_member(log, b->kos[i].key);
        if (m < 0) continue;
        bool joined_before = false;
        for (int j = 0; j < log->ev_count && !joined_before; j++)
            joined_before = log->ev[j].type == RUN_EV_JOIN && nuz_key_eq(log->ev[j].key, b->kos[i].key) &&
                            log->ev[j].seq < b->seq;
        if (!joined_before) continue;
        int f = find_folded(log, b->kos[i].key);
        if (f < 0) {
            if (log->folded_ko_count >= RUN_MEMBERS_MAX) continue;
            f = log->folded_ko_count++;
            memset(&log->folded_kos[f], 0, sizeof(log->folded_kos[f]));
            log->folded_kos[f].key = b->kos[i].key;
        }
        RunKo* k = &log->folded_kos[f];
        uint32_t sum = (uint32_t)k->count + b->kos[i].count;
        k->count = (uint16_t)(sum > 0xFFFFu ? 0xFFFFu : sum);
        if (b->kos[i].level) k->level = b->kos[i].level;
    }
    if (b->low < log->folded_low) {
        log->folded_low = b->low;
        log->folded_low_foe = b->foe;
        log->folded_low_by = b->low_by;
        log->folded_low_seq = b->seq;
    }
    log->folded_wild++;
    memmove(&log->bt[w], &log->bt[w + 1], (size_t)(log->bt_count - w - 1) * sizeof(log->bt[0]));
    log->bt_count--;
    return true;
}

/* START, backfilled JOINs and untimed events survive a reload. */
static bool immune(const RunEvent* ev) {
    return ev->type == RUN_EV_START || ev->untimed ||
           (ev->type == RUN_EV_JOIN && ev->how == RUN_HOW_BACKFILL);
}

static void fold_low(RunLog* log) {
    if (log->folded_low < log->low_permille) {
        log->low_permille = log->folded_low;
        log->low_foe = log->folded_low_foe;
        log->low_key = log->folded_low_by;
    }
}

static void reload(RunLog* log, uint32_t to) {
    for (int i = 0; i < log->bt_count; i++) battle_counters(log, &log->bt[i], -1);

    uint16_t n = 0;
    for (int i = 0; i < log->ev_count; i++) {
        const RunEvent* ev = &log->ev[i];
        if (ev->pt <= to || immune(ev)) {
            if (n != i) log->ev[n] = *ev;
            n++;
            continue;
        }
        int o = -1;
        if (ev->type == RUN_EV_BADGE && ev->id >= 1 && ev->id <= RUN_BADGES) o = ev->id - 1;
        if (ev->type == RUN_EV_STORY && ev->id < RUN_STORY_COUNT) o = RUN_BADGES + ev->id;
        if (o >= 0) {
            log->orphan[o] = true;
            log->orphan_pt[o] = ev->pt;
        }
    }
    log->ev_count = n;
    n = 0;
    for (int i = 0; i < log->bt_count; i++) {
        if (log->bt[i].pt > to && !immune(&log->bt[i])) continue;
        if (n != i) log->bt[n] = log->bt[i];
        n++;
    }
    log->bt_count = n;

    /* Rebuild everything derived from the lists; the battle counters now hold the folded part. */
    log->active = false;
    log->tid = log->start_pt = 0;
    log->started_new_game = false;
    log->member_count = 0;
    memset(log->badge, 0, sizeof(log->badge));
    memset(log->badge_untimed, 0, sizeof(log->badge_untimed));
    memset(log->badge_pt, 0, sizeof(log->badge_pt));
    memset(log->story, 0, sizeof(log->story));
    memset(log->story_untimed, 0, sizeof(log->story_untimed));
    memset(log->story_pt, 0, sizeof(log->story_pt));
    log->faints = log->whiteouts = log->shinies_caught = 0;
    log->low_permille = 1000;
    memset(&log->low_key, 0, sizeof(log->low_key));
    log->low_foe = 0;
    /* Both lists are in seq order: merge them. The folded closest call is replayed at its
     * own seq so ties resolve exactly as they did live. */
    int e = 0, b = 0;
    bool low_done = log->folded_low >= 1000;
    while (e < log->ev_count || b < log->bt_count) {
        const RunEvent* next;
        if (b == log->bt_count || (e < log->ev_count && log->ev[e].seq < log->bt[b].seq))
            next = &log->ev[e++];
        else
            next = &log->bt[b++];
        if (!low_done && next->seq > log->folded_low_seq) {
            fold_low(log);
            low_done = true;
        }
        fold_one(log, next);
    }
    if (!low_done) fold_low(log);
}

static bool has_wild(const RunLog* log) {
    for (int i = 0; i < log->bt_count; i++)
        if (log->bt[i].kind == RUN_BATTLE_WILD) return true;
    return false;
}

static void clamp_key(NuzKey* k) {
    if (k->variant > 1) k->variant = 1;
}

/* Out-of-range fields get the value a format/parse round trip would give, so live == replay. */
static void normalize(RunEvent* ev) {
    if (ev->how >= RUN_HOW_COUNT) ev->how = RUN_HOW_GIFT;
    if (ev->kind > RUN_BATTLE_OTHER) ev->kind = RUN_BATTLE_OTHER;
    if (ev->ko_count > RUN_KO_SLOTS) ev->ko_count = RUN_KO_SLOTS;
    if (!ev->has_ace) {
        memset(&ev->ace_by, 0, sizeof(ev->ace_by));
        ev->ace_species = 0;
    }
    clamp_key(&ev->key);
    clamp_key(&ev->ace_by);
    clamp_key(&ev->low_by);
    for (uint8_t i = 0; i < ev->ko_count; i++) clamp_key(&ev->kos[i].key);
}

void run_apply(RunLog* log, const RunEvent* in) {
    RunEvent ev = *in;
    normalize(&ev);
    ev.seq = log->next_seq++;
    log->version++;
    if (ev.type == RUN_EV_RELOAD) {
        reload(log, ev.to_pt);
    } else if (ev.type == RUN_EV_BATTLE) {
        if (log->bt_count < RUN_BATTLES_MAX || fold_oldest_wild(log)) {
            log->bt[log->bt_count++] = ev;
            fold_one(log, &ev);
        }
    } else if (ev.type < RUN_EV_COUNT && log->ev_count < RUN_EVENTS_MAX) {
        log->ev[log->ev_count++] = ev;
        fold_one(log, &ev);
    }
    log->events_full = log->ev_count >= RUN_EVENTS_MAX;
    log->battles_full = log->bt_count >= RUN_BATTLES_MAX && !has_wild(log);
}

/* ---- Log lines ---- */

typedef struct {
    char* out;
    size_t size, len;
    bool bad;
} Line;

static void put(Line* w, const char* s) {
    size_t n = strlen(s);
    if (w->bad || w->len + n >= w->size) {
        w->bad = true;
        return;
    }
    memcpy(w->out + w->len, s, n + 1);
    w->len += n;
}

static void put_u(Line* w, const char* k, uint32_t v) {
    char buf[32];
    snprintf(buf, sizeof(buf), " %s=%u", k, (unsigned)v);
    put(w, buf);
}

static void key_text(char* buf, size_t size, NuzKey k) {
    snprintf(buf, size, "%08X:%08X:%u", (unsigned)k.pid, (unsigned)k.ot, (unsigned)k.variant);
}

static void put_key(Line* w, NuzKey k) {
    char buf[64];
    snprintf(buf, sizeof(buf), " pid=%08X ot=%08X var=%u", (unsigned)k.pid, (unsigned)k.ot,
             (unsigned)k.variant);
    put(w, buf);
}

int run_format(const RunEvent* in, char* out, size_t out_size) {
    char buf[64];
    Line w = { out, out_size, 0, false };
    if (in->type >= RUN_EV_COUNT || out_size == 0) return -1;
    RunEvent norm = *in;
    normalize(&norm);
    const RunEvent* ev = &norm;
    out[0] = '\0';
    put(&w, k_type_names[ev->type]);
    put_u(&w, "pt", ev->pt);
    switch (ev->type) {
    case RUN_EV_START:
        snprintf(buf, sizeof(buf), " tid=%08X", (unsigned)ev->tid);
        put(&w, buf);
        break;
    case RUN_EV_JOIN: {
        put_key(&w, ev->key);
        put_u(&w, "sp", ev->species);
        put_u(&w, "lv", ev->level);
        put_u(&w, "sec", ev->sec);
        put(&w, " how=");
        put(&w, ev->how < RUN_HOW_COUNT ? k_how_names[ev->how] : "gift");
        put_u(&w, "ball", ev->ball);
        put_u(&w, "sh", ev->shiny);
        put_u(&w, "nd", ev->newdex);
        char nick[2 * sizeof(ev->nick) + 1];
        logline_hex_encode(nick, ev->nick, sizeof(ev->nick));
        put(&w, " nick=");
        put(&w, nick);
        break;
    }
    case RUN_EV_EVOLVE:
        put_key(&w, ev->key);
        put_u(&w, "sp", ev->species);
        put_u(&w, "from", ev->from);
        put_u(&w, "lv", ev->level);
        break;
    case RUN_EV_FAINT:
        put_key(&w, ev->key);
        put_u(&w, "sp", ev->species);
        put_u(&w, "lv", ev->level);
        put_u(&w, "foe", ev->foe);
        put_u(&w, "sec", ev->sec);
        break;
    case RUN_EV_WHITEOUT:
        put_u(&w, "sec", ev->sec);
        break;
    case RUN_EV_BADGE:
        put_u(&w, "id", ev->id);
        put_u(&w, "tmax", ev->team_max);
        put_u(&w, "tavg", ev->team_avg);
        put_u(&w, "dex", ev->dex);
        put_u(&w, "steps", ev->steps);
        break;
    case RUN_EV_STORY:
        put_u(&w, "id", ev->id);
        break;
    case RUN_EV_BATTLE:
        put(&w, " kind=");
        put(&w, ev->kind < RUN_KIND_COUNT ? k_kind_names[ev->kind] : "other");
        put_u(&w, "tr", ev->trainer);
        put_u(&w, "foe", ev->foe);
        put_u(&w, "sh", ev->shiny);
        put_u(&w, "out", ev->outcome);
        for (uint8_t i = 0; i < ev->ko_count && i < RUN_KO_SLOTS; i++) {
            char k[32];
            key_text(k, sizeof(k), ev->kos[i].key);
            snprintf(buf, sizeof(buf), "%s%s:%u:%u", i == 0 ? " ko=" : ",", k, (unsigned)ev->kos[i].count,
                     (unsigned)ev->kos[i].level);
            put(&w, buf);
        }
        put_u(&w, "ind", ev->indirect);
        put_u(&w, "up", ev->upset);
        put_u(&w, "low", ev->low);
        key_text(buf, sizeof(buf), ev->low_by);
        put(&w, " lowby=");
        put(&w, buf);
        put_u(&w, "fnt", ev->faints);
        if (ev->has_ace) {
            char k[32];
            key_text(k, sizeof(k), ev->ace_by);
            put(&w, " ace=");
            put(&w, k);
            put_u(&w, "acesp", ev->ace_species);
        }
        break;
    default: /* RUN_EV_RELOAD */
        put_u(&w, "to", ev->to_pt);
        break;
    }
    if (ev->untimed) put(&w, " u=1");
    return w.bad ? -1 : (int)w.len;
}

/* Splits s in place on sep into at most max parts; returns the count, 0 if too many. */
static int split(char* s, char sep, char** part, int max) {
    int n = 0;
    part[n++] = s;
    for (char* p = s; *p; p++)
        if (*p == sep) {
            if (n == max) return 0;
            *p = '\0';
            part[n++] = p + 1;
        }
    return n;
}

/* p[0..2] = pid (hex), ot (hex), variant. */
static bool parse_key_parts(char** p, NuzKey* k) {
    uint32_t v;
    if (!logline_u32(p[0], 16, 0xFFFFFFFFu, &k->pid) || !logline_u32(p[1], 16, 0xFFFFFFFFu, &k->ot) ||
        !logline_u32(p[2], 10, 1, &v))
        return false;
    k->variant = (uint8_t)v;
    return true;
}

static bool parse_kos(char* v, RunEvent* ev) {
    char* item[RUN_KO_SLOTS];
    int n = split(v, ',', item, RUN_KO_SLOTS);
    if (n == 0) return false;
    for (int i = 0; i < n; i++) {
        char* p[5];
        if (split(item[i], ':', p, 5) != 5) return false;
        RunKo* k = &ev->kos[i];
        if (!parse_key_parts(p, &k->key) || !logline_u16(p[3], &k->count) || !logline_u8(p[4], &k->level))
            return false;
    }
    ev->ko_count = (uint8_t)n;
    return true;
}

static bool parse_field(const char* k, char* v, RunEvent* ev, bool* have_pt) {
    uint32_t x;
    if (strcmp(k, "pt") == 0) { if (!logline_u32(v, 10, 0xFFFFFFFFu, &ev->pt)) return false; *have_pt = true; }
    else if (strcmp(k, "tid") == 0) return logline_u32(v, 16, 0xFFFFFFFFu, &ev->tid);
    else if (strcmp(k, "pid") == 0) return logline_u32(v, 16, 0xFFFFFFFFu, &ev->key.pid);
    else if (strcmp(k, "ot") == 0) return logline_u32(v, 16, 0xFFFFFFFFu, &ev->key.ot);
    else if (strcmp(k, "var") == 0) { if (!logline_u32(v, 10, 1, &x)) return false; ev->key.variant = (uint8_t)x; }
    else if (strcmp(k, "sp") == 0) return logline_u16(v, &ev->species);
    else if (strcmp(k, "from") == 0) return logline_u16(v, &ev->from);
    else if (strcmp(k, "lv") == 0) return logline_u8(v, &ev->level);
    else if (strcmp(k, "sec") == 0) return logline_u8(v, &ev->sec);
    else if (strcmp(k, "how") == 0) return logline_name(v, k_how_names, RUN_HOW_COUNT, &ev->how);
    else if (strcmp(k, "ball") == 0) return logline_u8(v, &ev->ball);
    else if (strcmp(k, "sh") == 0) return logline_bool(v, &ev->shiny);
    else if (strcmp(k, "nd") == 0) return logline_bool(v, &ev->newdex);
    else if (strcmp(k, "u") == 0) return logline_bool(v, &ev->untimed);
    else if (strcmp(k, "nick") == 0) return logline_hex_decode(v, ev->nick, sizeof(ev->nick));
    else if (strcmp(k, "foe") == 0) return logline_u16(v, &ev->foe);
    else if (strcmp(k, "id") == 0) return logline_u8(v, &ev->id);
    else if (strcmp(k, "tmax") == 0) return logline_u8(v, &ev->team_max);
    else if (strcmp(k, "tavg") == 0) return logline_u8(v, &ev->team_avg);
    else if (strcmp(k, "dex") == 0) return logline_u16(v, &ev->dex);
    else if (strcmp(k, "steps") == 0) return logline_u32(v, 10, 0xFFFFFFFFu, &ev->steps);
    else if (strcmp(k, "kind") == 0) return logline_name(v, k_kind_names, RUN_KIND_COUNT, &ev->kind);
    else if (strcmp(k, "tr") == 0) return logline_u16(v, &ev->trainer);
    else if (strcmp(k, "out") == 0) return logline_u8(v, &ev->outcome);
    else if (strcmp(k, "ko") == 0) return parse_kos(v, ev);
    else if (strcmp(k, "ind") == 0) return logline_u8(v, &ev->indirect);
    else if (strcmp(k, "up") == 0) return logline_u8(v, &ev->upset);
    else if (strcmp(k, "low") == 0) return logline_u16(v, &ev->low);
    else if (strcmp(k, "lowby") == 0) {
        char* p[3];
        return split(v, ':', p, 3) == 3 && parse_key_parts(p, &ev->low_by);
    }
    else if (strcmp(k, "fnt") == 0) return logline_u8(v, &ev->faints);
    else if (strcmp(k, "ace") == 0) {
        char* p[3];
        if (split(v, ':', p, 3) != 3 || !parse_key_parts(p, &ev->ace_by)) return false;
        ev->has_ace = true;
    }
    else if (strcmp(k, "acesp") == 0) return logline_u16(v, &ev->ace_species);
    else if (strcmp(k, "to") == 0) return logline_u32(v, 10, 0xFFFFFFFFu, &ev->to_pt);
    /* Unknown keys are skipped so newer files still load. */
    return true;
}

bool run_parse(const char* line, RunEvent* ev) {
    char buf[RUN_LINE_MAX];
    char* tok[40];
    memset(ev, 0, sizeof(*ev));
    ev->sec = NUZ_SEC_NONE;
    ev->low = 1000;
    int ntok = logline_tokens(line, buf, sizeof(buf), tok, 40);
    if (ntok == 0) return false;
    uint8_t type;
    if (!logline_name(tok[0], k_type_names, RUN_EV_COUNT, &type)) return false;
    ev->type = type;
    bool have_pt = false;
    for (int i = 1; i < ntok; i++) {
        char *k, *v;
        if (!logline_field(tok[i], &k, &v) || !parse_field(k, v, ev, &have_pt)) return false;
    }
    return have_pt;
}
