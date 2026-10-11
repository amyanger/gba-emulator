#include "game/nuzlocke.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const k_type_names[NUZ_EV_COUNT] = {
    "start", "owned", "enc", "out", "caught", "gift", "death", "deadused", "wipe", "reload",
};
static const char* const k_kind_names[NUZ_KIND_COUNT] = {
    "first", "dupe", "shiny", "extra", "reroll",
};

const char* nuz_kind_name(uint8_t kind) {
    return kind < NUZ_KIND_COUNT ? k_kind_names[kind] : "?";
}

void nuz_run_init(NuzRun* run) {
    memset(run, 0, sizeof(*run));
    for (int i = 0; i < NUZ_ROUTES; i++) run->routes[i].member = run->routes[i].gift = -1;
    run->last_enc_sec = NUZ_SEC_NONE;
}

int nuz_find_member(const NuzRun* run, NuzKey key) {
    for (int i = 0; i < run->member_count; i++)
        if (nuz_key_eq(run->members[i].key, key)) return i;
    return -1;
}

static int add_member(NuzRun* run, const NuzEvent* ev, uint8_t join) {
    int i = nuz_find_member(run, ev->key);
    if (i >= 0) return i;
    if (run->member_count >= NUZ_MEMBERS_MAX) return -1;
    i = run->member_count++;
    NuzMember* m = &run->members[i];
    memset(m, 0, sizeof(*m));
    m->key = ev->key;
    m->species = ev->species;
    m->sec = ev->sec;
    m->join = join;
    memcpy(m->nick, ev->nick, sizeof(m->nick));
    return i;
}

void nuz_apply(NuzRun* run, const NuzEvent* ev) {
    NuzRoute* r = ev->sec != NUZ_SEC_NONE ? &run->routes[ev->sec] : NULL;
    int m;
    switch (ev->type) {
    case NUZ_EV_START:
        run->active = true;
        run->tid = ev->tid;
        run->start_pt = ev->pt;
        break;
    case NUZ_EV_OWNED:
        m = add_member(run, ev, NUZ_JOIN_OWNED);
        if (r && !r->used) {
            r->used = true;
            r->result = NUZ_RES_OWNED;
            r->species = ev->species;
            r->pt = ev->pt;
            r->member = (int16_t)m;
        }
        break;
    case NUZ_EV_ENC:
        run->last_enc_sec = ev->sec;
        run->last_enc_kind = ev->kind;
        if (ev->kind == NUZ_KIND_FIRST && r) {
            r->used = true;
            r->result = NUZ_RES_PENDING;
            r->species = ev->species;
            r->pt = ev->pt;
        } else if (ev->kind == NUZ_KIND_REROLL) {
            run->rerolls++;
            if (r) r->rerolled = true;
        }
        break;
    case NUZ_EV_OUT:
        if (r && run->last_enc_kind == NUZ_KIND_FIRST && ev->sec == run->last_enc_sec &&
            r->result == NUZ_RES_PENDING)
            r->result = ev->caught ? NUZ_RES_CAUGHT : NUZ_RES_MISSED;
        break;
    case NUZ_EV_CAUGHT:
        m = add_member(run, ev, NUZ_JOIN_CAUGHT);
        if (m >= 0 && ev->kind == NUZ_KIND_REROLL) run->members[m].reroll = true;
        if (m >= 0 && r && ev->kind == NUZ_KIND_FIRST && r->member < 0) r->member = (int16_t)m;
        break;
    case NUZ_EV_GIFT:
        m = add_member(run, ev, NUZ_JOIN_GIFT);
        if (m >= 0 && r && r->gift < 0) r->gift = (int16_t)m;
        break;
    case NUZ_EV_DEATH:
        m = nuz_find_member(run, ev->key);
        if (m >= 0 && !run->members[m].dead) {
            NuzMember* mb = &run->members[m];
            mb->dead = true;
            mb->death_pt = ev->pt;
            mb->death_sec = ev->sec;
            mb->death_level = ev->level;
            mb->foe = ev->foe;
            run->deaths++;
        }
        break;
    case NUZ_EV_DEADUSED:
        run->deadused++;
        m = nuz_find_member(run, ev->key);
        if (m >= 0) run->members[m].used_dead = true;
        break;
    case NUZ_EV_WIPE:
        run->wiped = true;
        break;
    case NUZ_EV_RELOAD:
        run->reloads++;
        for (int i = 0; i < run->member_count; i++) {
            NuzMember* mb = &run->members[i];
            if (mb->dead && mb->death_pt > ev->to && mb->death_pt <= ev->from) {
                mb->reloaded_after = true;
                run->reloaded_after_death = true;
            }
        }
        /* The new timeline starts here; later events push max_pt up again. */
        run->max_pt = ev->pt;
        run->version++;
        return;
    default:
        return;
    }
    if (ev->pt > run->max_pt) run->max_pt = ev->pt;
    run->version++;
}

/* ---- Log lines ---- */

static void put_hex(char* out, const uint8_t* b, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = hex[b[i] >> 4];
        out[2 * i + 1] = hex[b[i] & 15];
    }
    out[2 * n] = '\0';
}

int nuz_format(const NuzEvent* ev, char* out, size_t out_size) {
    char key[32], nick[21];
    int n;
    if (ev->type >= NUZ_EV_COUNT) return -1;
    snprintf(key, sizeof(key), "%08X:%08X:%u", (unsigned)ev->key.pid, (unsigned)ev->key.ot,
             (unsigned)ev->key.variant);
    put_hex(nick, ev->nick, sizeof(ev->nick));
    const char* t = k_type_names[ev->type];
    unsigned pt = (unsigned)ev->pt, sec = ev->sec, sp = ev->species, lv = ev->level;
    switch (ev->type) {
    case NUZ_EV_START:
        n = snprintf(out, out_size, "%s pt=%u tid=%08X", t, pt, (unsigned)ev->tid);
        break;
    case NUZ_EV_OWNED:
    case NUZ_EV_GIFT:
        n = snprintf(out, out_size, "%s pt=%u key=%s sp=%u sec=%u nick=%s", t, pt, key, sp, sec, nick);
        break;
    case NUZ_EV_ENC:
        n = snprintf(out, out_size, "%s pt=%u sec=%u sp=%u lv=%u shiny=%u kind=%s", t, pt, sec, sp, lv,
                     ev->shiny ? 1u : 0u, nuz_kind_name(ev->kind));
        break;
    case NUZ_EV_OUT:
        n = snprintf(out, out_size, "%s pt=%u sec=%u res=%s", t, pt, sec, ev->caught ? "caught" : "missed");
        break;
    case NUZ_EV_CAUGHT:
        n = snprintf(out, out_size, "%s pt=%u key=%s sp=%u sec=%u kind=%s nick=%s", t, pt, key, sp, sec,
                     nuz_kind_name(ev->kind), nick);
        break;
    case NUZ_EV_DEATH:
        n = snprintf(out, out_size, "%s pt=%u key=%s sec=%u sp=%u lv=%u foe=%u", t, pt, key, sec, sp, lv,
                     (unsigned)ev->foe);
        break;
    case NUZ_EV_DEADUSED:
        n = snprintf(out, out_size, "%s pt=%u key=%s", t, pt, key);
        break;
    case NUZ_EV_WIPE:
        n = snprintf(out, out_size, "%s pt=%u", t, pt);
        break;
    default: /* NUZ_EV_RELOAD */
        n = snprintf(out, out_size, "%s pt=%u from=%u to=%u", t, pt, (unsigned)ev->from, (unsigned)ev->to);
        break;
    }
    return (n < 0 || (size_t)n >= out_size) ? -1 : n;
}

static bool parse_u32(const char* s, int base, uint32_t max, uint32_t* out) {
    char* end;
    if (*s == '\0' || *s == '-' || *s == '+' || *s == ' ') return false;
    unsigned long v = strtoul(s, &end, base);
    if (*end != '\0' || v > max) return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_key(const char* s, NuzKey* k) {
    char buf[32];
    uint32_t v;
    if (strlen(s) >= sizeof(buf)) return false;
    strcpy(buf, s);
    char* a = strchr(buf, ':');
    char* b = a ? strchr(a + 1, ':') : NULL;
    if (!a || !b) return false;
    *a = *b = '\0';
    if (!parse_u32(buf, 16, 0xFFFFFFFFu, &k->pid) || !parse_u32(a + 1, 16, 0xFFFFFFFFu, &k->ot) ||
        !parse_u32(b + 1, 10, 1, &v))
        return false;
    k->variant = (uint8_t)v;
    return true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool parse_field(const char* k, const char* v, NuzEvent* ev, bool* have_pt) {
    uint32_t x;
    if (strcmp(k, "pt") == 0) { if (!parse_u32(v, 10, 0xFFFFFFFFu, &ev->pt)) return false; *have_pt = true; }
    else if (strcmp(k, "tid") == 0) return parse_u32(v, 16, 0xFFFFFFFFu, &ev->tid);
    else if (strcmp(k, "key") == 0) return parse_key(v, &ev->key);
    else if (strcmp(k, "sp") == 0) { if (!parse_u32(v, 10, 0xFFFF, &x)) return false; ev->species = (uint16_t)x; }
    else if (strcmp(k, "foe") == 0) { if (!parse_u32(v, 10, 0xFFFF, &x)) return false; ev->foe = (uint16_t)x; }
    else if (strcmp(k, "lv") == 0) { if (!parse_u32(v, 10, 255, &x)) return false; ev->level = (uint8_t)x; }
    else if (strcmp(k, "sec") == 0) { if (!parse_u32(v, 10, 255, &x)) return false; ev->sec = (uint8_t)x; }
    else if (strcmp(k, "shiny") == 0) { if (!parse_u32(v, 10, 1, &x)) return false; ev->shiny = x != 0; }
    else if (strcmp(k, "from") == 0) return parse_u32(v, 10, 0xFFFFFFFFu, &ev->from);
    else if (strcmp(k, "to") == 0) return parse_u32(v, 10, 0xFFFFFFFFu, &ev->to);
    else if (strcmp(k, "res") == 0) {
        if (strcmp(v, "caught") == 0) ev->caught = true;
        else if (strcmp(v, "missed") == 0) ev->caught = false;
        else return false;
    } else if (strcmp(k, "kind") == 0) {
        int i = 0;
        while (i < NUZ_KIND_COUNT && strcmp(v, k_kind_names[i]) != 0) i++;
        if (i == NUZ_KIND_COUNT) return false;
        ev->kind = (uint8_t)i;
    } else if (strcmp(k, "nick") == 0) {
        if (strlen(v) != 2 * sizeof(ev->nick)) return false;
        for (size_t i = 0; i < sizeof(ev->nick); i++) {
            int hi = hex_digit(v[2 * i]), lo = hex_digit(v[2 * i + 1]);
            if (hi < 0 || lo < 0) return false;
            ev->nick[i] = (uint8_t)(hi << 4 | lo);
        }
    }
    /* Unknown keys are skipped so newer files still load. */
    return true;
}

bool nuz_parse(const char* line, NuzEvent* ev) {
    char buf[256];
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, line, len);
    buf[len] = '\0';

    memset(ev, 0, sizeof(*ev));
    ev->sec = NUZ_SEC_NONE;
    /* Split on spaces by hand: strtok_r isn't available on MSVC. */
    char* tok[24];
    int ntok = 0;
    for (char* p = buf; *p;) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        if (ntok == 24) return false;
        tok[ntok++] = p;
        while (*p && *p != ' ') p++;
    }
    if (ntok == 0) return false;
    int type = 0;
    while (type < NUZ_EV_COUNT && strcmp(tok[0], k_type_names[type]) != 0) type++;
    if (type == NUZ_EV_COUNT) return false;
    ev->type = (uint8_t)type;
    bool have_pt = false;
    for (int i = 1; i < ntok; i++) {
        char* eq = strchr(tok[i], '=');
        if (!eq || eq == tok[i]) return false;
        *eq = '\0';
        if (!parse_field(tok[i], eq + 1, ev, &have_pt)) return false;
    }
    return have_pt;
}

/* ---- Detection ---- */

void nuz_tracker_reset(NuzTracker* t, const NuzRun* run) {
    memset(t, 0, sizeof(*t));
    t->high = run->max_pt;
}

static bool is_dupe(const NuzRun* run, const uint16_t* family, uint16_t species) {
    if (species >= NUZ_SPECIES_MAX) return false;
    uint16_t fam = family[species];
    for (int i = 0; i < run->member_count; i++) {
        uint16_t s = run->members[i].species;
        if (s < NUZ_SPECIES_MAX && family[s] == fam) return true;
    }
    return false;
}

static uint8_t classify(const NuzRun* run, const NuzFacts* f, const uint16_t* family, uint8_t sec,
                        bool shiny) {
    if (shiny) return NUZ_KIND_SHINY;
    if (sec == NUZ_SEC_NONE) return NUZ_KIND_EXTRA;
    const NuzRoute* r = &run->routes[sec];
    if (!r->used) return is_dupe(run, family, f->enemy_species) ? NUZ_KIND_DUPE : NUZ_KIND_FIRST;
    return r->pt > f->play_time ? NUZ_KIND_REROLL : NUZ_KIND_EXTRA;
}

static uint8_t to_sec(uint8_t v, uint8_t sec_count) {
    return v < sec_count ? v : NUZ_SEC_NONE;
}

static void new_event(NuzEvent* ev, uint8_t type, uint32_t pt) {
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->pt = pt;
    ev->sec = NUZ_SEC_NONE;
}

static bool emitted_key(const NuzEvent* out, uint8_t n, NuzKey key) {
    for (uint8_t i = 0; i < n; i++)
        if ((out[i].type == NUZ_EV_CAUGHT || out[i].type == NUZ_EV_GIFT) && nuz_key_eq(out[i].key, key))
            return true;
    return false;
}

static uint8_t track_joins(NuzTracker* t, const NuzRun* run, const NuzMon* mons, uint16_t count,
                           uint8_t sec_count, uint32_t pt, NuzEvent* out, uint8_t n) {
    for (uint16_t i = 0; i < count && n < NUZ_EVENTS_MAX; i++) {
        const NuzMon* mon = &mons[i];
        if (mon->species == 0 || mon->egg || nuz_find_member(run, mon->key) >= 0 ||
            emitted_key(out, n, mon->key))
            continue;
        NuzEvent* ev = &out[n++];
        bool caught = t->pending && mon->key.pid == t->pending_pid;
        new_event(ev, caught ? NUZ_EV_CAUGHT : NUZ_EV_GIFT, pt);
        ev->key = mon->key;
        ev->species = mon->species;
        memcpy(ev->nick, mon->nick, sizeof(ev->nick));
        if (caught) {
            ev->sec = t->pending_sec;
            ev->kind = t->pending_kind;
            t->pending = false;
        } else {
            ev->sec = to_sec(mon->met, sec_count);
        }
    }
    return n;
}

uint8_t nuz_track_step(NuzTracker* t, const NuzRun* run, const NuzFacts* f, const uint16_t* family,
                       uint8_t sec_count, NuzEvent out[NUZ_EVENTS_MAX]) {
    uint8_t n = 0;
    uint32_t pt = f->play_time;
    uint8_t here = to_sec(f->mapsec, sec_count);

    /* Reloads: play time never runs backwards on its own. One event per jump,
     * written once play has carried on, so a held rewind logs a single reload. */
    if (!t->behind) {
        if (pt < t->high) {
            t->behind = true;
            t->low = pt;
            t->behind_frames = 0;
        } else {
            t->high = pt;
        }
    } else if (pt >= t->high && t->behind_frames < NUZ_GLITCH_FRAMES) {
        t->behind = false;
        t->high = pt;
    } else {
        if (t->behind_frames < 255) t->behind_frames++;
        if (pt < t->low) t->low = pt;
        if (pt >= t->low + NUZ_RELOAD_SETTLE) {
            NuzEvent* ev = &out[n++];
            new_event(ev, NUZ_EV_RELOAD, pt);
            ev->from = t->high;
            ev->to = t->low;
            t->behind = false;
            t->high = pt;
        }
    }

    /* Battle edges. A battle already running when tracking began is ignored. */
    bool rising = f->in_battle && !t->prev_in_battle;
    bool falling = !f->in_battle && t->prev_in_battle;
    if (rising) {
        t->outcome = 0;
        t->deadused_logged = false;
        t->battle_ignored = !t->have_prev || f->enemy_species == 0 ||
                            (f->battle_flags & (NUZ_BATTLE_TRAINER | NUZ_BATTLE_FIRST_BATTLE |
                                                NUZ_BATTLE_WALLY_TUTORIAL));
        if (!t->battle_ignored) {
            bool shiny = nuz_is_shiny(f->enemy_pid, f->enemy_ot);
            NuzEvent* ev = &out[n++];
            new_event(ev, NUZ_EV_ENC, pt);
            ev->sec = here;
            ev->species = f->enemy_species;
            ev->level = f->enemy_level;
            ev->shiny = shiny;
            ev->kind = classify(run, f, family, here, shiny);
            t->enc_sec = here;
            t->enc_kind = ev->kind;
            t->enc_pid = f->enemy_pid;
        }
    }
    if (f->in_battle && f->battle_outcome != 0) t->outcome = f->battle_outcome;
    if (falling && !t->battle_ignored) {
        NuzEvent* ev = &out[n++];
        new_event(ev, NUZ_EV_OUT, pt);
        ev->sec = t->enc_sec;
        ev->caught = t->outcome == NUZ_OUTCOME_CAUGHT;
        if (ev->caught) {
            t->pending = true;
            t->pending_pid = t->enc_pid;
            t->pending_sec = t->enc_sec;
            t->pending_kind = t->enc_kind;
        }
    }
    t->prev_in_battle = f->in_battle;
    t->have_prev = true;

    /* New Pokemon. A catch lands in the party before the battle ends, so wait for it. */
    if (!f->in_battle) {
        n = track_joins(t, run, f->party, f->party_count, sec_count, pt, out, n);
        if (f->has_boxes) n = track_joins(t, run, f->boxes, f->box_count, sec_count, pt, out, n);
    }

    /* Deaths: a member seen alive whose HP then stays 0 for NUZ_DEATH_FRAMES frames. */
    NuzHpWatch next[NUZ_PARTY_MAX];
    memset(next, 0, sizeof(next));
    uint8_t members = 0, alive = 0;
    for (uint8_t i = 0; i < f->party_count; i++) {
        const NuzMon* mon = &f->party[i];
        int m = mon->species && !mon->egg ? nuz_find_member(run, mon->key) : -1;
        if (m < 0) continue;
        members++;
        if (run->members[m].dead) continue;
        NuzHpWatch* w = &next[i];
        w->key = mon->key;
        for (uint8_t j = 0; j < NUZ_PARTY_MAX; j++)
            if (nuz_key_eq(t->hp[j].key, mon->key)) {
                w->alive_seen = t->hp[j].alive_seen;
                w->zero_frames = t->hp[j].zero_frames;
            }
        if (mon->hp > 0) {
            w->alive_seen = true;
            w->zero_frames = 0;
            alive++;
            continue;
        }
        if (!w->alive_seen) {
            alive++;  /* never seen alive: not ours to judge */
            continue;
        }
        if (++w->zero_frames < NUZ_DEATH_FRAMES) {
            alive++;
            continue;
        }
        if (n < NUZ_EVENTS_MAX) {
            NuzEvent* ev = &out[n++];
            new_event(ev, NUZ_EV_DEATH, pt);
            ev->key = mon->key;
            ev->sec = here;
            ev->species = mon->species;
            ev->level = mon->level;
            ev->foe = f->in_battle ? f->enemy_species : 0;
        }
    }
    memcpy(t->hp, next, sizeof(next));
    if (!run->wiped && members > 0 && alive == 0 && n < NUZ_EVENTS_MAX)
        new_event(&out[n++], NUZ_EV_WIPE, pt);

    /* A dead member sent into battle (healed, still dead to the rules). */
    if (f->in_battle && !t->deadused_logged && f->active_party >= 0 && f->active_party < f->party_count) {
        const NuzMon* mon = &f->party[f->active_party];
        int m = mon->species ? nuz_find_member(run, mon->key) : -1;
        if (m >= 0 && run->members[m].dead && mon->hp > 0 && n < NUZ_EVENTS_MAX) {
            NuzEvent* ev = &out[n++];
            new_event(ev, NUZ_EV_DEADUSED, pt);
            ev->key = mon->key;
            t->deadused_logged = true;
        }
    }
    return n;
}

uint16_t nuz_backfill(const NuzFacts* f, uint8_t sec_count, NuzEvent* out, uint16_t max) {
    uint16_t n = 0;
    if (max == 0) return 0;
    new_event(&out[n], NUZ_EV_START, f->play_time);
    out[n++].tid = f->trainer_id;
    for (int pass = 0; pass < 2; pass++) {
        const NuzMon* mons = pass == 0 ? f->party : f->boxes;
        uint16_t count = pass == 0 ? f->party_count : f->box_count;
        for (uint16_t i = 0; i < count && n < max; i++) {
            const NuzMon* mon = &mons[i];
            bool seen = false;
            for (uint16_t j = 1; j < n && !seen; j++) seen = nuz_key_eq(out[j].key, mon->key);
            if (mon->species == 0 || mon->egg || seen) continue;
            NuzEvent* ev = &out[n++];
            new_event(ev, NUZ_EV_OWNED, f->play_time);
            ev->key = mon->key;
            ev->species = mon->species;
            ev->sec = to_sec(mon->met, sec_count);
            memcpy(ev->nick, mon->nick, sizeof(ev->nick));
        }
    }
    return n;
}
