#include "test_harness.h"
#include "game/runlog.h"

#define TID 0x0000BEEFu

/* Large: keep off the stack. */
static RunLog s_a, s_b, s_c;

static RunEvent mk(uint8_t type, uint32_t pt) {
    RunEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.pt = pt;
    e.sec = NUZ_SEC_NONE;
    e.low = 1000;
    return e;
}

static NuzKey key(uint32_t n) {
    NuzKey k;
    memset(&k, 0, sizeof(k));
    k.pid = 0x11110000u + n;
    k.ot = TID;
    return k;
}

static bool key_eq(NuzKey a, NuzKey b) {
    return a.pid == b.pid && a.ot == b.ot && a.variant == b.variant;
}

static void apply(RunLog* log, RunEvent e) { run_apply(log, &e); }

static RunEvent join(uint32_t pt, NuzKey k, uint16_t sp, uint8_t how) {
    RunEvent e = mk(RUN_EV_JOIN, pt);
    e.key = k;
    e.species = sp;
    e.level = 5;
    e.sec = 16;
    e.how = how;
    e.ball = 4;
    return e;
}

static RunEvent battle(uint32_t pt, uint8_t kind, uint16_t foe) {
    RunEvent e = mk(RUN_EV_BATTLE, pt);
    e.kind = kind;
    e.foe = foe;
    e.outcome = 1;
    if (kind == RUN_BATTLE_TRAINER) e.trainer = 265;
    return e;
}

static void add_ko(RunEvent* e, NuzKey k, uint16_t count, uint8_t level) {
    e->kos[e->ko_count].key = k;
    e->kos[e->ko_count].count = count;
    e->kos[e->ko_count].level = level;
    e->ko_count++;
}

/* Every field except seq (set by run_apply). */
static bool ev_eq(const RunEvent* a, const RunEvent* b) {
    if (a->type != b->type || a->pt != b->pt || a->untimed != b->untimed || !key_eq(a->key, b->key) ||
        a->species != b->species || a->from != b->from || a->level != b->level || a->sec != b->sec ||
        a->how != b->how || a->ball != b->ball || a->shiny != b->shiny || a->newdex != b->newdex ||
        memcmp(a->nick, b->nick, sizeof(a->nick)) != 0 || a->foe != b->foe || a->id != b->id ||
        a->team_max != b->team_max || a->team_avg != b->team_avg || a->dex != b->dex ||
        a->steps != b->steps || a->kind != b->kind || a->trainer != b->trainer ||
        a->outcome != b->outcome || a->ko_count != b->ko_count || a->indirect != b->indirect ||
        a->upset != b->upset || a->low != b->low || !key_eq(a->low_by, b->low_by) || a->faints != b->faints || a->has_ace != b->has_ace ||
        !key_eq(a->ace_by, b->ace_by) || a->ace_species != b->ace_species || a->tid != b->tid ||
        a->to_pt != b->to_pt)
        return false;
    for (uint8_t i = 0; i < a->ko_count && i < RUN_KO_SLOTS; i++)
        if (!key_eq(a->kos[i].key, b->kos[i].key) || a->kos[i].count != b->kos[i].count ||
            a->kos[i].level != b->kos[i].level)
            return false;
    return true;
}

static bool s_ignore_level;

static bool member_eq(const RunMember* a, const RunMember* b) {
    return key_eq(a->key, b->key) && a->species == b->species && a->join_species == b->join_species &&
           a->join_level == b->join_level && (s_ignore_level || a->level == b->level) && a->sec == b->sec && a->how == b->how &&
           a->ball == b->ball && a->shiny == b->shiny && a->live == b->live && a->join_pt == b->join_pt &&
           a->kos == b->kos && a->faints == b->faints && a->evo_species == b->evo_species &&
           a->evo_pt == b->evo_pt && memcmp(a->nick, b->nick, sizeof(a->nick)) == 0;
}

/* Aggregates and lists, field by field (no memcmp over padded structs). */
static bool log_eq(const RunLog* a, const RunLog* b) {
    if (a->active != b->active || a->tid != b->tid || a->start_pt != b->start_pt ||
        a->started_new_game != b->started_new_game || a->ev_count != b->ev_count ||
        a->bt_count != b->bt_count || a->member_count != b->member_count || a->faints != b->faints ||
        a->whiteouts != b->whiteouts || a->upsets != b->upsets || a->shinies_seen != b->shinies_seen ||
        a->shinies_caught != b->shinies_caught || a->indirect != b->indirect ||
        a->low_permille != b->low_permille || !key_eq(a->low_key, b->low_key) || a->low_foe != b->low_foe ||
        a->folded_wild != b->folded_wild || a->folded_ko_count != b->folded_ko_count ||
        a->folded_low != b->folded_low || a->folded_low_foe != b->folded_low_foe ||
        !key_eq(a->folded_low_by, b->folded_low_by) || a->folded_low_seq != b->folded_low_seq ||
        a->events_full != b->events_full || a->battles_full != b->battles_full)
        return false;
    if (memcmp(a->orphan, b->orphan, sizeof(a->orphan)) || memcmp(a->orphan_pt, b->orphan_pt, sizeof(a->orphan_pt)) ||
        memcmp(a->badge, b->badge, sizeof(a->badge)) || memcmp(a->badge_untimed, b->badge_untimed, sizeof(a->badge_untimed)) ||
        memcmp(a->badge_pt, b->badge_pt, sizeof(a->badge_pt)) || memcmp(a->story, b->story, sizeof(a->story)) ||
        memcmp(a->story_untimed, b->story_untimed, sizeof(a->story_untimed)) ||
        memcmp(a->story_pt, b->story_pt, sizeof(a->story_pt)) ||
        memcmp(a->wild_count, b->wild_count, sizeof(a->wild_count)))
        return false;
    for (uint16_t i = 0; i < a->member_count; i++)
        if (!member_eq(&a->members[i], &b->members[i])) return false;
    for (uint16_t i = 0; i < a->folded_ko_count; i++)
        if (!key_eq(a->folded_kos[i].key, b->folded_kos[i].key) || a->folded_kos[i].count != b->folded_kos[i].count ||
            a->folded_kos[i].level != b->folded_kos[i].level)
            return false;
    for (uint16_t i = 0; i < a->ev_count; i++)
        if (!ev_eq(&a->ev[i], &b->ev[i]) || a->ev[i].seq != b->ev[i].seq) return false;
    for (uint16_t i = 0; i < a->bt_count; i++)
        if (!ev_eq(&a->bt[i], &b->bt[i]) || a->bt[i].seq != b->bt[i].seq) return false;
    return true;
}

static bool roundtrip(const RunEvent* e) {
    char line[RUN_LINE_MAX];
    RunEvent p;
    int n = run_format(e, line, sizeof(line));
    if (n <= 0 || !run_parse(line, &p)) {
        printf("\n    line: %s\n", n > 0 ? line : "(format failed)");
        return false;
    }
    if (!ev_eq(e, &p)) printf("\n    mismatch: %s\n", line);
    return ev_eq(e, &p);
}

TEST(runlog_format_parse_roundtrip) {
    RunEvent e = mk(RUN_EV_START, 123456);
    e.tid = 0xDEADBEEFu;
    ASSERT_TRUE(roundtrip(&e));

    e = join(4000000000u, key(1), 411, RUN_HOW_TRADED);
    e.key.variant = 1;
    e.level = 100;
    e.sec = 0xFE;
    e.ball = 12;
    e.shiny = e.newdex = e.untimed = true;
    for (int i = 0; i < 10; i++) e.nick[i] = (uint8_t)(0xA0 + i * 7);
    e.nick[9] = 0xFF;
    ASSERT_TRUE(roundtrip(&e));

    e = mk(RUN_EV_EVOLVE, 77);
    e.key = key(2);
    e.species = 281;
    e.from = 280;
    e.level = 16;
    ASSERT_TRUE(roundtrip(&e));

    e = mk(RUN_EV_FAINT, 78);
    e.key = key(3);
    e.species = 255;
    e.level = 9;
    e.foe = 286;
    e.sec = 17;
    ASSERT_TRUE(roundtrip(&e));

    e = mk(RUN_EV_WHITEOUT, 79);
    e.sec = 18;
    ASSERT_TRUE(roundtrip(&e));

    e = mk(RUN_EV_BADGE, 80);
    e.id = 8;
    e.team_max = 46;
    e.team_avg = 41;
    e.dex = 202;
    e.steps = 123456789u;
    e.untimed = true;
    ASSERT_TRUE(roundtrip(&e));

    e = mk(RUN_EV_STORY, 81);
    e.id = RUN_STORY_NATDEX;
    e.untimed = true;
    ASSERT_TRUE(roundtrip(&e));

    e = battle(4294967295u, RUN_BATTLE_TRAINER, 65535);
    e.trainer = 335;
    e.shiny = true;
    e.outcome = 255;
    for (uint32_t i = 0; i < RUN_KO_SLOTS; i++) {
        NuzKey k = key(10 + i);
        k.pid = 0xFFFFFFF0u + i;
        k.ot = 0xFFFFFFFFu;
        k.variant = (uint8_t)(i & 1);
        add_ko(&e, k, (uint16_t)(65530 + i), (uint8_t)(100 - i));
    }
    e.indirect = 255;
    e.upset = 255;
    e.low = 999;
    e.low_by = key(13);
    e.low_by.variant = 1;
    e.faints = 6;
    e.has_ace = true;
    e.ace_by = key(12);
    e.ace_by.variant = 1;
    e.ace_species = 65535;
    ASSERT_TRUE(roundtrip(&e));

    e = battle(5, RUN_BATTLE_OTHER, 0);  /* no kos, no ace */
    e.low = 1000;
    ASSERT_TRUE(roundtrip(&e));

    e = mk(RUN_EV_RELOAD, 900);
    e.to_pt = 450;
    ASSERT_TRUE(roundtrip(&e));

    /* Too small a buffer. */
    char tiny[8];
    ASSERT_EQ(run_format(&e, tiny, sizeof(tiny)), -1);

    RunEvent p;
    ASSERT_TRUE(!run_parse("", &p));
    ASSERT_TRUE(!run_parse("\n", &p));
    ASSERT_TRUE(!run_parse("bogus pt=1", &p));
    ASSERT_TRUE(!run_parse("badge id=1", &p));
    ASSERT_TRUE(!run_parse("badge pt=12x id=1", &p));
    ASSERT_TRUE(!run_parse("badge pt=-1 id=1", &p));
    ASSERT_TRUE(!run_parse("join pt=1 lv=999", &p));
    ASSERT_TRUE(!run_parse("join pt=1 how=stolen", &p));
    ASSERT_TRUE(!run_parse("join pt=1 nick=00", &p));
    ASSERT_TRUE(!run_parse("battle pt=1 ko=1:2", &p));
    ASSERT_TRUE(!run_parse("battle pt=1 ko=1:2:0:1:1,1:2:0:1:1,1:2:0:1:1,1:2:0:1:1,1:2:0:1:1,1:2:0:1:1,1:2:0:1:1", &p));
    ASSERT_TRUE(!run_parse("whiteout pt=1 sec", &p));
    ASSERT_TRUE(!run_parse("badge pt=99999999999 id=1", &p));
    ASSERT_TRUE(!run_parse("badge pt=4294967296 id=1", &p));
    ASSERT_TRUE(!run_parse("start pt=1 tid=1FFFFFFFF", &p));
    ASSERT_TRUE(!run_parse("battle pt=1 lowby=1:2", &p));
    ASSERT_TRUE(run_parse("whiteout pt=5 sec=3 zz=9\r\n", &p));
    ASSERT_EQ(p.type, RUN_EV_WHITEOUT);
    ASSERT_EQ(p.pt, 5);
    ASSERT_EQ(p.sec, 3);
}

TEST(runlog_join_creates_member) {
    run_log_init(&s_a);
    ASSERT_EQ(s_a.low_permille, 1000);
    apply(&s_a, mk(RUN_EV_START, 100));
    ASSERT_TRUE(s_a.active);
    ASSERT_TRUE(s_a.started_new_game);
    RunEvent e = join(200, key(1), 252, RUN_HOW_CAUGHT);
    e.shiny = true;
    e.nick[0] = 0xBB;
    apply(&s_a, e);
    ASSERT_EQ(s_a.member_count, 1);
    ASSERT_EQ(run_find_member(&s_a, key(1)), 0);
    ASSERT_EQ(run_find_member(&s_a, key(2)), -1);
    const RunMember* m = &s_a.members[0];
    ASSERT_EQ(m->species, 252);
    ASSERT_EQ(m->join_species, 252);
    ASSERT_EQ(m->join_level, 5);
    ASSERT_EQ(m->level, 5);
    ASSERT_EQ(m->sec, 16);
    ASSERT_EQ(m->how, RUN_HOW_CAUGHT);
    ASSERT_EQ(m->ball, 4);
    ASSERT_TRUE(m->shiny);
    ASSERT_TRUE(m->live);
    ASSERT_EQ(m->join_pt, 200);
    ASSERT_EQ(m->nick[0], 0xBB);
    ASSERT_EQ(s_a.shinies_caught, 1);
    /* A backfilled member is not live and the run did not start at a new game. */
    apply(&s_a, join(300, key(2), 25, RUN_HOW_BACKFILL));
    ASSERT_TRUE(!s_a.members[1].live);
    ASSERT_TRUE(!s_a.started_new_game);
}

TEST(runlog_join_dedupes) {
    run_log_init(&s_a);
    apply(&s_a, mk(RUN_EV_START, 100));
    apply(&s_a, join(200, key(1), 252, RUN_HOW_CAUGHT));
    apply(&s_a, join(300, key(1), 300, RUN_HOW_GIFT));
    ASSERT_EQ(s_a.member_count, 1);
    ASSERT_EQ(s_a.members[0].species, 252);
    ASSERT_EQ(s_a.members[0].join_pt, 200);
}

TEST(runlog_evolve_updates_member) {
    run_log_init(&s_a);
    apply(&s_a, join(200, key(1), 252, RUN_HOW_GIFT));
    RunEvent e = mk(RUN_EV_EVOLVE, 900);
    e.key = key(1);
    e.species = 253;
    e.from = 252;
    e.level = 16;
    apply(&s_a, e);
    const RunMember* m = &s_a.members[0];
    ASSERT_EQ(m->species, 253);
    ASSERT_EQ(m->join_species, 252);
    ASSERT_EQ(m->evo_species, 253);
    ASSERT_EQ(m->evo_pt, 900);
    ASSERT_EQ(m->level, 16);
    ASSERT_EQ(m->join_level, 5);
}

TEST(runlog_battle_credits_kos) {
    run_log_init(&s_a);
    apply(&s_a, join(10, key(1), 252, RUN_HOW_GIFT));
    apply(&s_a, join(20, key(2), 263, RUN_HOW_CAUGHT));
    RunEvent b = battle(100, RUN_BATTLE_WILD, 261);
    b.shiny = true;
    add_ko(&b, key(1), 2, 12);
    add_ko(&b, key(2), 1, 9);
    b.upset = 11;
    b.low = 50;
    b.low_by = key(2);
    b.indirect = 1;
    apply(&s_a, b);
    b = battle(200, RUN_BATTLE_TRAINER, 74);
    add_ko(&b, key(1), 1, 13);
    b.low = 30;
    b.low_by = key(1);
    b.indirect = 2;
    apply(&s_a, b);
    b = battle(300, RUN_BATTLE_WILD, 261);
    b.low = 40;  /* not better */
    apply(&s_a, b);
    ASSERT_EQ(s_a.bt_count, 3);
    ASSERT_EQ(s_a.members[0].kos, 3);
    ASSERT_EQ(s_a.members[0].level, 13);
    ASSERT_EQ(s_a.members[1].kos, 1);
    ASSERT_EQ(s_a.members[1].level, 9);
    ASSERT_EQ(s_a.upsets, 1);
    ASSERT_EQ(s_a.shinies_seen, 1);
    ASSERT_EQ(s_a.indirect, 3);
    ASSERT_EQ(s_a.wild_count[261], 2);
    ASSERT_EQ(s_a.wild_count[74], 0);
    ASSERT_EQ(s_a.low_permille, 30);
    ASSERT_EQ(s_a.low_foe, 74);
    ASSERT_TRUE(key_eq(s_a.low_key, key(1)));
}

TEST(runlog_reload_truncates) {
    run_log_init(&s_a);
    apply(&s_a, mk(RUN_EV_START, 50));
    apply(&s_a, join(100, key(1), 252, RUN_HOW_GIFT));
    RunEvent e = mk(RUN_EV_BADGE, 200);
    e.id = 1;
    apply(&s_a, e);
    e = mk(RUN_EV_FAINT, 300);
    e.key = key(1);
    apply(&s_a, e);
    ASSERT_TRUE(s_a.badge[0]);
    ASSERT_EQ(s_a.faints, 1);
    ASSERT_EQ(s_a.members[0].faints, 1);
    e = mk(RUN_EV_RELOAD, 400);
    e.to_pt = 150;
    apply(&s_a, e);
    ASSERT_EQ(s_a.ev_count, 2);
    ASSERT_EQ(s_a.member_count, 1);
    ASSERT_TRUE(!s_a.badge[0]);
    ASSERT_TRUE(s_a.orphan[0]);
    ASSERT_EQ(s_a.orphan_pt[0], 200);
    ASSERT_EQ(s_a.faints, 0);
    ASSERT_EQ(s_a.members[0].faints, 0);
    ASSERT_TRUE(s_a.active);
}

TEST(runlog_reload_keeps_immune) {
    run_log_init(&s_a);
    apply(&s_a, mk(RUN_EV_START, 500));
    apply(&s_a, join(500, key(1), 252, RUN_HOW_BACKFILL));
    RunEvent e = mk(RUN_EV_BADGE, 500);
    e.id = 3;
    e.untimed = true;
    apply(&s_a, e);
    e = mk(RUN_EV_STORY, 600);
    e.id = RUN_STORY_SURF;
    apply(&s_a, e);
    e = mk(RUN_EV_RELOAD, 700);
    e.to_pt = 100;
    apply(&s_a, e);
    ASSERT_EQ(s_a.ev_count, 3);
    ASSERT_TRUE(s_a.active);
    ASSERT_EQ(s_a.start_pt, 500);
    ASSERT_EQ(s_a.member_count, 1);
    ASSERT_TRUE(s_a.badge[2]);
    ASSERT_TRUE(s_a.badge_untimed[2]);
    ASSERT_TRUE(!s_a.orphan[2]);
    ASSERT_TRUE(!s_a.story[RUN_STORY_SURF]);
    ASSERT_TRUE(s_a.orphan[RUN_BADGES + RUN_STORY_SURF]);
    ASSERT_EQ(s_a.orphan_pt[RUN_BADGES + RUN_STORY_SURF], 600);
    ASSERT_TRUE(!s_a.started_new_game);
}

/* Deterministic stream generator (LCG). */
static uint32_t s_rng;
static uint32_t rnd(uint32_t n) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) % n;
}

/* battle_heavy: mostly battles and only short reloads, so the battle list fills and folds. */
static RunEvent gen_event(uint32_t* pt, bool battle_heavy) {
    uint32_t r = rnd(100);
    if (battle_heavy) {
        uint32_t x = rnd(1000);
        r = x < 5 ? 0 : x < 150 ? 4 + rnd(48) : 52 + rnd(48);
    }
    *pt += rnd(400);
    RunEvent e;
    if (r < 4) {
        e = mk(RUN_EV_RELOAD, *pt);
        if (battle_heavy)
            e.to_pt = *pt > 2000 ? *pt - rnd(2000) : 0;
        else
            e.to_pt = *pt > 0 ? rnd(*pt) : 0;
        *pt = e.to_pt + rnd(100);
    } else if (r < 18) {
        e = join(*pt, key(rnd(40)), (uint16_t)(1 + rnd(411)), (uint8_t)rnd(RUN_HOW_COUNT));
        e.shiny = rnd(10) == 0;
        e.newdex = rnd(2) == 0;
        e.untimed = rnd(8) == 0;
        e.key.variant = (uint8_t)(rnd(10) == 0);
        e.nick[rnd(10)] = (uint8_t)rnd(256);
    } else if (r < 26) {
        e = mk(RUN_EV_EVOLVE, *pt);
        e.key = key(rnd(40));
        e.from = (uint16_t)(1 + rnd(411));
        e.species = (uint16_t)(1 + rnd(411));
        e.level = (uint8_t)(1 + rnd(100));
    } else if (r < 36) {
        e = mk(RUN_EV_FAINT, *pt);
        e.key = key(rnd(40));
        e.species = (uint16_t)(1 + rnd(411));
        e.level = (uint8_t)(1 + rnd(100));
        e.foe = (uint16_t)rnd(412);
        e.sec = (uint8_t)rnd(213);
    } else if (r < 40) {
        e = mk(RUN_EV_WHITEOUT, *pt);
        e.sec = (uint8_t)rnd(213);
    } else if (r < 46) {
        e = mk(RUN_EV_BADGE, *pt);
        e.id = (uint8_t)(1 + rnd(RUN_BADGES));
        e.untimed = rnd(5) == 0;
        e.team_max = (uint8_t)rnd(100);
        e.team_avg = (uint8_t)rnd(100);
        e.dex = (uint16_t)rnd(386);
        e.steps = rnd(1000000);
    } else if (r < 52) {
        e = mk(RUN_EV_STORY, *pt);
        e.id = (uint8_t)rnd(RUN_STORY_COUNT);
        e.untimed = rnd(5) == 0;
    } else {
        e = battle(*pt, (uint8_t)rnd(3), (uint16_t)rnd(412));
        if (e.kind == RUN_BATTLE_TRAINER) e.trainer = (uint16_t)(1 + rnd(854));
        e.shiny = rnd(20) == 0;
        e.outcome = (uint8_t)(1 + rnd(7));
        uint32_t kos = rnd(RUN_KO_SLOTS + 1);
        for (uint32_t i = 0; i < kos; i++) add_ko(&e, key(rnd(40)), (uint8_t)(1 + rnd(3)), (uint8_t)(1 + rnd(100)));
        e.indirect = (uint8_t)rnd(3);
        e.upset = rnd(10) == 0 ? (uint8_t)(10 + rnd(20)) : 0;
        e.low = (uint16_t)(rnd(4) == 0 ? rnd(1000) : 1000);
        /* Battle-heavy: closest calls get worse over time, so the best one sits in the oldest
         * battles, which are the ones folded. */
        if (battle_heavy && e.low < 1000 && e.kind == RUN_BATTLE_WILD)
            e.low = (uint16_t)(*pt / 2500 + rnd(20) > 999 ? 999 : *pt / 2500 + rnd(20));
        else if (battle_heavy)
            e.low = 1000;
        if (e.low < 1000) e.low_by = key(rnd(40));
        e.faints = (uint8_t)rnd(3);
        if (e.kind == RUN_BATTLE_TRAINER && rnd(4) == 0) {
            e.has_ace = true;
            e.ace_by = key(rnd(40));
            e.ace_species = (uint16_t)(1 + rnd(411));
        }
    }
    return e;
}

TEST(runlog_replay_equals_live) {
    run_log_init(&s_a);
    run_log_init(&s_b);
    s_rng = 12345;
    uint32_t pt = 0, reloads = 0;
    char line[RUN_LINE_MAX];
    RunEvent e = mk(RUN_EV_START, 0);
    e.tid = TID;
    for (int i = 0; i < 3000; i++) {
        if (i > 0) e = gen_event(&pt, false);
        reloads += e.type == RUN_EV_RELOAD;
        run_apply(&s_a, &e);
        RunEvent p;
        ASSERT_TRUE(run_format(&e, line, sizeof(line)) > 0);
        ASSERT_TRUE(run_parse(line, &p));
        run_apply(&s_b, &p);
    }
    ASSERT_TRUE(reloads > 50);
    ASSERT_TRUE(s_a.member_count > 0);
    ASSERT_TRUE(s_a.bt_count > 0);
    ASSERT_EQ(s_a.ev_count, s_b.ev_count);
    ASSERT_EQ(s_a.bt_count, s_b.bt_count);
    ASSERT_EQ(s_a.next_seq, s_b.next_seq);
    ASSERT_EQ(s_a.version, s_b.version);
    ASSERT_TRUE(log_eq(&s_a, &s_b));
    /* One code path: a reload that drops nothing rebuilds to the same aggregates. */
    memcpy(&s_c, &s_a, sizeof(s_c));
    e = mk(RUN_EV_RELOAD, pt);
    e.to_pt = 0xFFFFFFFFu;
    run_apply(&s_c, &e);
    ASSERT_TRUE(log_eq(&s_a, &s_c));
}

/* Long battle-heavy stream: folding and full lists, then replay and rebuild checks. */
TEST(runlog_replay_with_folding) {
    run_log_init(&s_a);
    run_log_init(&s_b);
    s_rng = 777;
    uint32_t pt = 0, reloads = 0, battles = 0;
    char line[RUN_LINE_MAX];
    RunEvent e = mk(RUN_EV_START, 0);
    e.tid = TID;
    for (int i = 0; i < 10000; i++) {
        if (i > 0) e = gen_event(&pt, true);
        reloads += e.type == RUN_EV_RELOAD;
        battles += e.type == RUN_EV_BATTLE;
        run_apply(&s_a, &e);
        RunEvent p;
        ASSERT_TRUE(run_format(&e, line, sizeof(line)) > 0);
        ASSERT_TRUE(run_parse(line, &p));
        run_apply(&s_b, &p);
        /* After folds start, a reload that drops nothing must rebuild to the same state. */
        if (s_a.folded_wild > 0 && i % 250 == 0) {
            memcpy(&s_c, &s_a, sizeof(s_c));
            RunEvent r = mk(RUN_EV_RELOAD, pt);
            r.to_pt = 0xFFFFFFFFu;
            run_apply(&s_c, &r);
            s_c.next_seq = s_a.next_seq;
            s_c.version = s_a.version;
            s_ignore_level = true;  /* folded KO levels are approximate after a rebuild (documented) */
            bool same = log_eq(&s_a, &s_c);
            s_ignore_level = false;
            if (!same) printf("\n    rebuild differs at event %d\n", i);
            ASSERT_TRUE(same);
        }
    }
    ASSERT_TRUE(reloads > 10);
    ASSERT_TRUE(battles > RUN_BATTLES_MAX);
    ASSERT_TRUE(s_a.folded_wild > 100);
    ASSERT_EQ(s_a.bt_count, RUN_BATTLES_MAX);
    ASSERT_TRUE(log_eq(&s_a, &s_b));
}

/* Out-of-range fields are normalised on apply and format, so live == replay. */
TEST(runlog_apply_normalizes) {
    run_log_init(&s_a);
    run_log_init(&s_b);
    RunEvent ev[3];
    ev[0] = join(10, key(1), 252, 9);
    ev[0].key.variant = 3;
    ev[1] = battle(20, 7, 10);
    ev[1].ko_count = 9;
    for (int i = 0; i < RUN_KO_SLOTS; i++) {
        ev[1].kos[i].key = key(1);
        ev[1].kos[i].key.variant = 3;
        ev[1].kos[i].count = 1;
        ev[1].kos[i].level = 7;
    }
    ev[1].ace_by = key(2);
    ev[1].ace_species = 99;
    ev[1].low = 10;
    ev[1].low_by = key(1);
    ev[1].low_by.variant = 5;
    ev[2] = battle(30, RUN_BATTLE_TRAINER, 11);
    ev[2].has_ace = true;
    ev[2].ace_by = key(1);
    ev[2].ace_by.variant = 2;
    char line[RUN_LINE_MAX];
    for (int i = 0; i < 3; i++) {
        RunEvent p;
        run_apply(&s_a, &ev[i]);
        ASSERT_TRUE(run_format(&ev[i], line, sizeof(line)) > 0);
        ASSERT_TRUE(run_parse(line, &p));
        run_apply(&s_b, &p);
    }
    ASSERT_EQ(s_a.ev[0].how, RUN_HOW_GIFT);
    ASSERT_EQ(s_a.members[0].key.variant, 1);
    ASSERT_EQ(s_a.bt[0].kind, RUN_BATTLE_OTHER);
    ASSERT_EQ(s_a.bt[0].ko_count, RUN_KO_SLOTS);
    ASSERT_EQ(s_a.bt[0].ace_by.pid, 0);
    ASSERT_EQ(s_a.bt[0].ace_species, 0);
    ASSERT_EQ(s_a.bt[1].ace_by.variant, 1);
    ASSERT_EQ(s_a.members[0].kos, RUN_KO_SLOTS);
    ASSERT_EQ(s_a.low_key.variant, 1);
    ASSERT_TRUE(log_eq(&s_a, &s_b));
}

TEST(runlog_fold_oldest_wild) {
    run_log_init(&s_a);
    apply(&s_a, mk(RUN_EV_START, 0));
    apply(&s_a, join(0, key(1), 252, RUN_HOW_BACKFILL));
    RunEvent b = battle(1, RUN_BATTLE_TRAINER, 74);  /* oldest, but not wild: never folded */
    apply(&s_a, b);
    for (uint32_t i = 1; i < RUN_BATTLES_MAX; i++) {
        b = battle(1 + i, RUN_BATTLE_WILD, 10);
        add_ko(&b, key(1), 1, 20);
        b.shiny = i == 1;
        apply(&s_a, b);
    }
    ASSERT_EQ(s_a.bt_count, RUN_BATTLES_MAX);
    ASSERT_EQ(s_a.members[0].kos, RUN_BATTLES_MAX - 1);
    b = battle(RUN_BATTLES_MAX + 1, RUN_BATTLE_WILD, 11);
    add_ko(&b, key(1), 1, 21);
    apply(&s_a, b);
    ASSERT_EQ(s_a.bt_count, RUN_BATTLES_MAX);
    ASSERT_EQ(s_a.folded_wild, 1);
    ASSERT_TRUE(!s_a.battles_full);
    ASSERT_EQ(s_a.members[0].kos, RUN_BATTLES_MAX);
    ASSERT_EQ(s_a.bt[0].foe, 74);
    ASSERT_EQ(s_a.bt[1].pt, 3);  /* the battle at pt 2 was folded */
    ASSERT_EQ(s_a.bt[RUN_BATTLES_MAX - 1].foe, 11);
    ASSERT_EQ(s_a.wild_count[10], RUN_BATTLES_MAX - 1);
    ASSERT_EQ(s_a.wild_count[11], 1);
    ASSERT_EQ(s_a.shinies_seen, 1);

    RunEvent r = mk(RUN_EV_RELOAD, RUN_BATTLES_MAX + 2);
    r.to_pt = 0;
    apply(&s_a, r);
    ASSERT_EQ(s_a.bt_count, 0);
    ASSERT_EQ(s_a.member_count, 1);
    ASSERT_EQ(s_a.folded_wild, 1);
    ASSERT_EQ(s_a.members[0].kos, 1);
    ASSERT_EQ(s_a.wild_count[10], 1);
    ASSERT_EQ(s_a.wild_count[11], 0);
    ASSERT_EQ(s_a.shinies_seen, 1);  /* the folded battle's shiny */

    /* No wild battle to fold: the new battle is dropped. */
    run_log_init(&s_a);
    for (uint32_t i = 0; i < RUN_BATTLES_MAX; i++) apply(&s_a, battle(i, RUN_BATTLE_TRAINER, 74));
    ASSERT_TRUE(s_a.battles_full);  /* derived: full and nothing left to fold */
    apply(&s_a, battle(RUN_BATTLES_MAX, RUN_BATTLE_TRAINER, 75));
    ASSERT_EQ(s_a.bt_count, RUN_BATTLES_MAX);
    ASSERT_TRUE(s_a.battles_full);
    ASSERT_EQ(s_a.bt[RUN_BATTLES_MAX - 1].foe, 74);
    r = mk(RUN_EV_RELOAD, RUN_BATTLES_MAX);
    r.to_pt = 10;
    apply(&s_a, r);
    ASSERT_EQ(s_a.bt_count, 11);
    ASSERT_TRUE(!s_a.battles_full);
}

TEST(runlog_events_full) {
    run_log_init(&s_a);
    apply(&s_a, mk(RUN_EV_START, 0));
    for (uint32_t i = 1; i < RUN_EVENTS_MAX; i++) apply(&s_a, mk(RUN_EV_WHITEOUT, i));
    ASSERT_EQ(s_a.ev_count, RUN_EVENTS_MAX);
    ASSERT_TRUE(s_a.events_full);
    ASSERT_EQ(s_a.whiteouts, RUN_EVENTS_MAX - 1);
    apply(&s_a, join(RUN_EVENTS_MAX, key(1), 252, RUN_HOW_GIFT));
    ASSERT_EQ(s_a.member_count, 0);
    ASSERT_EQ(s_a.ev_count, RUN_EVENTS_MAX);
    RunEvent r = mk(RUN_EV_RELOAD, RUN_EVENTS_MAX + 1);
    r.to_pt = 10;
    apply(&s_a, r);
    ASSERT_EQ(s_a.ev_count, 11);
    ASSERT_EQ(s_a.whiteouts, 10);
    ASSERT_TRUE(!s_a.events_full);
    apply(&s_a, join(20, key(1), 252, RUN_HOW_GIFT));
    ASSERT_EQ(s_a.member_count, 1);
}

TEST(runlog_seq_order) {
    run_log_init(&s_a);
    apply(&s_a, mk(RUN_EV_START, 0));
    apply(&s_a, battle(5, RUN_BATTLE_WILD, 10));
    apply(&s_a, join(6, key(1), 252, RUN_HOW_GIFT));
    RunEvent r = mk(RUN_EV_RELOAD, 7);
    r.to_pt = 6;
    apply(&s_a, r);
    apply(&s_a, mk(RUN_EV_WHITEOUT, 8));
    ASSERT_EQ(s_a.ev[0].seq, 0);
    ASSERT_EQ(s_a.bt[0].seq, 1);
    ASSERT_EQ(s_a.ev[1].seq, 2);
    ASSERT_EQ(s_a.ev[2].seq, 4);
    ASSERT_EQ(s_a.next_seq, 5);
    ASSERT_EQ(s_a.version, 5);
}

void run_runlog_tests(void) {
    RUN_TEST(runlog_format_parse_roundtrip);
    RUN_TEST(runlog_join_creates_member);
    RUN_TEST(runlog_join_dedupes);
    RUN_TEST(runlog_evolve_updates_member);
    RUN_TEST(runlog_battle_credits_kos);
    RUN_TEST(runlog_reload_truncates);
    RUN_TEST(runlog_reload_keeps_immune);
    RUN_TEST(runlog_replay_equals_live);
    RUN_TEST(runlog_replay_with_folding);
    RUN_TEST(runlog_apply_normalizes);
    RUN_TEST(runlog_fold_oldest_wild);
    RUN_TEST(runlog_events_full);
    RUN_TEST(runlog_seq_order);
}
