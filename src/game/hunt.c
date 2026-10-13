#include "game/hunt.h"
#include "game/nuzlocke_read.h"
#include "game/rng3.h"
#include <math.h>
#include <string.h>

#define STABLE_FRAMES 2 /* R8: same enemy pid on 2 frames in a row */

void hunt_init(HuntState* h) {
    memset(h, 0, sizeof(*h));
    h->nature = HUNT_NATURE_ANY;
    h->resync = true;
}

void hunt_reset_counts(HuntState* h) {
    h->total = 0;
    h->shiny_total = 0;
    h->session = 0;
    h->tally_n = 0;
    memset(h->tally_species, 0, sizeof(h->tally_species));
    memset(h->tally_count, 0, sizeof(h->tally_count));
    h->version++;
}

static void drop_scan(HuntState* h) {
    h->anchored = false;
    h->found = false;
    h->scanned = 0;
    h->version++;
}

void hunt_discontinuity(HuntState* h) {
    h->resync = true;
    h->armed_method = 0;
    h->stable_frames = 0;
    drop_scan(h);
}

static bool seen(const HuntState* h, uint32_t pid) {
    for (uint8_t i = 0; i < h->seen_n; i++)
        if (h->seen_pids[i] == pid)
            return true;
    return false;
}

static void tally(HuntState* h, uint16_t species) {
    uint8_t i = 0;
    while (i < h->tally_n && h->tally_species[i] != species)
        i++;
    if (i == h->tally_n) {
        if (h->tally_n == HUNT_TALLY_MAX)
            return;
        h->tally_species[i] = species;
        h->tally_count[i] = 0;
        h->tally_n++;
    }
    h->tally_count[i]++;
    /* Keep most seen first; ties stay in first-seen order. */
    for (; i > 0 && h->tally_count[i] > h->tally_count[i - 1]; i--) {
        uint16_t s = h->tally_species[i];
        uint32_t c = h->tally_count[i];
        h->tally_species[i] = h->tally_species[i - 1];
        h->tally_count[i] = h->tally_count[i - 1];
        h->tally_species[i - 1] = s;
        h->tally_count[i - 1] = c;
    }
}

void hunt_apply(HuntState* h, const HuntEvent* ev) {
    if (ev->kind != HUNT_EV_ENCOUNTER)
        return;
    h->total++;
    if (ev->shiny)
        h->shiny_total++;
    tally(h, ev->species);
    if (!seen(h, ev->pid)) {
        h->seen_pids[h->seen_head] = ev->pid;
        h->seen_head = (uint8_t)((h->seen_head + 1) % HUNT_SEEN_RING);
        if (h->seen_n < HUNT_SEEN_RING)
            h->seen_n++;
    }
    h->version++;
}

static uint8_t method_of(uint8_t battle_kind) {
    switch (battle_kind) {
    case HUNT_BATTLE_WILD:
        return HUNT_METHOD_WILD + 1;
    case HUNT_BATTLE_SAFARI:
        return HUNT_METHOD_SAFARI + 1;
    case HUNT_BATTLE_STATIC:
        return HUNT_METHOD_STATIC + 1;
    default:
        return 0;
    }
}

bool hunt_frame(HuntState* h, const HuntFacts* f, bool rewinding, HuntEvent* ev) {
    if (!f->valid)
        return false;
    bool edge = f->in_battle && !h->was_in_battle;
    h->was_in_battle = f->in_battle;
    if (h->resync) {
        /* First frame after a discontinuity: a battle already running is not an edge. */
        h->resync = false;
        h->armed_method = 0;
        return false;
    }
    if (!f->in_battle) {
        h->armed_method = 0;
        h->counted_this_battle = false;
        h->stable_frames = 0;
        return false;
    }
    if (edge && !rewinding) {
        h->armed_method = method_of(f->battle_kind);
        h->counted_this_battle = false;
        h->stable_frames = 0;
    }
    if (rewinding || !h->armed_method || h->counted_this_battle)
        return false;
    if (!f->enemy_complete) {
        h->stable_frames = 0;
        return false;
    }
    if (h->stable_frames && f->enemy_pid == h->stable_pid) {
        h->stable_frames++;
    } else {
        h->stable_pid = f->enemy_pid;
        h->stable_frames = 1;
    }
    if (h->stable_frames < STABLE_FRAMES)
        return false;
    h->counted_this_battle = true;
    if (seen(h, f->enemy_pid))
        return false;
    ev->kind = HUNT_EV_ENCOUNTER;
    ev->species = f->enemy_species;
    ev->pid = f->enemy_pid;
    ev->shiny = nuz_is_shiny(f->enemy_pid, f->enemy_ot);
    ev->method = (uint8_t)(h->armed_method - 1);
    hunt_apply(h, ev);
    h->session++;
    return true;
}

/* FNV-1a over the inputs a prediction depends on. */
static uint32_t mix(uint32_t k, uint32_t v) {
    for (int i = 0; i < 4; i++) {
        k ^= (v >> (8 * i)) & 0xFFu;
        k *= 16777619u;
    }
    return k;
}

static uint32_t facts_key(const HuntState* h, const HuntFacts* f) {
    const WildInput* w = &f->wild;
    uint32_t k = 2166136261u;
    k = mix(k, (uint32_t)f->map_group | (uint32_t)f->map_num << 8 | (uint32_t)w->method << 16);
    for (int i = 0; i < WILD_SLOTS; i++) {
        const WildSlot* s = &w->slots[i];
        k = mix(k, s->species | (uint32_t)s->min_level << 16 | (uint32_t)s->max_level << 24);
        k = mix(k, s->type1 | (uint32_t)s->type2 << 8 | (uint32_t)s->gender_ratio << 16 |
                       (uint32_t)s->two_abilities << 24);
    }
    k = mix(k, w->rate | (uint32_t)w->metatile_roll << 16 | (uint32_t)w->roamer_here << 17 |
                   (uint32_t)w->outbreak_here << 18 | (uint32_t)w->lead_ok << 19 |
                   (uint32_t)w->repel_active << 20);
    k = mix(k,
            w->roamer_species | (uint32_t)w->roamer_level << 16 | (uint32_t)w->repel_level << 24);
    k = mix(k, w->lead_ability | (uint32_t)w->lead_level << 8 | (uint32_t)w->lead_nature << 16 |
                   (uint32_t)w->lead_gender << 24);
    k = mix(k, f->otid);
    k = mix(k, h->target | (uint32_t)h->nature << 16);
    return k;
}

/* wildgen reads otid from the input; the session may override HuntFacts.otid (debounced TID). */
static void wild_input(const HuntFacts* f, WildInput* in) {
    *in = f->wild;
    in->otid = f->otid;
}

static bool on_anchor(const HuntState* h, const HuntFacts* f) {
    return h->anchored && f->rand_count >= h->anchor_count &&
           rng3_jump(h->anchor_rng, f->rand_count - h->anchor_count) == f->rng;
}

void hunt_scan(HuntState* h, const HuntFacts* f, uint32_t budget) {
    if (!f->valid || !f->predictable || !f->on_overworld || f->in_battle)
        return;
    uint32_t key = facts_key(h, f);
    if (key != h->key || !on_anchor(h, f)) {
        drop_scan(h);
        h->anchored = true;
        h->key = key;
        h->anchor_count = f->rand_count;
        h->anchor_rng = f->rng;
        h->cursor_rng = f->rng;
    }
    uint32_t now = f->rand_count - h->anchor_count;
    if (h->found && h->match_at >= f->rand_count)
        return;
    if (h->found) {
        h->found = false; /* passed: look for the next one */
        h->version++;
    }
    if (h->scanned < now) {
        /* Advances behind now can no longer be hit. */
        h->scanned = now;
        h->cursor_rng = f->rng;
    }
    WildInput in;
    wild_input(f, &in);
    uint64_t end = (uint64_t)now + HUNT_SCAN_LIMIT;
    for (; budget > 0 && h->scanned < end; budget--) {
        WildPreview p;
        wildgen_step(&in, h->cursor_rng, &p);
        uint32_t k = h->scanned++;
        h->cursor_rng = rng3_next(h->cursor_rng);
        if (hunt_matches(h, &p)) {
            h->found = true;
            h->match_at = h->anchor_count + k;
            h->match = p;
            h->version++;
            return;
        }
    }
}

uint32_t hunt_match_in(const HuntState* h, const HuntFacts* f) {
    if (!h->found || !f->valid || !f->predictable || h->match_at < f->rand_count ||
        facts_key(h, f) != h->key || !on_anchor(h, f))
        return UINT32_MAX;
    return h->match_at - f->rand_count;
}

void hunt_next_rows(const HuntFacts* f, WildPreview out[HUNT_NEXT_ROWS]) {
    if (!f->valid || !f->predictable) {
        memset(out, 0, sizeof(WildPreview) * HUNT_NEXT_ROWS);
        return;
    }
    WildInput in;
    wild_input(f, &in);
    uint32_t s = f->rng;
    for (int i = 0; i < HUNT_NEXT_ROWS; i++, s = rng3_next(s))
        wildgen_step(&in, s, &out[i]);
}

bool hunt_matches(const HuntState* h, const WildPreview* p) {
    return p->kind == WG_BATTLE && p->certain && p->shiny &&
           (h->target == 0 || p->species == h->target) &&
           (h->nature >= HUNT_NATURE_ANY || p->nature == h->nature);
}

double hunt_chance(uint32_t n) {
    return 1.0 - pow(8191.0 / 8192.0, (double)n);
}
