#include "frontend/hunt_session.h"
#include "common.h"
#include "frontend/game_panel.h"
#include "frontend/hunt_panel.h"
#include "frontend/session_log.h"
#include "game/pokemon.h"
#include <string.h>

#define HUNT_HEADER "huntlog 1"
#define RETRY_FRAMES 60  /* retry queued writes about once a second */
#define REBUILD_MS 250   /* R11: the view is rebuilt at most 4 times a second */
#define RESET_ARM_MS 3000
#define RATE_MIN_MS 500
#define RATE_SPAN_MS 2000 /* the advance rate averages over about the last 2 s */
#define RATE_DEFAULT 60.0f
#define GATE_FRAMES 4 /* a new prediction state must hold this many frames before it shows */

/* Land encounter slot shares (pokeemerald gWildMonHeaders land rates, ENCOUNTER_CHANCE_*). */
static const uint8_t k_land_percent[WILD_SLOTS] = {20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1};

void hunt_session_init(HuntSession* s, const GameProfile* p, const char* rom_path,
                       const char* fixed_path) {
    memset(s, 0, sizeof(*s));
    s->profile = p;
    game_strcpy(s->rom_path, sizeof(s->rom_path), rom_path ? rom_path : "");
    game_strcpy(s->fixed_path, sizeof(s->fixed_path), fixed_path ? fixed_path : "");
    hunt_init(&s->hunt);
    s->rate = RATE_DEFAULT;
    game_strcpy(s->view.reason, sizeof(s->view.reason),
                p ? "Waiting for save data" : "Game not supported");
}

const HuntView* hunt_session_view(const HuntSession* s) {
    return &s->view;
}

/* ---- File ---- */

static void flush_queue(HuntSession* s) {
    if (!s->queue_n || !s->path_ok)
        return;
    FILE* f = fopen(s->path, "a");
    uint8_t written = 0;
    bool header = false;
    if (f) {
        bool ok = true;
        if (!s->file_has_header) {
            ok = fprintf(f, HUNT_HEADER "\n") >= 0;
            header = ok;
        }
        while (ok && written < s->queue_n) {
            ok = fprintf(f, "%s\n", s->queue[written]) >= 0;
            if (ok)
                written++;
        }
        /* Buffered write errors show up at fclose: nothing counts as written until it succeeds. */
        if (fclose(f) != 0) {
            written = 0;
            header = false;
        }
    }
    if (header)
        s->file_has_header = true;
    if (written) {
        memmove(s->queue[0], s->queue[written], (size_t)(s->queue_n - written) * HUNT_LINE_MAX);
        s->queue_n = (uint8_t)(s->queue_n - written);
    }
    if (s->queue_n && !s->write_warned) {
        s->write_warned = true;
        LOG_WARN("Shiny Hunt: can't write %s; will keep retrying", s->path);
    }
    if (!s->queue_n)
        s->write_warned = false;
}

static void enqueue(HuntSession* s, const char* line) {
    if (!s->attached)
        return;
    if (s->queue_n == HUNT_QUEUE_MAX) {
        memmove(s->queue[0], s->queue[1], (size_t)(HUNT_QUEUE_MAX - 1) * HUNT_LINE_MAX);
        s->queue_n--;
        LOG_WARN("Shiny Hunt: write queue full, dropped the oldest line");
    }
    game_strcpy(s->queue[s->queue_n++], HUNT_LINE_MAX, line);
    flush_queue(s);
}

static void replay_line(HuntSession* s, const char* line) {
    unsigned species, pid, shiny, method, nature;
    switch (line[0]) {
    case 'E':
        if (sscanf(line, "E %u %x %u %u", &species, &pid, &shiny, &method) == 4 && species > 0 &&
            species <= 0xFFFF && shiny <= 1 && method <= HUNT_METHOD_STATIC) {
            HuntEvent ev = {HUNT_EV_ENCOUNTER, (uint16_t)species, pid, shiny != 0, (uint8_t)method};
            hunt_apply(&s->hunt, &ev);
        }
        break;
    case 'R':
        hunt_reset_counts(&s->hunt);
        break;
    case 'T':
        if (sscanf(line, "T %u", &species) == 1 && species <= 0xFFFF)
            s->hunt.target = (uint16_t)species;
        break;
    case 'N':
        if (sscanf(line, "N %u", &nature) == 1 && nature <= HUNT_NATURE_ANY)
            s->hunt.nature = (uint8_t)nature;
        break;
    default:
        break;
    }
}

static void load_file(HuntSession* s) {
    char line[128];
    SessionLogReader r;
    SessionLogOpen open = session_log_open(&r, s->path, HUNT_HEADER);
    if (open == SESSION_LOG_FOREIGN) {
        LOG_WARN("Shiny Hunt: %s is not a hunt file; leaving it alone", s->path);
        s->path_ok = false;
    }
    if (open != SESSION_LOG_OK)
        return;
    s->file_has_header = true;
    while (session_log_next(&r, line, sizeof(line)))
        replay_line(s, line);
    /* If the torn-tail repair can't be written, appends can't either: the retry queue
     * reports that. */
    session_log_close(&r, s->path);
}

static void attach(HuntSession* s, uint32_t tid) {
    flush_queue(s); /* one last try at the old path before leaving it */
    if (s->queue_n)
        LOG_WARN("Shiny Hunt: dropped %u unwritten line(s) for %s", (unsigned)s->queue_n, s->path);
    s->queue_n = 0;
    s->write_warned = false;
    s->slots_valid = false;
    hunt_init(&s->hunt);
    s->attached = true;
    s->tid = tid;
    s->file_has_header = false;
    s->reset_armed = false;
    s->path_ok = session_log_path(s->path, sizeof(s->path), s->rom_path, s->fixed_path, tid, "hunt");
    if (s->path_ok)
        load_file(s);
    s->dirty = true;
}

/* ---- Names ---- */

static void species_name(const HuntSession* s, const GameMem* m, uint16_t sp, char* out) {
    game_species_name(m, s->profile, sp, out);
}

static bool slots_known(const HuntSession* s) {
    return s->slots_valid && s->slots_group == s->facts.map_group &&
           s->slots_num == s->facts.map_num;
}

/* Distinct land species of this map in first-slot order; returns the count. */
static uint8_t map_species(const HuntSession* s, uint16_t out[WILD_SLOTS]) {
    uint8_t n = 0;
    if (!slots_known(s))
        return 0;
    for (uint8_t i = 0; i < WILD_SLOTS; i++) {
        uint16_t sp = s->slots[i].species;
        bool dup = sp == 0;
        for (uint8_t j = 0; j < n && !dup; j++)
            dup = out[j] == sp;
        if (!dup)
            out[n++] = sp;
    }
    return n;
}

/* ---- View ---- */

static void build_view(const HuntSession* s, const GameMem* m, HuntView* v) {
    const HuntState* h = &s->hunt;
    const HuntFacts* f = &s->facts;
    const GameProfile* p = s->profile;
    memset(v, 0, sizeof(*v));
    v->target_types[0] = v->target_types[1] = 0xFF;
    if (!s->attached || !f->valid) {
        game_strcpy(v->reason, sizeof(v->reason), "Waiting for save data");
        return;
    }
    v->on = true;
    char map[GAME_MAP_NAME_LEN];
    game_mapsec_name(m, p, s->mapsec, map);
    game_panel_title_case(map, v->map, sizeof(v->map)); /* as the Route page writes it */
    if (s->gate_predict)
        game_strcpy(v->method, sizeof(v->method), "Grass");
    v->advance = f->rand_count;
    v->total = h->total;
    v->session = h->session;
    v->reset_armed = s->reset_armed;
    v->write_failed = s->queue_n > 0 || !s->path_ok;
    v->file_foreign = !s->path_ok;

    if (h->target) {
        species_name(s, m, h->target, v->target);
        for (uint8_t i = 0; i < WILD_SLOTS && slots_known(s); i++) {
            if (s->slots[i].species != h->target)
                continue;
            v->target_percent = (uint8_t)(v->target_percent + k_land_percent[i]);
            v->target_types[0] = s->slots[i].type1;
            v->target_types[1] = s->slots[i].type2;
        }
        for (uint8_t i = 0; i < h->tally_n; i++)
            if (h->tally_species[i] == h->target)
                v->target_count = h->tally_count[i];
    } else {
        game_strcpy(v->target, sizeof(v->target), "Any");
        v->any_target = true;
    }
    v->chance = hunt_chance(h->target ? v->target_count : h->total);
    if (h->nature < 25)
        game_nature_name(m, p, h->nature, v->nature_filter);
    else
        game_strcpy(v->nature_filter, sizeof(v->nature_filter), "Any");

    v->tally_n = h->tally_n;
    for (uint8_t i = 0; i < h->tally_n; i++) {
        v->tally_species[i] = h->tally_species[i];
        v->tally_count[i] = h->tally_count[i];
        species_name(s, m, h->tally_species[i], v->tally_name[i]);
    }

    v->predict = s->gate_predict;
    game_strcpy(v->why_not, sizeof(v->why_not), s->gate_why);
    if (!v->predict)
        return;

    /* Rows come from the last frame that could predict: during a transient misread that the
     * gate hides, the current facts can't. */
    f = &s->pred_facts;
    hunt_next_rows(f, v->rows);
    for (uint8_t i = 0; i < HUNT_NEXT_ROWS; i++) {
        const WildPreview* r = &v->rows[i];
        if (r->kind != WG_BATTLE && r->kind != WG_ROAMER)
            continue;
        species_name(s, m, r->species, v->row_name[i]);
        if (r->nature < 25)
            game_nature_name(m, p, r->nature, v->row_nature[i]);
    }
    uint32_t mi = hunt_match_in(h, f);
    if (mi != UINT32_MAX) {
        v->match_found = true;
        v->match_in = mi;
        v->match_seconds = (float)mi / s->rate;
        if (h->match.nature < 25)
            game_nature_name(m, p, h->match.nature, v->match_nature);
        pokemon_nature_effect(h->match.nature, &v->match_up, &v->match_down);
        memcpy(v->match_ivs, h->match.ivs, sizeof(v->match_ivs));
    } else {
        v->scanning = h->anchored && (uint64_t)h->scanned + h->anchor_count <
                                         (uint64_t)f->rand_count + HUNT_SCAN_LIMIT;
    }
}

static void rebuild(HuntSession* s, const GameMem* m, uint32_t now_ms) {
    static HuntView next;
    build_view(s, m, &next);
    next.version = s->view.version;
    if (memcmp(&next, &s->view, sizeof(next)) != 0) {
        s->view = next;
        s->view.version++;
    }
    s->built = true;
    s->dirty = false;
    s->built_ms = now_ms;
}

/* ---- Per frame ---- */

/* Whether this frame's facts can predict, and why not. */
static bool gate_state(const HuntFacts* f, char why[40]) {
    why[0] = '\0';
    if (!f->predictable)
        game_strcpy(why, 40, f->why_not);
    else if (f->in_battle)
        game_strcpy(why, 40, "Not predicted in battle");
    else if (!f->on_overworld)
        game_strcpy(why, 40, "Not predicted here");
    return f->predictable && f->on_overworld && !f->in_battle;
}

static void gate_commit(HuntSession* s, bool predict, const char* why) {
    s->gate_known = true;
    s->gate_predict = predict;
    game_strcpy(s->gate_why, sizeof(s->gate_why), why);
    s->cand_frames = 0;
    s->dirty = true;
}

static void update_gate(HuntSession* s) {
    char why[40];
    bool predict = gate_state(&s->facts, why);
    if (predict)
        s->pred_facts = s->facts;
    if (!s->gate_known) {
        gate_commit(s, predict, why);
        return;
    }
    if (predict == s->gate_predict && strcmp(why, s->gate_why) == 0) {
        s->cand_frames = 0;
        return;
    }
    if (s->cand_frames == 0 || predict != s->cand_predict || strcmp(why, s->cand_why) != 0) {
        s->cand_predict = predict;
        game_strcpy(s->cand_why, sizeof(s->cand_why), why);
        s->cand_frames = 0;
    }
    if (++s->cand_frames >= GATE_FRAMES)
        gate_commit(s, predict, why);
}

bool hunt_session_frame(HuntSession* s, const GameMem* m, const NuzFacts* nf, bool rewinding,
                        char* toast_title, char* toast_detail) {
    if (!s->profile || !nf->valid)
        return false;
    s->frame++;
    if (session_tid_step(&s->tid_gate, s->attached, s->tid, nf->trainer_id))
        attach(s, nf->trainer_id);
    else if (!s->attached || nf->trainer_id != s->tid)
        return false; /* a misread (save blocks mid-move), a blank ID, or a new ID still debouncing */
    if (s->queue_n && s->frame % RETRY_FRAMES == 0)
        flush_queue(s);
    if (!hunt_read(s->profile, m, nf, s->shown, &s->facts))
        return false;
    s->facts.otid = s->tid;
    if (s->facts.predictable) {
        s->slots_valid = true;
        s->slots_group = s->facts.map_group;
        s->slots_num = s->facts.map_num;
        memcpy(s->slots, s->facts.wild.slots, sizeof(s->slots));
    }
    s->mapsec = nf->mapsec;

    if (s->shown)
        update_gate(s);

    HuntEvent ev;
    if (!hunt_frame(&s->hunt, &s->facts, rewinding, &ev))
        return false;
    char line[HUNT_LINE_MAX];
    snprintf(line, sizeof(line), "E %u %08X %u %u", ev.species, (unsigned)ev.pid,
             ev.shiny ? 1u : 0u, ev.method);
    enqueue(s, line);
    s->dirty = true;
    if (!ev.shiny)
        return false;
    char name[GAME_NAME_LEN], nice[GAME_NAME_LEN];
    species_name(s, m, ev.species, name);
    game_panel_title_case(name, nice, sizeof(nice));
    snprintf(toast_title, 48, "Shiny %s!", nice);
    char count[16];
    hunt_panel_number(s->hunt.total, count, sizeof(count));
    snprintf(toast_detail, 48, s->hunt.total == 1 ? "After %s encounter" : "After %s encounters",
             count);
    return true;
}

void hunt_session_discontinuity(HuntSession* s) {
    hunt_discontinuity(&s->hunt);
    s->gate_known = false; /* the loaded state shows its own gate at once */
    s->rate_valid = false;
    s->dirty = true;
}

void hunt_session_set_shown(HuntSession* s, bool shown) {
    if (shown && !s->shown) {
        /* Nothing from before it was hidden; the next frame's state shows at once. */
        s->gate_known = false;
        s->gate_predict = false;
        s->gate_why[0] = '\0';
        s->dirty = true;
    }
    s->shown = shown;
}

/* ---- Render-time work ---- */

static void update_rate(HuntSession* s, uint32_t now_ms) {
    const HuntFacts* f = &s->facts;
    if (!f->valid || !f->on_overworld || f->in_battle) {
        s->rate_valid = false;
        return;
    }
    uint32_t dt = now_ms - s->rate_ms;
    if (!s->rate_valid || f->rand_count < s->rate_count || dt > 5000) {
        s->rate_valid = true;
        s->rate_ms = now_ms;
        s->rate_count = f->rand_count;
        return;
    }
    if (dt < RATE_MIN_MS)
        return;
    uint32_t delta = f->rand_count - s->rate_count;
    s->rate_ms = now_ms;
    s->rate_count = f->rand_count;
    if (delta == 0)
        return; /* paused or in a menu: keep the old rate */
    float inst = (float)delta * 1000.0f / (float)dt;
    float alpha = (float)dt / RATE_SPAN_MS;
    if (alpha > 1.0f)
        alpha = 1.0f;
    s->rate += (inst - s->rate) * alpha;
    if (s->rate < 1.0f)
        s->rate = 1.0f;
}

void hunt_session_present(HuntSession* s, const GameMem* m, uint32_t now_ms, uint32_t budget) {
    if (!s->profile)
        return;
    if (s->attached && s->facts.valid) {
        hunt_scan(&s->hunt, &s->facts, budget);
        update_rate(s, now_ms);
    }
    if (s->reset_armed && now_ms - s->reset_armed_ms > RESET_ARM_MS) {
        s->reset_armed = false;
        s->dirty = true;
    }
    if (s->built && !s->dirty && now_ms - s->built_ms < REBUILD_MS)
        return;
    rebuild(s, m, now_ms);
}

/* ---- Keys ---- */

static int wrap(int i, int n) {
    return ((i % n) + n) % n;
}

void hunt_session_target(HuntSession* s, int delta) {
    if (!s->attached)
        return;
    uint16_t sp[WILD_SLOTS + 1];
    uint8_t n = map_species(s, sp + 1);
    if (n == 0)
        return;
    sp[0] = 0; /* Any */
    int cur = 0;
    for (int i = 1; i <= n; i++)
        if (sp[i] == s->hunt.target)
            cur = i;
    uint16_t next = sp[wrap(cur + delta, n + 1)];
    if (next == s->hunt.target)
        return;
    s->hunt.target = next;
    char line[HUNT_LINE_MAX];
    snprintf(line, sizeof(line), "T %u", next);
    enqueue(s, line);
    s->dirty = true;
}

void hunt_session_nature(HuntSession* s, int delta) {
    if (!s->attached)
        return;
    int next = wrap(s->hunt.nature + delta, HUNT_NATURE_ANY + 1);
    if (next == s->hunt.nature)
        return;
    s->hunt.nature = (uint8_t)next;
    char line[HUNT_LINE_MAX];
    snprintf(line, sizeof(line), "N %d", next);
    enqueue(s, line);
    s->dirty = true;
}

bool hunt_session_reset_key(HuntSession* s, uint32_t now_ms) {
    if (!s->attached)
        return false;
    s->dirty = true;
    if (s->reset_armed && now_ms - s->reset_armed_ms <= RESET_ARM_MS) {
        s->reset_armed = false;
        hunt_reset_counts(&s->hunt);
        enqueue(s, "R");
        return true;
    }
    s->reset_armed = true;
    s->reset_armed_ms = now_ms;
    return false;
}

/* ---- Dump (headless --game-dump) ---- */

bool hunt_session_dump(const HuntSession* s, FILE* out) {
    const HuntView* v = &s->view;
    int ok = 1;
    if (!v->on)
        return fprintf(out, "hunt off: %s\n", v->reason) >= 0;
    ok &=
        fprintf(
            out,
            "hunt tid %08X map %s method %s advance %u total %u session %u target %s nature %s%s\n",
            (unsigned)s->tid, v->map, v->method, (unsigned)v->advance, (unsigned)v->total,
            (unsigned)v->session, v->target, v->nature_filter,
            v->write_failed ? " write-failed" : "") >= 0;
    for (uint8_t i = 0; i < v->tally_n; i++)
        ok &= fprintf(out, "hunt seen %s %u\n", v->tally_name[i], (unsigned)v->tally_count[i]) >= 0;
    if (!v->predict)
        return ok && fprintf(out, "hunt next none: %s\n", v->why_not) >= 0;
    for (uint8_t i = 0; i < HUNT_NEXT_ROWS; i++)
        ok &=
            fprintf(out, "hunt row +%u kind %u %s %s lv %u pid %08X iv %u/%u/%u/%u/%u/%u%s%s\n", i,
                    v->rows[i].kind, v->row_name[i], v->row_nature[i], v->rows[i].level,
                    (unsigned)v->rows[i].pid, v->rows[i].ivs[0], v->rows[i].ivs[1],
                    v->rows[i].ivs[2], v->rows[i].ivs[3], v->rows[i].ivs[4], v->rows[i].ivs[5],
                    v->rows[i].certain ? "" : " uncertain", v->rows[i].shiny ? " shiny" : "") >= 0;
    if (v->match_found)
        ok &= fprintf(out, "hunt match in %u (%.1fs) %s\n", (unsigned)v->match_in,
                      (double)v->match_seconds, v->match_nature) >= 0;
    else
        ok &= fprintf(out, "hunt match %s\n", v->scanning ? "scanning" : "none") >= 0;
    return ok != 0;
}
