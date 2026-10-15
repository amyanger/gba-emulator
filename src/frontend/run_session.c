#include "frontend/run_session.h"
#include "game/emerald_layout.h"
#include "common.h"
#include "frontend/game_panel.h"   /* game_panel_title_case */
#include "frontend/session_log.h"
#include "game/gen3_text.h"
#include <stdarg.h>
#include <string.h>

#define RUN_HEADER     "run 1"
#define VIEW_EVERY     30   /* frames between view refreshes when nothing was logged */
#define CLOSE_CALL_MAX 100  /* closest-call card only at or below 10% HP */
#define DOT            "\xC2\xB7"   /* "·": untimed */
#define MDOT           " \xC2\xB7 " /* " · " separator */

/* struct Trainer (include/data.h): u8 trainerName[12] at +4, 0x28 bytes each. */
#define TRAINER_NAME     4
#define TRAINER_NAME_LEN 12
/* include/constants/opponents.h: TRAINER_ROXANNE_1 265 .. TRAINER_JUAN_1 272, in badge order. */
#define TRAINER_GYM_FIRST 265
/* include/constants/species.h */
#define SPECIES_KYOGRE  404
#define SPECIES_GROUDON 405
static const uint16_t k_starters[3] = { 277, 280, 283 };   /* SPECIES_TREECKO, TORCHIC, MUDKIP */
/* include/constants/items.h: ITEM_MASTER_BALL 1 .. ITEM_PREMIER_BALL 12 */
#define BALL_LAST 12

static const char* const k_badge_names[RUN_BADGES] = {
    "Stone", "Knuckle", "Dynamo", "Heat", "Balance", "Feather", "Mind", "Rain",
};

void run_session_init(RunSession* s, const GameProfile* p, const char* rom_path, const char* fixed_path) {
    memset(s, 0, sizeof(*s));
    s->profile = p;
    game_strcpy(s->rom_path, sizeof(s->rom_path), rom_path ? rom_path : "");
    game_strcpy(s->fixed_path, sizeof(s->fixed_path), fixed_path ? fixed_path : "");
    run_log_init(&s->log);
    run_tracker_reset(&s->tracker);
    s->view.state = p ? RUN_VIEW_WAITING : RUN_VIEW_OFF;
    game_strcpy(s->view.reason, sizeof(s->view.reason), p ? "Waiting for save data" : "Game not supported");
}

/* ---- File ---- */

static void append_events(RunSession* s, const RunEvent* ev, uint32_t n) {
    if (!s->path_ok || s->write_failed || n == 0) return;
    FILE* f = fopen(s->path, "a");
    bool ok = f != NULL;
    if (ok && !s->file_has_header) ok = fprintf(f, RUN_HEADER "\n") >= 0;
    for (uint32_t i = 0; ok && i < n; i++) {
        char line[RUN_LINE_MAX];
        ok = run_format(&ev[i], line, sizeof(line)) > 0 && fprintf(f, "%s\n", line) >= 0;
    }
    if (f && fclose(f) != 0) ok = false;
    if (ok) {
        s->file_has_header = true;
    } else {
        /* Sticky: a gap in the file would replay into a different log. */
        s->write_failed = true;
        LOG_WARN("Run log: can't write %s; this run is kept in memory only", s->path);
    }
}

/* The in-memory log is the truth: apply everything, then write exactly that. */
static void emit(RunSession* s, const RunEvent* ev, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) run_apply(&s->log, &ev[i]);
    append_events(s, ev, n);
}

static void load_file(RunSession* s) {
    static char line[RUN_LINE_MAX + 2];
    SessionLogReader r;
    SessionLogOpen open = session_log_open(&r, s->path, RUN_HEADER);
    if (open == SESSION_LOG_FOREIGN) {
        LOG_WARN("Run log: %s is not a run file; leaving it alone", s->path);
        s->path_ok = false;
    }
    if (open != SESSION_LOG_OK) return;
    s->file_has_header = true;
    while (session_log_next(&r, line, sizeof(line))) {
        RunEvent ev;
        if (run_parse(line, &ev)) run_apply(&s->log, &ev);
        else r.bad++;
    }
    if (!session_log_close(&r, s->path)) s->write_failed = true;
    if (r.bad) LOG_WARN("Run log: skipped %u unreadable line(s) in %s", (unsigned)r.bad, s->path);
}

/* A save showed up (the first one, or a different trainer): switch to its log. */
static void attach(RunSession* s, uint32_t tid, uint32_t pt) {
    run_log_init(&s->log);
    s->attached = true;
    s->tid = tid;
    s->file_has_header = false;
    s->write_failed = false;
    /* path[] fits either path at its longest (1023 + 13), so the length test is only a guard;
     * a name the filesystem rejects shows up as write_failed on the first append. */
    s->path_ok = session_log_path(s->path, sizeof(s->path), s->rom_path, s->fixed_path, tid, "run");
    if (s->path_ok) load_file(s);
    if (s->log.active && s->log.tid != tid) {
        /* A fixed headless file from another save: don't mix runs. */
        run_log_init(&s->log);
        s->path_ok = false;
    }
    /* Replay first, then track: the tracker's first step takes its reload reference from the log. */
    run_tracker_reset(&s->tracker);
    if (!s->log.active) {
        RunEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = RUN_EV_START;
        ev.pt = pt;
        ev.tid = tid;
        ev.sec = NUZ_SEC_NONE;
        ev.low = 1000;
        emit(s, &ev, 1);
    }
}

/* ---- Text helpers ---- */

/* snprintf that never leaves half a UTF-8 sequence at a cut. */
static void textf(char* out, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, size, fmt, ap);
    va_end(ap);
    if (r < 0) {
        out[0] = '\0';
    } else if ((size_t)r >= size) {
        size_t n = strlen(out);
        while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) n--;
        if (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0xC0) n--;
        out[n] = '\0';
    }
}

/* ROM text -> display case ("TATE&LIZA" -> "Tate&Liza"). */
static void nice(const char* raw, char* out, size_t size) {
    game_panel_title_case(raw, out, size);
    for (size_t i = 0; out[i] && out[i + 1]; i++)
        if (out[i] == '&' && out[i + 1] >= 'a' && out[i + 1] <= 'z') out[i + 1] = (char)(out[i + 1] - 32);
}

static void hm(uint32_t frames, char* out, size_t size) {
    char buf[24];
    nuz_format_time(frames, buf, sizeof(buf));
    size_t n = strlen(buf);
    if (n >= 3) buf[n - 3] = '\0';   /* drop ":ss" */
    game_strcpy(out, size, buf);
}

static void species_name(const RunSession* s, const GameMem* m, uint16_t sp, char out[GAME_NAME_LEN]) {
    char raw[GAME_NAME_LEN];
    game_species_name(m, s->profile, sp, raw);
    nice(raw, out, GAME_NAME_LEN);
}

static void place_name(const RunSession* s, const GameMem* m, uint8_t sec, char out[GAME_MAP_NAME_LEN]) {
    char raw[GAME_MAP_NAME_LEN];
    out[0] = '\0';
    if (sec == NUZ_SEC_NONE) return;
    game_mapsec_name(m, s->profile, sec, raw);
    nice(raw, out, GAME_MAP_NAME_LEN);
}

static void trainer_name(const RunSession* s, const GameMem* m, uint16_t id, char out[GAME_NAME_LEN]) {
    uint8_t raw[TRAINER_NAME_LEN];
    char txt[GAME_NAME_LEN];
    const GameProfile* p = s->profile;
    if (id >= p->trainer_count ||
        !game_mem_copy(m, p->trainers + (uint32_t)id * TRAINER_SIZE + TRAINER_NAME, raw, sizeof(raw))) {
        game_strcpy(out, GAME_NAME_LEN, "???");
        return;
    }
    gen3_decode(raw, sizeof(raw), txt, sizeof(txt));
    nice(txt, out, GAME_NAME_LEN);
}

/* Nickname for display; species name when it is blank. */
static void mon_name(const RunSession* s, const GameMem* m, const uint8_t nick[10], uint16_t species,
                     char out[GAME_NAME_LEN]) {
    char raw[GAME_NAME_LEN];
    gen3_decode(nick, 10, raw, sizeof(raw));
    size_t n = strlen(raw);
    while (n > 0 && raw[n - 1] == ' ') raw[--n] = '\0';
    if (n == 0) species_name(s, m, species, out);
    else nice(raw, out, GAME_NAME_LEN);
}

static void member_name(const RunSession* s, const GameMem* m, NuzKey key, char out[GAME_NAME_LEN]) {
    int i = run_find_member(&s->log, key);
    if (i < 0) game_strcpy(out, GAME_NAME_LEN, "???");
    else mon_name(s, m, s->log.members[i].nick, s->log.members[i].species, out);
}

/* "Poke Ball" -> "Poké": the ROM's item name without the trailing " Ball". */
static void ball_name(const RunSession* s, const GameMem* m, uint8_t ball, char out[GAME_NAME_LEN]) {
    char raw[GAME_NAME_LEN];
    game_item_name(m, s->profile, ball, raw);
    nice(raw, out, GAME_NAME_LEN);
    size_t n = strlen(out);
    if (n > 5 && strcmp(out + n - 5, " Ball") == 0) out[n - 5] = '\0';
}

static const char* plural(uint32_t n) { return n == 1 ? "" : "s"; }

/* ---- View ---- */

/* R3: an untimed badge or story beat survives a reload, so it is shown only while RAM has it. */
static bool feedable(const RunEvent* e, const RunFacts* rf) {
    switch (e->type) {
    case RUN_EV_JOIN:   return e->how != RUN_HOW_BACKFILL;
    case RUN_EV_BATTLE: return e->has_ace && run_is_leader(e->trainer); /* also older files */
    case RUN_EV_BADGE:  return e->id >= 1 && e->id <= RUN_BADGES && (rf->badges & (1u << (e->id - 1)));
    case RUN_EV_STORY:  return e->id < RUN_STORY_COUNT && (rf->story & (1u << e->id));
    case RUN_EV_EVOLVE: case RUN_EV_FAINT: case RUN_EV_WHITEOUT:
        return true;
    default:            return false;
    }
}

static bool newer(const RunEvent* a, const RunEvent* b) {
    return a->pt > b->pt || (a->pt == b->pt && a->seq > b->seq);
}

/* Keeps pick[] (newest first) the RUN_VIEW_FEED newest feed events. */
static void feed_offer(const RunEvent** pick, uint8_t* count, const RunEvent* e, const RunFacts* rf) {
    if (!feedable(e, rf)) return;
    uint8_t n = *count;
    if (n == RUN_VIEW_FEED) {
        if (!newer(e, pick[n - 1])) return;
        n--;
    }
    uint8_t i = n;
    while (i > 0 && newer(e, pick[i - 1])) {
        pick[i] = pick[i - 1];
        i--;
    }
    pick[i] = e;
    *count = (uint8_t)(n + 1);
}

static void story_text(const RunSession* s, const GameMem* m, uint8_t id, char* out, size_t size) {
    char name[GAME_NAME_LEN];
    switch (id) {
    case RUN_STORY_STARTER:
        if (s->rf.starter < 3) {
            species_name(s, m, k_starters[s->rf.starter], name);
            textf(out, size, "Chose %s", name);
        } else {
            textf(out, size, "Chose a starter");
        }
        break;
    case RUN_STORY_POKEDEX:    textf(out, size, "Got the Pok\xC3\xA9" "dex"); break;
    case RUN_STORY_CHIMNEY:    textf(out, size, "Stopped Team Magma at Mt. Chimney"); break;
    case RUN_STORY_WEATHER:    textf(out, size, "Saved the Weather Institute"); break;
    case RUN_STORY_SURF:       textf(out, size, "Got Surf"); break;
    case RUN_STORY_GROUDON:
        species_name(s, m, SPECIES_GROUDON, name);
        textf(out, size, "%s awakened", name);
        break;
    case RUN_STORY_KYOGRE:
        species_name(s, m, SPECIES_KYOGRE, name);
        textf(out, size, "%s escaped", name);
        break;
    case RUN_STORY_SOOTOPOLIS: textf(out, size, "Calmed Sootopolis"); break;
    case RUN_STORY_E4:         textf(out, size, "Entered the Elite Four"); break;
    case RUN_STORY_CHAMPION:
        trainer_name(s, m, TRAINER_WALLACE, name);
        textf(out, size, "Beat Champion %s", name);
        break;
    case RUN_STORY_HOF:        textf(out, size, "Entered the Hall of Fame"); break;
    default:                   textf(out, size, "Got the National Dex"); break;
    }
}

static void feed_row(const RunSession* s, const GameMem* m, const RunEvent* e, RunViewFeed* row) {
    char a[GAME_NAME_LEN], b[GAME_NAME_LEN], c[GAME_NAME_LEN];
    if (e->untimed) game_strcpy(row->time, sizeof(row->time), DOT);
    else hm(e->pt, row->time, sizeof(row->time));
    switch (e->type) {
    case RUN_EV_BADGE:
        row->icon = RUN_ICON_BADGE;
        if (e->id >= 1 && e->id <= RUN_BADGES) {
            trainer_name(s, m, (uint16_t)(TRAINER_GYM_FIRST + e->id - 1), a);
            textf(row->text, sizeof(row->text), "%s Badge, beat %s", k_badge_names[e->id - 1], a);
        }
        break;
    case RUN_EV_STORY:
        row->icon = RUN_ICON_STORY;
        story_text(s, m, e->id, row->text, sizeof(row->text));
        break;
    case RUN_EV_JOIN:
        species_name(s, m, e->species, a);
        place_name(s, m, e->sec, row->where);
        if (e->how == RUN_HOW_CAUGHT) {
            row->icon = RUN_ICON_CATCH;
            textf(row->text, sizeof(row->text), e->shiny ? "Caught a shiny %s" : "Caught %s", a);
        } else {
            row->icon = RUN_ICON_GIFT;
            if (e->how == RUN_HOW_HATCHED) textf(row->text, sizeof(row->text), "%s hatched", a);
            else if (e->how == RUN_HOW_TRADED) textf(row->text, sizeof(row->text), "Traded for %s", a);
            else textf(row->text, sizeof(row->text), "Got %s", a);
        }
        break;
    case RUN_EV_EVOLVE:
        row->icon = RUN_ICON_EVOLVE;
        member_name(s, m, e->key, a);
        species_name(s, m, e->species, b);
        textf(row->text, sizeof(row->text), "%s evolved into %s", a, b);
        break;
    case RUN_EV_FAINT:
        row->icon = RUN_ICON_FAINT;
        member_name(s, m, e->key, a);
        place_name(s, m, e->sec, row->where);
        if (e->foe) {
            species_name(s, m, e->foe, b);
            textf(row->text, sizeof(row->text), "%s fainted to %s", a, b);
        } else {
            textf(row->text, sizeof(row->text), "%s fainted", a);
        }
        break;
    case RUN_EV_WHITEOUT:
        row->icon = RUN_ICON_WHITEOUT;
        place_name(s, m, e->sec, row->where);
        textf(row->text, sizeof(row->text), "Whited out");
        break;
    default: /* BATTLE with an ace */
        row->icon = RUN_ICON_ACE;
        trainer_name(s, m, e->trainer, a);
        species_name(s, m, e->ace_species, b);
        member_name(s, m, e->ace_by, c);
        textf(row->text, sizeof(row->text), "%s's %s KO'd by %s", a, b, c);
        break;
    }
}

static const NuzMon* find_live(const NuzMon* mons, uint16_t count, NuzKey key) {
    for (uint16_t i = 0; i < count; i++)
        if (mons[i].species && nuz_key_eq(mons[i].key, key)) return &mons[i];
    return NULL;
}

static void add_mon(const RunSession* s, const GameMem* m, RunView* v, int idx, const NuzMon* live,
                    bool in_party, int mvp) {
    v->team_total++;
    if (v->team_count >= RUN_VIEW_TEAM) return;
    const RunMember* mb = &s->log.members[idx];
    RunViewMon* t = &v->team[v->team_count++];
    char sp[GAME_NAME_LEN], when[12];
    if (live) mon_name(s, m, live->nick, live->species, t->name);
    else mon_name(s, m, mb->nick, mb->species, t->name);
    species_name(s, m, live ? live->species : mb->species, t->species);
    if (mb->live) hm(mb->join_pt, t->joined, sizeof(t->joined));
    else game_strcpy(t->joined, sizeof(t->joined), DOT);
    place_name(s, m, mb->sec, t->where);
    t->lv_from = mb->join_level;
    t->lv_to = live && live->level ? live->level : mb->level;
    t->kos = mb->kos;
    t->faints = mb->faints;
    if (mb->evo_species) {
        species_name(s, m, mb->evo_species, sp);
        hm(mb->evo_pt, when, sizeof(when));
        textf(t->evolved, sizeof(t->evolved), "%s %s", sp, when);
    }
    t->mvp = idx == mvp;
    t->in_party = in_party;
    t->away = live == NULL;
}

static void build_team(const RunSession* s, const GameMem* m, const NuzFacts* nf, RunView* v, int mvp) {
    static bool placed[RUN_MEMBERS_MAX];
    const RunLog* log = &s->log;
    memset(placed, 0, sizeof(placed));
    for (uint8_t i = 0; i < nf->party_count && i < NUZ_PARTY_MAX; i++) {
        int idx = nf->party[i].species ? run_find_member(log, nf->party[i].key) : -1;
        if (idx < 0 || placed[idx]) continue;
        placed[idx] = true;
        add_mon(s, m, v, idx, &nf->party[i], true, mvp);
    }
    for (int i = 0; i < log->member_count; i++) {
        const NuzMon* live = placed[i] ? NULL : find_live(nf->boxes, nf->box_count, log->members[i].key);
        if (!live) continue;
        placed[i] = true;
        add_mon(s, m, v, i, live, false, mvp);
    }
    for (int i = 0; i < log->member_count; i++)
        if (!placed[i]) add_mon(s, m, v, i, NULL, false, mvp);
}

static int pick_mvp(const RunLog* log) {
    int best = -1;
    for (int i = 0; i < log->member_count; i++) {
        const RunMember* a = &log->members[i];
        if (a->kos == 0) continue;
        if (best < 0) {
            best = i;
            continue;
        }
        const RunMember* b = &log->members[best];
        int ga = (int)a->level - (int)a->join_level, gb = (int)b->level - (int)b->join_level;
        if (a->kos > b->kos || (a->kos == b->kos && ga > gb)) best = i;
    }
    return best;
}

bool run_session_mvp_key(const RunSession* s, NuzKey* out) {
    if (s->view.state != RUN_VIEW_RUN) return false;
    int i = pick_mvp(&s->log);
    if (i < 0) return false;
    *out = s->log.members[i].key;
    return true;
}

typedef struct { uint8_t stat; const char* pre; const char* one; const char* many; } FunFact;
static const FunFact k_fun[] = {
    { RUN_STAT_LEDGES,        "Jumped ",                 " ledge",  " ledges" },
    { RUN_STAT_RAINED_ON,     "Got rained on ",          " time",   " times" },
    { RUN_STAT_SPLASH,        "Used Splash ",            " time",   " times" },
    { RUN_STAT_STRUGGLE,      "Used Struggle ",          " time",   " times" },
    { RUN_STAT_CHECKED_CLOCK, "Checked the clock ",      " time",   " times" },
    { RUN_STAT_WATCHED_TV,    "Watched TV ",             " time",   " times" },
    { RUN_STAT_CHECKED_DEX,   "Checked the Pok\xC3\xA9" "dex ", " time", " times" },
    { RUN_STAT_POKECENTER,    "Healed at a Center ",     " time",   " times" },
    { RUN_STAT_SLOT_JACKPOTS, "Hit ",                    " slot jackpot", " slot jackpots" },
    { RUN_STAT_HATCHED,       "Hatched ",                " egg",    " eggs" },
};
#define FUN_COUNT (sizeof(k_fun) / sizeof(k_fun[0]))

static void build_cards(const RunSession* s, const GameMem* m, const NuzFacts* nf, RunView* v, int mvp) {
    const RunLog* log = &s->log;
    char name[GAME_NAME_LEN], foe[GAME_NAME_LEN];
    if (mvp >= 0) {
        const RunMember* mb = &log->members[mvp];
        mon_name(s, m, mb->nick, mb->species, name);
        textf(v->mvp, sizeof(v->mvp), "%s" MDOT "%u KO%s%s", name, (unsigned)mb->kos, plural(mb->kos),
              mb->live && mb->faints == 0 ? ", never fainted" : "");
    }
    if (log->low_permille <= CLOSE_CALL_MAX) {
        char pct[12];
        member_name(s, m, log->low_key, name);
        if (log->low_permille < 10) game_strcpy(pct, sizeof(pct), "under 1%");
        else textf(pct, sizeof(pct), "%u%%", (unsigned)((log->low_permille + 5) / 10));
        if (log->low_foe) {
            species_name(s, m, log->low_foe, foe);
            textf(v->closest, sizeof(v->closest), "%s held on with %s HP vs %s", name, pct, foe);
        } else {
            textf(v->closest, sizeof(v->closest), "%s held on with %s HP", name, pct);
        }
    }
    uint8_t live[FUN_COUNT];
    uint32_t n = 0;
    for (uint32_t i = 0; i < FUN_COUNT; i++)
        if (s->rf.stats[k_fun[i].stat]) live[n++] = (uint8_t)i;
    if (n) {
        const FunFact* f = &k_fun[live[(nf->play_time / 7200u) % n]];   /* changes every 2 minutes */
        uint32_t val = s->rf.stats[f->stat];
        textf(v->fun, sizeof(v->fun), "%s%u%s", f->pre, (unsigned)val, val == 1 ? f->one : f->many);
    }
}

static void build_footer(const RunSession* s, const GameMem* m, RunView* v) {
    const RunLog* log = &s->log;
    char name[GAME_NAME_LEN];
    uint16_t best = 0;
    for (uint16_t sp = 1; sp < NUZ_SPECIES_MAX; sp++)
        if (log->wild_count[sp] > log->wild_count[best]) best = sp;
    if (best && log->wild_count[best]) {
        species_name(s, m, best, name);
        textf(v->most_enc, sizeof(v->most_enc), "%s (%u)", name, (unsigned)log->wild_count[best]);
    }
    uint16_t balls[BALL_LAST + 1] = { 0 };
    for (int i = 0; i < log->member_count; i++)
        if (log->members[i].how == RUN_HOW_CAUGHT && log->members[i].ball >= 1 && log->members[i].ball <= BALL_LAST)
            balls[log->members[i].ball]++;
    size_t len = 0;
    for (;;) {
        uint8_t top = 0;
        for (uint8_t b = 1; b <= BALL_LAST; b++)
            if (balls[b] > balls[top]) top = b;
        if (!top) break;
        char piece[40];
        ball_name(s, m, top, name);
        textf(piece, sizeof(piece), "%s%u %s", len ? MDOT : "", (unsigned)balls[top], name);
        size_t pl = strlen(piece);
        if (len + pl >= sizeof(v->caught_in)) break;
        memcpy(v->caught_in + len, piece, pl + 1);
        len += pl;
        balls[top] = 0;
    }
}

static void build_view(const RunSession* s, const GameMem* m, const NuzFacts* nf, RunView* v) {
    const RunLog* log = &s->log;
    const RunFacts* rf = &s->rf;
    char raw[GAME_NAME_LEN];
    memset(v, 0, sizeof(*v));
    if (!s->attached) {
        v->state = RUN_VIEW_WAITING;
        game_strcpy(v->reason, sizeof(v->reason), "Waiting for save data");
        return;
    }
    v->state = RUN_VIEW_RUN;
    gen3_decode(rf->player_name, sizeof(rf->player_name), raw, sizeof(raw));
    nice(raw, v->player, sizeof(v->player));
    hm(nf->play_time, v->play_time, sizeof(v->play_time));
    v->dex_owned = rf->dex_owned;
    v->dex_seen = rf->dex_seen;

    /* R3: a cell lights from RAM; the log only gives its time. */
    for (uint8_t b = 0; b < RUN_BADGES; b++) {
        game_strcpy(v->badges[b].name, sizeof(v->badges[b].name), k_badge_names[b]);
        if (!(rf->badges & (1u << b))) continue;
        v->badges[b].have = true;
        v->badge_count++;
        if (!log->badge[b] || log->badge_untimed[b]) {
            v->badges[b].untimed = true;
            game_strcpy(v->badges[b].split, sizeof(v->badges[b].split), DOT);
            continue;
        }
        uint32_t pt = log->badge_pt[b];
        bool have_prev = false;
        uint32_t prev = 0;
        for (uint8_t o = 0; o < RUN_BADGES; o++) {
            if (o == b || !(rf->badges & (1u << o)) || !log->badge[o] || log->badge_untimed[o]) continue;
            uint32_t q = log->badge_pt[o];
            if ((q < pt || (q == pt && o < b)) && (!have_prev || q > prev)) {
                prev = q;
                have_prev = true;
            }
        }
        if (!have_prev) {
            hm(pt, v->badges[b].split, sizeof(v->badges[b].split));
        } else {
            char d[12];
            hm(pt - prev, d, sizeof(d));
            textf(v->badges[b].split, sizeof(v->badges[b].split), "+%s", d);
        }
    }

    static const RunEvent* pick[RUN_VIEW_FEED];
    uint8_t picked = 0;
    for (int i = 0; i < log->ev_count; i++) feed_offer(pick, &picked, &log->ev[i], rf);
    for (int i = 0; i < log->bt_count; i++) feed_offer(pick, &picked, &log->bt[i], rf);
    for (uint8_t i = 0; i < picked; i++) feed_row(s, m, pick[i], &v->feed[i]);
    v->feed_count = picked;

    v->caught = rf->stats[RUN_STAT_CAPTURES];
    v->faints = log->faints;
    v->faints_since_tracking = !log->started_new_game;
    v->steps = rf->stats[RUN_STAT_STEPS];
    v->battles = rf->stats[RUN_STAT_TOTAL_BATTLES];
    int mvp = pick_mvp(log);
    build_cards(s, m, nf, v, mvp);
    if (log->started_new_game) {
        game_strcpy(v->tracking_since, sizeof(v->tracking_since), "Tracking since new game");
    } else {
        char t[12];
        hm(log->start_pt, t, sizeof(t));
        textf(v->tracking_since, sizeof(v->tracking_since), "Tracking since %s", t);
    }
    build_team(s, m, nf, v, mvp);

    v->wild = rf->stats[RUN_STAT_WILD_BATTLES];
    v->trainer_battles = rf->stats[RUN_STAT_TRAINER_BATTLES];
    v->whiteouts = log->whiteouts;
    v->upsets = log->upsets;
    v->shiny_seen = log->shinies_seen;
    v->shiny_caught = log->shinies_caught;
    v->eggs = rf->stats[RUN_STAT_HATCHED];
    v->evolutions = rf->stats[RUN_STAT_EVOLVED];
    v->centers = rf->stats[RUN_STAT_POKECENTER];
    v->shops = rf->stats[RUN_STAT_SHOPPED];
    v->fishing = rf->stats[RUN_STAT_FISHING];
    v->ledges = rf->stats[RUN_STAT_LEDGES];
    v->rained = rf->stats[RUN_STAT_RAINED_ON];
    v->clocks = rf->stats[RUN_STAT_CHECKED_CLOCK];
    v->splash = rf->stats[RUN_STAT_SPLASH];
    build_footer(s, m, v);
    v->write_failed = s->write_failed || !s->path_ok;
    v->log_full = log->events_full || log->battles_full;
}

static void refresh_view(RunSession* s, const GameMem* m, const NuzFacts* nf) {
    static RunView next;  /* large; keep off the stack */
    build_view(s, m, nf, &next);
    if (memcmp(&next, &s->view, sizeof(next)) != 0) {
        s->view = next;
        s->view_version++;
    }
    s->view_log_version = s->log.version;
}

/* ---- Per frame ---- */

void run_session_frame(RunSession* s, const GameMem* m, const NuzFacts* nf) {
    if (!s->profile) return;
    s->frame++;
    run_read(s->profile, m, nf->valid ? nf->enemy_species : 0, &s->rf);
    bool valid = nf->valid && s->rf.valid;

    if (nf->valid && session_tid_step(&s->tid_gate, s->attached, s->tid, nf->trainer_id))
        attach(s, nf->trainer_id, nf->play_time);

    /* Invalid frames still go to the tracker: it counts them to tell a save block move from a
     * real gap. A different trainer ID (misread, or a switch still debouncing) is skipped. */
    if (s->attached && (!valid || nf->trainer_id == s->tid)) {
        static RunEvent ev[RUN_EVENTS_FRAME_MAX];
        uint8_t n = run_track_step(&s->tracker, &s->log, nf, &s->rf, ev);
        emit(s, ev, n);
    }

    /* Keep showing the last view while the save blocks move or another trainer ID is debouncing. */
    if (s->attached && valid && nf->trainer_id == s->tid &&
        (s->log.version != s->view_log_version || s->frame % VIEW_EVERY == 0 || s->view.state != RUN_VIEW_RUN))
        refresh_view(s, m, nf);
}

/* ---- Dump (headless --game-dump) ---- */

bool run_session_dump(const RunSession* s, FILE* out) {
    static const char* const icons[] = { "badge", "story", "catch", "gift", "faint", "whiteout", "evolve", "ace" };
    const RunView* v = &s->view;
    int ok = 1;
    if (v->state != RUN_VIEW_RUN) return fprintf(out, "run: off %s\n", v->reason) >= 0;
    ok &= fprintf(out, "run: tid=%08X events=%u battles=%u members=%u badges=%u\n", (unsigned)s->log.tid,
                  (unsigned)s->log.ev_count, (unsigned)s->log.bt_count, (unsigned)s->log.member_count,
                  (unsigned)v->badge_count) >= 0;
    ok &= fprintf(out, "run head player=%s time=%s dex=%u/%u %s%s%s\n", v->player, v->play_time,
                  (unsigned)v->dex_owned, (unsigned)v->dex_seen, v->tracking_since,
                  v->write_failed ? " write-failed" : "", v->log_full ? " log-full" : "") >= 0;
    for (uint8_t b = 0; b < RUN_BADGES; b++)
        ok &= fprintf(out, "run badge %u %s have=%d untimed=%d split=%s\n", (unsigned)(b + 1), v->badges[b].name,
                      v->badges[b].have ? 1 : 0, v->badges[b].untimed ? 1 : 0, v->badges[b].split) >= 0;
    for (uint8_t i = 0; i < v->team_count; i++) {
        const RunViewMon* t = &v->team[i];
        ok &= fprintf(out, "run mon %s (%s) joined=%s at=%s lv%u->%u kos=%u faints=%u evolved=%s%s%s%s\n",
                      t->name, t->species, t->joined, t->where, t->lv_from, t->lv_to, (unsigned)t->kos,
                      (unsigned)t->faints, t->evolved, t->in_party ? " party" : "", t->away ? " away" : "",
                      t->mvp ? " mvp" : "") >= 0;
    }
    for (uint8_t i = 0; i < v->feed_count; i++)
        ok &= fprintf(out, "run feed %s %s %s | %s\n", v->feed[i].time,
                      v->feed[i].icon < sizeof(icons) / sizeof(icons[0]) ? icons[v->feed[i].icon] : "?",
                      v->feed[i].text, v->feed[i].where) >= 0;
    ok &= fprintf(out, "run tiles caught=%u faints=%u%s steps=%u battles=%u\n", (unsigned)v->caught,
                  (unsigned)v->faints, v->faints_since_tracking ? " (since tracking)" : "", (unsigned)v->steps,
                  (unsigned)v->battles) >= 0;
    ok &= fprintf(out, "run cards mvp=[%s] closest=[%s] fun=[%s]\n", v->mvp, v->closest, v->fun) >= 0;
    ok &= fprintf(out, "run counters wild=%u trainers=%u whiteouts=%u upsets=%u shiny=%u/%u eggs=%u evolutions=%u "
                       "centers=%u shops=%u fishing=%u ledges=%u rained=%u clocks=%u splash=%u\n",
                  (unsigned)v->wild, (unsigned)v->trainer_battles, (unsigned)v->whiteouts, (unsigned)v->upsets,
                  (unsigned)v->shiny_seen, (unsigned)v->shiny_caught, (unsigned)v->eggs, (unsigned)v->evolutions,
                  (unsigned)v->centers, (unsigned)v->shops, (unsigned)v->fishing, (unsigned)v->ledges,
                  (unsigned)v->rained, (unsigned)v->clocks, (unsigned)v->splash) >= 0;
    ok &= fprintf(out, "run footer most=[%s] caught_in=[%s]\n", v->most_enc, v->caught_in) >= 0;
    return ok != 0;
}
