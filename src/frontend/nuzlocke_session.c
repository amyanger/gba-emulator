#include "frontend/nuzlocke_session.h"
#include "common.h"
#include "game/gen3_text.h"
#include <string.h>

#define NUZ_HEADER       "nuzlocke 1"
#define BOX_EVERY        60   /* frames between PC box reads */
#define VIEW_EVERY       30   /* frames between view refreshes when nothing was logged */
#define ATTACH_FRAMES    30   /* a trainer ID must read the same this long before we switch runs */

void nuz_session_init(NuzSession* s, const GameProfile* p, const char* rom_path, const char* fixed_path) {
    memset(s, 0, sizeof(*s));
    s->profile = p;
    game_strcpy(s->rom_path, sizeof(s->rom_path), rom_path ? rom_path : "");
    game_strcpy(s->fixed_path, sizeof(s->fixed_path), fixed_path ? fixed_path : "");
    nuz_run_init(&s->run);
    s->view.state = NUZ_VIEW_OFF;
    game_strcpy(s->view.reason, sizeof(s->view.reason),
                p ? "Waiting for save data" : "Game not supported");
}

bool nuz_session_request_start(NuzSession* s) {
    if (!s->profile || !s->attached || s->run.active) return false;
    s->start_requested = true;
    return true;
}

/* ---- File ---- */

static void append_events(NuzSession* s, const NuzEvent* ev, uint32_t n) {
    if (!s->path_ok || s->write_failed || n == 0) return;
    FILE* f = fopen(s->path, "a");
    bool ok = f != NULL;
    if (ok && !s->file_has_header) ok = fprintf(f, NUZ_HEADER "\n") >= 0;
    for (uint32_t i = 0; ok && i < n; i++) {
        char line[256];
        ok = nuz_format(&ev[i], line, sizeof(line)) > 0 && fprintf(f, "%s\n", line) >= 0;
    }
    if (f && fclose(f) != 0) ok = false;
    if (ok) {
        s->file_has_header = true;
    } else {
        s->write_failed = true;
        LOG_WARN("Nuzlocke: can't write %s; this run is kept in memory only", s->path);
    }
}

static void load_file(NuzSession* s) {
    FILE* f = fopen(s->path, "r");
    char line[512];
    uint32_t bad = 0;
    if (!f) return;
    if (!fgets(line, sizeof(line), f) || strncmp(line, NUZ_HEADER, strlen(NUZ_HEADER)) != 0) {
        LOG_WARN("Nuzlocke: %s is not a run file; leaving it alone", s->path);
        s->path_ok = false;
        fclose(f);
        return;
    }
    s->file_has_header = true;
    while (fgets(line, sizeof(line), f)) {
        NuzEvent ev;
        if (nuz_parse(line, &ev)) nuz_apply(&s->run, &ev);
        else bad++;
    }
    fclose(f);
    if (bad) LOG_WARN("Nuzlocke: skipped %u unreadable line(s) in %s", (unsigned)bad, s->path);
}

/* A different save showed up (or the first one): switch to its run. */
static void attach(NuzSession* s, uint32_t tid) {
    nuz_run_init(&s->run);
    s->attached = true;
    s->tid = tid;
    s->file_has_header = false;
    s->write_failed = false;
    s->start_requested = false;
    int n = s->fixed_path[0]
                ? snprintf(s->path, sizeof(s->path), "%s", s->fixed_path)
                : snprintf(s->path, sizeof(s->path), "%s.%08X.nuzlocke", s->rom_path, (unsigned)tid);
    s->path_ok = s->rom_path[0] || s->fixed_path[0] ? n > 0 && (size_t)n < sizeof(s->path) : false;
    if (s->path_ok) load_file(s);
    if (s->run.active && s->run.tid != tid) {
        /* A fixed headless file from another save: don't mix runs. */
        nuz_run_init(&s->run);
        s->path_ok = false;
    }
    nuz_tracker_reset(&s->tracker, &s->run);
}

/* ---- View ---- */

static void mon_name(const uint8_t nick[10], char* out) {
    gen3_decode(nick, 10, out, GAME_NAME_LEN);
}

static const NuzMon* live_mon(const NuzFacts* f, NuzKey key, uint8_t* where) {
    for (uint8_t i = 0; i < f->party_count; i++)
        if (f->party[i].species && nuz_key_eq(f->party[i].key, key)) {
            *where = NUZ_WHERE_PARTY;
            return &f->party[i];
        }
    for (uint16_t i = 0; i < f->box_count; i++)
        if (nuz_key_eq(f->boxes[i].key, key)) {
            *where = NUZ_WHERE_PC;
            return &f->boxes[i];
        }
    *where = NUZ_WHERE_AWAY;
    return NULL;
}

static uint8_t route_pill(const NuzRun* run, const NuzRoute* r) {
    bool dead = r->member >= 0 && run->members[r->member].dead;
    switch (r->result) {
    case NUZ_RES_PENDING: return NUZ_PILL_PENDING;
    case NUZ_RES_CAUGHT:  return dead ? NUZ_PILL_DEAD : NUZ_PILL_CAUGHT;
    case NUZ_RES_OWNED:   return dead ? NUZ_PILL_DEAD : NUZ_PILL_OWNED;
    default:              return NUZ_PILL_MISSED;
    }
}

static void add_team(NuzView* v, const NuzSession* s, int idx, uint8_t want, const GameMem* m) {
    const NuzMember* mb = &s->run.members[idx];
    uint8_t where;
    const NuzMon* live = live_mon(&s->facts, mb->key, &where);
    if (mb->dead) where = NUZ_WHERE_DEAD;
    if (where != want) return;
    v->team_total++;
    if (v->team_count >= NUZ_VIEW_TEAM) return;
    NuzViewMon* t = &v->team[v->team_count++];
    mon_name(live ? live->nick : mb->nick, t->nick);
    game_species_name(m, s->profile, live ? live->species : mb->species, t->species);
    t->level = live ? live->level : (mb->dead ? mb->death_level : 0);
    t->where = where;
}

static void build_view(NuzSession* s, const GameMem* m, NuzView* v) {
    const NuzRun* run = &s->run;
    const GameProfile* p = s->profile;
    memset(v, 0, sizeof(*v));
    v->here_row = -1;
    if (!s->attached) {
        v->state = NUZ_VIEW_OFF;
        game_strcpy(v->reason, sizeof(v->reason), "Waiting for save data");
        return;
    }
    if (!run->active) {
        v->state = NUZ_VIEW_NO_RUN;
        game_strcpy(v->reason, sizeof(v->reason), "No Nuzlocke run for this save");
        return;
    }
    v->state = NUZ_VIEW_RUN;
    v->reloads = run->reloads;
    v->deadused = run->deadused;
    v->rerolls = run->rerolls;
    v->wiped = run->wiped;
    v->reloaded_after_death = run->reloaded_after_death;
    v->write_failed = s->write_failed || !s->path_ok;
    nuz_format_time(run->start_pt, v->start_time, sizeof(v->start_time));
    for (int i = 0; i < run->member_count; i++) {
        if (run->members[i].dead) v->dead++;
        else v->alive++;
    }

    uint8_t here = s->facts.mapsec;
    for (int sec = 0; sec < p->region_map_count && sec < NUZ_ROUTES; sec++) {
        const NuzRoute* r = &run->routes[sec];
        bool is_here = sec == here;
        if (r->used) v->routes_used++;
        if (r->rerolled && !v->reroll_place[0]) game_mapsec_name(m, p, (uint8_t)sec, v->reroll_place);
        if (!r->used && r->gift < 0 && !is_here) continue;
        if (v->route_count >= NUZ_VIEW_ROUTES) continue;
        NuzViewRoute* row = &v->routes[v->route_count];
        game_mapsec_name(m, p, (uint8_t)sec, row->place);
        if (!row->place[0]) continue;  /* nameless sections (the ferry) */
        row->here = is_here;
        row->reroll = r->rerolled;
        if (r->used) {
            game_species_name(m, p, r->species, row->who);
            row->pill = route_pill(run, r);
        } else if (r->gift >= 0) {
            const NuzMember* g = &run->members[r->gift];
            game_species_name(m, p, g->species, row->who);
            row->pill = g->dead ? NUZ_PILL_DEAD : NUZ_PILL_GIFT;
        } else {
            row->pill = NUZ_PILL_HERE;
        }
        if (is_here) v->here_row = (int16_t)v->route_count;
        v->route_count++;
    }

    static const uint8_t order[] = { NUZ_WHERE_PARTY, NUZ_WHERE_PC, NUZ_WHERE_AWAY, NUZ_WHERE_DEAD };
    for (size_t o = 0; o < sizeof(order); o++) {
        if (order[o] == NUZ_WHERE_PARTY) {
            /* party in party order */
            for (uint8_t i = 0; i < s->facts.party_count; i++) {
                int idx = s->facts.party[i].species ? nuz_find_member(run, s->facts.party[i].key) : -1;
                if (idx >= 0) add_team(v, s, idx, NUZ_WHERE_PARTY, m);
            }
        } else {
            for (int i = 0; i < run->member_count; i++) add_team(v, s, i, order[o], m);
        }
    }

    /* Graveyard, newest first. */
    bool taken[NUZ_MEMBERS_MAX] = { false };
    for (;;) {
        int best = -1;
        for (int i = 0; i < run->member_count; i++)
            if (run->members[i].dead && !taken[i] &&
                (best < 0 || run->members[i].death_pt > run->members[best].death_pt))
                best = i;
        if (best < 0) break;
        taken[best] = true;
        v->grave_total++;
        if (v->grave_count >= NUZ_VIEW_GRAVES) continue;
        const NuzMember* mb = &run->members[best];
        NuzViewGrave* g = &v->graves[v->grave_count++];
        mon_name(mb->nick, g->nick);
        game_species_name(m, p, mb->species, g->species);
        g->level = mb->death_level;
        if (mb->sec != NUZ_SEC_NONE) game_mapsec_name(m, p, mb->sec, g->caught_at);
        if (mb->death_sec != NUZ_SEC_NONE) game_mapsec_name(m, p, mb->death_sec, g->died_at);
        if (mb->foe) game_species_name(m, p, mb->foe, g->foe);
        nuz_format_time(mb->death_pt, g->time, sizeof(g->time));
        g->reloaded_after = mb->reloaded_after;
        g->used_dead = mb->used_dead;
    }
}

static void refresh_view(NuzSession* s, const GameMem* m) {
    static NuzView next;  /* large; keep off the stack */
    build_view(s, m, &next);
    if (memcmp(&next, &s->view, sizeof(next)) != 0) {
        s->view = next;
        s->view_version++;
    }
    s->view_run_version = s->run.version;
    s->view_mapsec = s->facts.mapsec;
}

/* ---- Per frame ---- */

void nuz_session_frame(NuzSession* s, const GameMem* m) {
    if (!s->profile) return;
    if (!s->families_ok) s->families_ok = nuz_families(s->profile, m, s->family);

    bool active = s->attached && s->run.active;
    bool starting = !active && (s->start_requested || s->fixed_path[0]);
    /* Boxes are read on a fixed cadence even without a Nuzlocke run: the run log needs them. */
    bool boxes = starting || s->frame % BOX_EVERY == 0;
    s->frame++;
    if (!nuz_read(s->profile, m, boxes, &s->facts)) {
        if (s->attached) return;  /* keep showing the run while the save blocks move */
        if (s->view.state != NUZ_VIEW_OFF) refresh_view(s, m);
        return;
    }
    if (s->attached && s->facts.trainer_id == s->tid) s->pending_count = 0;
    if (!s->attached || s->facts.trainer_id != s->tid) {
        /* Ignore a misread (save blocks mid-move) or the blank ID of a game that hasn't started. */
        if (s->facts.trainer_id == 0) return;
        if (s->facts.trainer_id != s->pending_tid) {
            s->pending_tid = s->facts.trainer_id;
            s->pending_count = 0;
        }
        if (++s->pending_count < ATTACH_FRAMES) return;
        attach(s, s->facts.trainer_id);
        active = s->run.active;
        starting = !active && (s->start_requested || s->fixed_path[0]);
        /* has_boxes is per read (nuz_read clears it), so the back-fill always sees boxes read
         * this frame, never the list kept from the last cadence read. */
        if (starting && !s->facts.has_boxes) nuz_read(s->profile, m, true, &s->facts);
    }

    if (starting && s->families_ok) {
        static NuzEvent ev[NUZ_BOX_MAX + NUZ_PARTY_MAX + 1];
        uint16_t n = nuz_backfill(&s->facts, s->profile->region_map_count, ev,
                                  (uint16_t)(sizeof(ev) / sizeof(ev[0])));
        for (uint16_t i = 0; i < n; i++) nuz_apply(&s->run, &ev[i]);
        append_events(s, ev, n);
        nuz_tracker_reset(&s->tracker, &s->run);
        s->start_requested = false;
    } else if (active && s->families_ok) {
        NuzEvent ev[NUZ_EVENTS_MAX];
        uint8_t n = nuz_track_step(&s->tracker, &s->run, &s->facts, s->family,
                                   s->profile->region_map_count, ev);
        for (uint8_t i = 0; i < n; i++) nuz_apply(&s->run, &ev[i]);
        append_events(s, ev, n);
    }

    if (s->run.version != s->view_run_version || s->facts.mapsec != s->view_mapsec ||
        s->frame % VIEW_EVERY == 0 || s->view.state == NUZ_VIEW_OFF)
        refresh_view(s, m);
}

/* ---- Dump (headless --game-dump) ---- */

bool nuz_session_dump(const NuzSession* s, FILE* out) {
    const NuzView* v = &s->view;
    static const char* const pills[] = { "here", "battle", "caught", "missed", "dead", "owned", "gift" };
    static const char* const wheres[] = { "party", "pc", "away", "dead" };
    const NuzFacts* f = &s->facts;
    int ok = 1;
    ok &= fprintf(out, "nuz facts tid %08X pt %u sec %u battle %d flags %08X outcome %u enemy %u lv%u "
                       "active %d party %u boxes %u\n",
                  (unsigned)f->trainer_id, (unsigned)f->play_time, f->mapsec, f->in_battle ? 1 : 0,
                  (unsigned)f->battle_flags, f->battle_outcome, f->enemy_species, f->enemy_level,
                  f->active_party, f->party_count, f->box_count) >= 0;
    if (v->state != NUZ_VIEW_RUN) return ok && fprintf(out, "nuz off: %s\n", v->reason) >= 0;
    ok &= fprintf(out, "nuz run tid %08X start %s alive %u dead %u routes %u reloads %u deadused %u "
                       "rerolls %u%s%s\n",
                  (unsigned)s->run.tid, v->start_time, v->alive, v->dead, v->routes_used, v->reloads,
                  v->deadused, v->rerolls, v->wiped ? " wiped" : "",
                  v->reloaded_after_death ? " reloaded-after-death" : "") >= 0;
    for (uint8_t i = 0; i < v->route_count; i++)
        ok &= fprintf(out, "nuz route %s | %s | %s%s%s\n", v->routes[i].place, v->routes[i].who,
                      pills[v->routes[i].pill], v->routes[i].here ? " here" : "",
                      v->routes[i].reroll ? " reroll" : "") >= 0;
    for (uint8_t i = 0; i < v->team_count; i++)
        ok &= fprintf(out, "nuz mon %s (%s) lv%u %s\n", v->team[i].nick, v->team[i].species,
                      v->team[i].level, wheres[v->team[i].where]) >= 0;
    for (uint8_t i = 0; i < v->grave_count; i++)
        ok &= fprintf(out, "nuz grave %s (%s) lv%u caught %s died %s foe %s at %s%s\n", v->graves[i].nick,
                      v->graves[i].species, v->graves[i].level, v->graves[i].caught_at,
                      v->graves[i].died_at, v->graves[i].foe, v->graves[i].time,
                      v->graves[i].reloaded_after ? " reloaded-after" : "") >= 0;
    return ok != 0;
}
