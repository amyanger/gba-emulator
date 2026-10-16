#include "game/run_track.h"
#include "game/emerald_layout.h"
#include <string.h>

/* gBattleTypeFlags bits (include/constants/battle.h) */
#define BT_SAFARI         (1u << 7)
/* gBattleOutcome */
#define OUTCOME_WON 1
/* Room kept free each frame for edge events: 6 FAINTs, a WHITEOUT and a BATTLE. */
#define EDGE_ROOM 8
#define DIFF_MAX  (RUN_EVENTS_FRAME_MAX - EDGE_ROOM)

void run_tracker_reset(RunTracker* t) {
    memset(t, 0, sizeof(*t));
}

static void new_event(RunEvent* ev, uint8_t type, uint32_t pt) {
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->pt = pt;
    ev->sec = NUZ_SEC_NONE;
    ev->low = 1000;
}

/* A party slot that belongs to the run: not empty, not an egg, not an in-game partner's. */
static bool own_mon(const RunTracker* t, const NuzMon* m) {
    return m->species != 0 && !m->egg && !(t->partner_ot != 0 && m->key.ot == t->partner_ot);
}

/* Where the logged timeline ends. RELOADs are not kept in the log, so use the events a
 * RELOAD can truncate: the immune ones (START, backfill, untimed) may sit above an earlier
 * RELOAD's target and would make an older save look like a reload. */
static uint32_t log_max_pt(const RunLog* log) {
    uint32_t hi = 0;
    for (int i = 0; i < log->ev_count; i++) {
        const RunEvent* e = &log->ev[i];
        bool immune = e->type == RUN_EV_START || e->untimed || (e->type == RUN_EV_JOIN && e->how == RUN_HOW_BACKFILL);
        if (!immune && e->pt > hi) hi = e->pt;
    }
    for (int i = 0; i < log->bt_count; i++)
        if (!log->bt[i].untimed && log->bt[i].pt > hi) hi = log->bt[i].pt;
    return hi;
}

/* Edges always have room: difference events stop at DIFF_MAX. */
static void emit_edge(const RunEvent* ev, RunEvent* out, uint8_t* n) {
    if (*n < RUN_EVENTS_FRAME_MAX) out[(*n)++] = *ev;
}

/* ---- R2: discontinuity ---- */

/* cut: the play time the timeline now continues from (a reload's target, or this frame). */
static void rebaseline(RunTracker* t, const NuzFacts* nf, const RunFacts* rf, uint32_t cut) {
    memset(t->hp, 0, sizeof(t->hp));
    for (uint8_t i = 0; i < nf->party_count && i < NUZ_PARTY_MAX; i++) {
        const NuzMon* m = &nf->party[i];
        if (!own_mon(t, m)) continue;
        t->hp[i].key = m->key;
        t->hp[i].alive_seen = m->hp > 0;
    }
    memset(t->evo, 0, sizeof(t->evo));
    t->prev_whiteout = rf->whiteout;

    /* R7: a battle we cannot finish watching (restored to before its start, or no longer
     * in battle so the outcome is unknown) is dropped; otherwise KOs and faints after cut
     * un-happen. */
    if (t->in_battle) {
        if (cut < t->battle_pt || !nf->in_battle) {
            t->in_battle = false;
        } else {
            uint8_t k = 0;
            for (uint8_t i = 0; i < t->ko_count; i++)
                if (t->kos[i].pt <= cut) t->kos[k++] = t->kos[i];
            t->ko_count = k;
            k = 0;
            for (uint8_t i = 0; i < t->battle_faints; i++)
                if (t->faint_pts[i] <= cut) t->faint_pts[k++] = t->faint_pts[i];
            t->battle_faints = k;
        }
    }
    for (uint8_t b = 0; b < 4; b++) {
        const RunBattler* rb = &rf->b[b];
        bool live = t->in_battle && t->foe_ok[b] && b < rf->battlers && (rb->pos & 1) && rb->hp > 0 &&
                    rb->species != 0;
        t->foe_hp[b] = live ? rb->hp : 0;
        t->foe_armed[b] = live;
        t->foe_live[b] = live ? 2 : 0;
    }
    t->prev_in_battle = nf->in_battle;   /* a battle already running without us is not ours */

    t->resync = true;
    t->boxes_fresh = false;
    t->box_rescan = false;
    t->conf_fresh = false;
    t->cand_frames = 0;
    t->conf_steps = 0;
}

/* ---- R5: values from the save blocks, confirmed ---- */

static void confirm(RunTracker* t, const RunFacts* rf, bool advanced) {
    if (rf->badges == t->cand_badges && rf->story == t->cand_story) {
        if (advanced && t->cand_frames < 255) t->cand_frames++;
    } else {
        t->cand_badges = rf->badges;
        t->cand_story = rf->story;
        t->cand_frames = advanced ? 1 : 0;
    }
    if (t->sb_stable < RUN_CONFIRM_FRAMES) return;
    t->conf_steps = rf->stats[RUN_STAT_STEPS];   /* changes too often to wait for it to hold */
    if (t->cand_frames >= RUN_CONFIRM_FRAMES) {
        t->conf_badges = t->cand_badges;
        t->conf_story = t->cand_story;
        t->conf_fresh = true;
    }
}

/* ---- R1 + R4: difference events for badges and story beats ---- */

/* Timed now, or on a resync: the orphan time from a reload, the Hall of Fame stat, else untimed. */
static void stamp(const RunTracker* t, const RunLog* log, const RunFacts* rf, RunEvent* ev, int idx,
                  uint32_t pt) {
    if (!t->resync) return;
    if (log->orphan[idx] && log->orphan_pt[idx] <= pt) {
        ev->pt = log->orphan_pt[idx];
        return;
    }
    if (idx == RUN_BADGES + rf->hof_story && rf->stats[RUN_STAT_ENTERED_HOF] > 0) {
        uint32_t s = rf->stats[RUN_STAT_FIRST_HOF_TIME];  /* h << 16 | m << 8 | s */
        uint32_t hof = (((s >> 16) * 60u + ((s >> 8) & 0xFF)) * 60u + (s & 0xFF)) * 60u;
        if (hof <= pt) {
            ev->pt = hof;
            return;
        }
    }
    ev->untimed = true;
}

static bool track_milestones(RunTracker* t, const RunLog* log, const NuzFacts* nf, const RunFacts* rf,
                             uint32_t pt, RunEvent* out, uint8_t* n) {
    if (!t->conf_fresh || log->events_full) return true;   /* a full log would never take them */
    for (uint8_t b = 0; b < RUN_BADGES; b++) {
        if (!(t->conf_badges & (1u << b)) || log->badge[b]) continue;
        if (*n >= DIFF_MAX) return false;
        RunEvent* ev = &out[(*n)++];
        new_event(ev, RUN_EV_BADGE, pt);
        ev->id = (uint8_t)(b + 1);
        uint32_t sum = 0, cnt = 0;
        for (uint8_t i = 0; i < nf->party_count && i < NUZ_PARTY_MAX; i++) {
            const NuzMon* m = &nf->party[i];
            if (!own_mon(t, m)) continue;
            if (m->level > ev->team_max) ev->team_max = m->level;
            sum += m->level;
            cnt++;
        }
        ev->team_avg = (uint8_t)(cnt ? (sum + cnt / 2) / cnt : 0);
        ev->dex = rf->dex_owned;
        ev->steps = t->conf_steps;
        stamp(t, log, rf, ev, b, pt);
    }
    for (uint8_t s = 0; s < RUN_STORY_COUNT; s++) {
        if (!(t->conf_story & (1u << s)) || log->story[s]) continue;
        if (*n >= DIFF_MAX) return false;
        RunEvent* ev = &out[(*n)++];
        new_event(ev, RUN_EV_STORY, pt);
        ev->id = s;
        stamp(t, log, rf, ev, RUN_BADGES + s, pt);
    }
    return true;
}

/* ---- R1 + R11: joins and evolutions ---- */

static bool emitted_key(const RunEvent* out, uint8_t n, NuzKey key) {
    for (uint8_t i = 0; i < n; i++)
        if (out[i].type == RUN_EV_JOIN && nuz_key_eq(out[i].key, key)) return true;
    return false;
}

/* False when out filled up before every new mon got its JOIN. */
static bool track_joins(RunTracker* t, const RunLog* log, const NuzFacts* nf, const NuzMon* mons,
                        uint16_t count, uint32_t pt, RunEvent* out, uint8_t* n) {
    if (log->events_full || log->member_count >= RUN_MEMBERS_MAX) return true;
    for (uint16_t i = 0; i < count; i++) {
        const NuzMon* m = &mons[i];
        if (!own_mon(t, m) || run_find_member(log, m->key) >= 0 || emitted_key(out, *n, m->key))
            continue;
        if (*n >= RUN_EVENTS_FRAME_MAX) return false;
        RunEvent* ev = &out[(*n)++];
        new_event(ev, RUN_EV_JOIN, pt);
        ev->key = m->key;
        ev->key.variant = m->key.variant ? 1 : 0;
        ev->species = m->species;
        ev->level = m->level;
        ev->sec = m->met;
        ev->ball = m->ball;
        ev->shiny = nuz_is_shiny(m->key.pid, m->key.ot);
        memcpy(ev->nick, m->nick, sizeof(ev->nick));
        if (t->pending && m->key.pid == t->pending_pid && m->key.ot == t->pending_ot) {
            ev->how = RUN_HOW_CAUGHT;
            ev->newdex = t->pending_newdex;
            t->pending = false;
        } else if (t->resync) {
            ev->how = RUN_HOW_BACKFILL;
        } else if (m->met == 0xFE || m->key.ot != nf->trainer_id) {
            ev->how = RUN_HOW_TRADED;
        } else if (m->met_level == 0) {
            ev->how = RUN_HOW_HATCHED;
        } else {
            ev->how = RUN_HOW_GIFT;
        }
    }
    return true;
}

static void track_evolutions(RunTracker* t, const RunLog* log, const NuzFacts* nf, uint32_t pt,
                             RunEvent* out, uint8_t* n) {
    if (log->events_full) return;
    for (uint8_t i = 0; i < NUZ_PARTY_MAX; i++) {
        RunEvoWatch* w = &t->evo[i];
        const NuzMon* m = &nf->party[i];
        int mb = i < nf->party_count && own_mon(t, m) ? run_find_member(log, m->key) : -1;
        if (mb < 0 || log->members[mb].species == m->species) {
            memset(w, 0, sizeof(*w));
            continue;
        }
        if (!nuz_key_eq(w->key, m->key) || w->species != m->species) {
            w->key = m->key;
            w->species = m->species;
            w->frames = 0;
        }
        if (w->frames < 255) w->frames++;
        if (w->frames < RUN_CONFIRM_FRAMES || *n >= RUN_EVENTS_FRAME_MAX) continue;
        RunEvent* ev = &out[(*n)++];
        new_event(ev, RUN_EV_EVOLVE, pt);
        ev->key = m->key;
        ev->species = m->species;
        ev->from = log->members[mb].species;
        ev->level = m->level;
        ev->untimed = t->resync;
        memset(w, 0, sizeof(*w));
    }
}

/* ---- Edges: faints and whiteouts ---- */

static void track_faints(RunTracker* t, const RunLog* log, const NuzFacts* nf, uint32_t pt, RunEvent* out,
                         uint8_t* n) {
    RunHpWatch next[NUZ_PARTY_MAX];
    memset(next, 0, sizeof(next));
    for (uint8_t i = 0; i < nf->party_count && i < NUZ_PARTY_MAX; i++) {
        const NuzMon* m = &nf->party[i];
        if (!own_mon(t, m) || run_find_member(log, m->key) < 0) continue;
        RunHpWatch* w = &next[i];
        w->key = m->key;
        for (uint8_t j = 0; j < NUZ_PARTY_MAX; j++)
            if (nuz_key_eq(t->hp[j].key, m->key)) {
                w->alive_seen = t->hp[j].alive_seen;
                w->zero_frames = t->hp[j].zero_frames;
            }
        if (m->hp > 0) {
            w->alive_seen = true;
            w->zero_frames = 0;
            continue;
        }
        if (!w->alive_seen || ++w->zero_frames < RUN_FAINT_FRAMES) continue;
        RunEvent ev;
        new_event(&ev, RUN_EV_FAINT, pt);
        ev.key = m->key;
        ev.species = m->species;
        ev.level = m->level;
        ev.foe = nf->in_battle ? nf->enemy_species : 0;
        ev.sec = nf->mapsec;
        emit_edge(&ev, out, n);
        if (t->in_battle && t->battle_faints < 12) t->faint_pts[t->battle_faints++] = pt;
        w->alive_seen = false;   /* re-armed when HP comes back */
        w->zero_frames = 0;
    }
    memcpy(t->hp, next, sizeof(next));
}

/* ---- R7 + R8: battles and KO credit ---- */

static void open_battle(RunTracker* t, uint32_t pt) {
    t->in_battle = true;
    t->battle_pt = pt;
    t->battle_kind = RUN_BATTLE_WILD;
    t->battle_trainer = t->battle_ace = 0;
    t->battle_outcome = 0;
    t->battle_foe = 0;
    t->battle_shiny = false;
    t->battle_faints = 0;
    t->battle_pid = t->battle_ot = 0;
    memset(t->foe_hp, 0, sizeof(t->foe_hp));
    memset(t->foe_armed, 0, sizeof(t->foe_armed));
    memset(t->foe_live, 0, sizeof(t->foe_live));
    memset(t->foe_ok, 0, sizeof(t->foe_ok));
    t->fought_mask = 0;
    memset(t->fought_key, 0, sizeof(t->fought_key));
    memset(t->fought_max, 0, sizeof(t->fought_max));
    t->ko_count = 0;
    t->start_owned = false;
}

static void battle_frame(RunTracker* t, const RunLog* log, const NuzFacts* nf, const RunFacts* rf,
                         uint32_t pt) {
    /* Latched once known: battle data may still be loading on the first frames. */
    if (t->battle_foe == 0 && nf->enemy_species != 0) {
        t->battle_foe = nf->enemy_species;
        t->battle_pid = nf->enemy_pid;
        t->battle_ot = nf->enemy_ot;
        t->battle_shiny = nuz_is_shiny(nf->enemy_pid, nf->enemy_ot);
        t->start_owned = rf->enemy_owned;
    }
    if (nf->battle_flags & NUZ_BATTLE_TRAINER) {
        if (rf->opponent != 0 && t->battle_trainer == 0) {
            t->battle_kind = RUN_BATTLE_TRAINER;
            t->battle_trainer = rf->opponent;
            /* Only a gym leader, Elite Four member or the Champion has an ace worth a feed row. */
            t->battle_ace = rf->opponent_leader ? rf->ace_species : 0;
        } else if (t->battle_kind == RUN_BATTLE_WILD) {
            t->battle_kind = RUN_BATTLE_OTHER;
        }
    }
    if (nf->battle_outcome != 0) t->battle_outcome = nf->battle_outcome;

    uint8_t count = rf->battlers > 4 ? 4 : rf->battlers;
    bool safari = (nf->battle_flags & BT_SAFARI) != 0;
    bool partner = rf->partner_battle;
    for (uint8_t b = 0; b < count; b++) {
        const RunBattler* rb = &rf->b[b];
        if (!(rb->pos & 1)) {
            uint16_t pi = rb->party_index;
            if (pi < nf->party_count && pi < NUZ_PARTY_MAX && own_mon(t, &nf->party[pi])) {
                t->fought_mask |= (uint8_t)(1u << pi);
                t->fought_key[pi] = nf->party[pi].key;
                t->fought_max[pi] = rb->max_hp;
            }
            continue;
        }
        t->foe_hp[b] = rb->hp;
        /* gBattleMons can still hold the last battle when inBattle rises: arm a slot only
         * after 2 frames of a live mon, and the lead slot only once it shows this battle's foe. */
        if (rb->hp > 0 && rb->species != 0) {
            bool fits = t->foe_ok[b] || rb->pos != 1 || t->battle_foe == 0 || rb->species == t->battle_foe;
            t->foe_live[b] = fits ? (uint8_t)(t->foe_live[b] < 255 ? t->foe_live[b] + 1 : 255) : 0;
            if (t->foe_live[b] >= 2) t->foe_armed[b] = t->foe_ok[b] = true;
            continue;
        }
        t->foe_live[b] = 0;
        if (!t->foe_armed[b]) continue;
        t->foe_armed[b] = false;
        if (safari || t->ko_count >= 12) continue;

        /* R7: credit the player-side attacker's party mon when it is a run member. */
        RunKoEntry* e = &t->kos[t->ko_count++];
        memset(e, 0, sizeof(*e));
        e->pt = pt;
        e->foe_level = rb->level;
        e->foe_slot = b;
        uint8_t a = rf->attacker;
        if (a < count && a != b && !(rf->b[a].pos & 1)) {
            uint16_t pi = rf->b[a].party_index;
            bool partner_mon = partner && pi >= 3;
            if (!partner_mon && pi < nf->party_count && pi < NUZ_PARTY_MAX &&
                own_mon(t, &nf->party[pi]) && run_find_member(log, nf->party[pi].key) >= 0) {
                e->credited = true;
                e->key = nf->party[pi].key;
                e->att_level = rf->b[a].level;
            }
        }
        e->on_ace = t->battle_kind == RUN_BATTLE_TRAINER && t->battle_ace != 0 && rb->species == t->battle_ace;
    }
}

static void close_battle(RunTracker* t, const NuzFacts* nf, uint32_t pt, RunEvent* out, uint8_t* n) {
    RunEvent ev;
    new_event(&ev, RUN_EV_BATTLE, pt);
    ev.kind = t->battle_kind;
    ev.trainer = t->battle_kind == RUN_BATTLE_TRAINER ? t->battle_trainer : 0;
    ev.foe = t->battle_foe;
    ev.shiny = t->battle_kind == RUN_BATTLE_WILD && t->battle_shiny;
    ev.outcome = t->battle_outcome;
    ev.faints = t->battle_faints;
    for (uint8_t i = 0; i < t->ko_count; i++) {
        const RunKoEntry* e = &t->kos[i];
        if (!e->credited) {
            ev.indirect++;
            continue;
        }
        uint8_t k = 0;
        while (k < ev.ko_count && !nuz_key_eq(ev.kos[k].key, e->key)) k++;
        if (k == ev.ko_count) {
            if (ev.ko_count == RUN_KO_SLOTS) {
                ev.indirect++;
                continue;
            }
            ev.kos[k].key = e->key;
            ev.ko_count++;
        }
        ev.kos[k].count++;
        ev.kos[k].level = e->att_level;
        if (e->foe_level >= e->att_level + 10 && e->foe_level - e->att_level > ev.upset)
            ev.upset = (uint8_t)(e->foe_level - e->att_level);
        if (e->on_ace) {
            ev.has_ace = true;
            ev.ace_by = e->key;
            ev.ace_species = t->battle_ace;
        }
    }
    /* Closest call: mons that fought and are still standing at the end of a won battle. */
    if (t->battle_outcome == OUTCOME_WON) {
        for (uint8_t pi = 0; pi < NUZ_PARTY_MAX; pi++) {
            if (!(t->fought_mask & (1u << pi)) || t->fought_max[pi] == 0) continue;
            for (uint8_t i = 0; i < nf->party_count && i < NUZ_PARTY_MAX; i++) {
                const NuzMon* m = &nf->party[i];
                if (!nuz_key_eq(m->key, t->fought_key[pi]) || m->hp == 0) continue;
                uint32_t pm = (uint32_t)m->hp * 1000u / t->fought_max[pi];
                if (pm > 1000) pm = 1000;
                if (pm < ev.low) {
                    ev.low = (uint16_t)pm;
                    ev.low_by = m->key;
                }
            }
        }
    }
    if (t->battle_outcome == NUZ_OUTCOME_CAUGHT) {
        t->pending = true;
        t->pending_pid = t->battle_pid;
        t->pending_ot = t->battle_ot;
        t->pending_newdex = !t->start_owned;
    }
    t->in_battle = false;
    emit_edge(&ev, out, n);
}

static void track_battle(RunTracker* t, const RunLog* log, const NuzFacts* nf, const RunFacts* rf,
                         uint32_t pt, RunEvent* out, uint8_t* n) {
    if (nf->in_battle && !t->prev_in_battle) open_battle(t, pt);
    if (!t->in_battle) return;
    if (nf->in_battle) battle_frame(t, log, nf, rf, pt);
    else close_battle(t, nf, pt, out, n);
}

/* ---- One frame ---- */

uint8_t run_track_step(RunTracker* t, const RunLog* log, const NuzFacts* nf, const RunFacts* rf,
                       RunEvent out[RUN_EVENTS_FRAME_MAX]) {
    uint8_t n = 0;
    /* Short invalid stretches (save blocks moving) are frozen like a dip; long ones are a
     * discontinuity on the next valid frame. */
    if (!nf->valid || !rf->valid) {
        if (t->invalid_frames < 255) t->invalid_frames++;
        return 0;
    }
    if (t->invalid_frames > RUN_INVALID_FRAMES) t->have_prev = false;
    t->invalid_frames = 0;
    t->partner_ot = rf->partner_otid;
    uint32_t pt = nf->play_time;
    if (!t->seeded) {
        t->high = log_max_pt(log);
        t->seeded = true;
    }

    /* R2 reloads: play time never runs backwards on its own. While it reads behind, nothing
     * is tracked and no state moves. Any frame back at or past high cancels the dip as a
     * misread and play resumes as if it never happened. A reload needs RUN_RELOAD_SETTLE
     * consecutive behind frames whose play time runs on from low about one per frame, so
     * garbage reads (at boot, or a save block move) can never truncate the log. */
    bool reload = false;
    if (!t->behind) {
        if (pt < t->high) {
            t->behind = true;
            t->low = pt;
            t->behind_frames = 1;
            t->dip_frames = 1;
            return 0;
        }
    } else if (pt >= t->high) {
        /* A misread, not a reload. Play went on during the frozen frames, so judge the
         * jump from where it would be now. */
        t->behind = false;
        uint32_t resumed = t->prev_pt + t->dip_frames;
        t->prev_pt = resumed < pt ? resumed : pt;
    } else {
        if (t->dip_frames < 0xFFFFFFFFu) t->dip_frames++;
        /* Expected pt here is low + behind_frames. Allow brief pauses below that, but the
         * floor rises with the count, so a value stuck at one reading never settles. */
        uint32_t expect = t->low + t->behind_frames;
        uint32_t min_pt = expect > RUN_RELOAD_SLACK + 8 ? expect - RUN_RELOAD_SLACK - 8 : 0;
        if (min_pt < t->low) min_pt = t->low;
        if (pt < min_pt || pt > expect + RUN_RELOAD_SLACK) {
            t->low = pt;     /* not running on from low: count again from here */
            t->behind_frames = 1;
            return 0;
        }
        if (t->behind_frames < 255) t->behind_frames++;
        if (t->behind_frames < RUN_RELOAD_SETTLE) return 0;
        RunEvent* ev = &out[n++];
        new_event(ev, RUN_EV_RELOAD, t->high);
        ev->to_pt = t->low;
        t->behind = false;
        reload = true;
    }
    t->high = pt;

    /* R6 + R16: rentals, link, partner battles, Steven's mons and Teachy TV's Pokedude party. */
    bool swapped = nuz_party_swapped(&t->party_base, nf);
    bool gate = rf->facility || swapped;
    for (uint8_t i = 0; i < nf->party_count && i < NUZ_PARTY_MAX && !gate; i++)
        gate = t->partner_ot != 0 && nf->party[i].species != 0 &&
               nf->party[i].key.ot == t->partner_ot;

    bool disc = reload || !t->have_prev || pt > t->prev_pt + RUN_JUMP_FRAMES ||
                (t->was_facility && !gate) || nf->trainer_id != t->prev_tid;
    bool advanced = t->have_prev && pt > t->prev_pt;
    if (t->have_prev && rf->sb1 == t->prev_sb1 && rf->sb2 == t->prev_sb2) {
        if (t->sb_stable < 255) t->sb_stable++;
    } else {
        t->sb_stable = 0;
    }
    t->have_prev = true;
    t->prev_pt = pt;
    t->prev_tid = nf->trainer_id;
    t->prev_sb1 = rf->sb1;
    t->prev_sb2 = rf->sb2;
    t->was_facility = gate;

    if (disc) {
        /* On a reload, cut at low (the restored point, ~59 frames back): KOs and faints
         * after it were un-done by the reload, even though they had not been frozen yet. */
        rebaseline(t, nf, rf, reload ? t->low : pt);
        return n;
    }

    confirm(t, rf, advanced);
    bool done = track_milestones(t, log, nf, rf, pt, out, &n);

    if (gate) {
        t->in_battle = false;   /* nothing party-derived while gated (R6) */
    } else {
        track_faints(t, log, nf, pt, out, &n);
    }
    if (rf->whiteout && !t->prev_whiteout) {
        RunEvent ev;
        new_event(&ev, RUN_EV_WHITEOUT, pt);
        ev.sec = nf->mapsec;
        emit_edge(&ev, out, &n);
    }
    t->prev_whiteout = rf->whiteout;
    if (!gate) track_battle(t, log, nf, rf, pt, out, &n);
    t->prev_in_battle = nf->in_battle;

    /* Joins wait for the battle to end: a catch is in the party before the outcome is final. */
    if (!gate) {
        track_evolutions(t, log, nf, pt, out, &n);
        if (!nf->in_battle) {
            if (!track_joins(t, log, nf, nf->party, nf->party_count, pt, out, &n)) done = false;
            if (nf->has_boxes || t->box_rescan) {
                bool full = track_joins(t, log, nf, nf->boxes, nf->box_count, pt, out, &n);
                t->box_rescan = !full;
                if (full) t->boxes_fresh = true;
                else done = false;
            }
        }
    }

    if (t->resync && done && t->boxes_fresh && t->conf_fresh && t->sb_stable >= RUN_CONFIRM_FRAMES)
        t->resync = false;
    return n;
}
