#include "test_harness.h"
#include "game/run_track.h"

#define TID     0x0000BEEFu
#define SB1     0x02025A00u
#define SB2     0x02024A00u
#define STEPS   500u
#define WON     1
#define CAUGHT  7
#define B_DOUBLE  (1u << 0)
#define B_SAFARI  (1u << 7)
#define B_PARTNER (1u << 22)

/* Large: keep off the stack. */
static RunLog s_log, s_replay;
static RunTracker s_t;
static NuzFacts s_nf;
static RunFacts s_rf;
static RunEvent s_out[RUN_EVENTS_FRAME_MAX];
static RunEvent s_all[4096];
static uint16_t s_all_n, s_mark;

static NuzKey key(uint32_t pid) {
    NuzKey k;
    memset(&k, 0, sizeof(k));
    k.pid = pid;
    k.ot = TID;
    return k;
}

static bool key_eq(NuzKey a, NuzKey b) {
    return a.pid == b.pid && a.ot == b.ot && a.variant == b.variant;
}

static void fill_mon(NuzMon* m, uint32_t pid, uint16_t species, uint16_t hp) {
    memset(m, 0, sizeof(*m));
    m->key = key(pid);
    m->species = species;
    m->level = 10;
    m->met = 16;
    m->met_level = 5;
    m->ball = 4;
    m->hp = hp;
}

static NuzMon* party_mon(uint8_t slot, uint32_t pid, uint16_t species, uint16_t hp) {
    fill_mon(&s_nf.party[slot], pid, species, hp);
    if (s_nf.party_count <= slot) s_nf.party_count = (uint8_t)(slot + 1);
    return &s_nf.party[slot];
}

/* One frame at the current play time; returned events are applied and recorded. */
static void frame(void) {
    uint8_t n = run_track_step(&s_t, &s_log, &s_nf, &s_rf, s_out);
    for (uint8_t i = 0; i < n; i++) {
        run_apply(&s_log, &s_out[i]);
        if (s_all_n < 4096) s_all[s_all_n++] = s_out[i];
    }
}

static void step_n(uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) {
        s_nf.play_time++;
        frame();
    }
}

/* Next frame happens at play time pt. */
static void goto_pt(uint32_t pt) {
    s_nf.play_time = pt - 1;
}

static void mark(void) { s_mark = s_all_n; }

static int count(uint8_t type) {
    int c = 0;
    for (uint16_t i = s_mark; i < s_all_n; i++)
        if (s_all[i].type == type) c++;
    return c;
}

static const RunEvent* find(uint8_t type, int nth) {
    for (uint16_t i = s_mark; i < s_all_n; i++)
        if (s_all[i].type == type && nth-- == 0) return &s_all[i];
    return NULL;
}

static int index_of(uint8_t type) {
    for (uint16_t i = s_mark; i < s_all_n; i++)
        if (s_all[i].type == type) return i;
    return -1;
}

static void setup(void) {
    memset(&s_nf, 0, sizeof(s_nf));
    memset(&s_rf, 0, sizeof(s_rf));
    run_log_init(&s_log);
    run_tracker_reset(&s_t);
    s_all_n = s_mark = 0;
    s_nf.valid = true;
    s_nf.trainer_id = TID;
    s_nf.play_time = 100;
    s_nf.mapsec = 16;
    s_nf.has_boxes = true;
    s_rf.valid = true;
    s_rf.sb1 = SB1;
    s_rf.sb2 = SB2;
    s_rf.stats[RUN_STAT_STEPS] = STEPS;
    RunEvent start;
    memset(&start, 0, sizeof(start));
    start.type = RUN_EV_START;
    start.pt = 101;
    start.tid = TID;
    start.low = 1000;
    run_apply(&s_log, &start);
}

/* Setup plus one member (slot 0, pid 1) attached and the resync window over. */
static void attached(void) {
    setup();
    party_mon(0, 1, 280, 30);
    step_n(20);
    mark();
}

static void enter_battle(uint16_t species, uint32_t pid) {
    s_nf.in_battle = true;
    s_nf.enemy_species = species;
    s_nf.enemy_level = 5;
    s_nf.enemy_pid = pid;
    s_nf.enemy_ot = TID;
    s_rf.battlers = 2;
    s_rf.b[0] = (RunBattler){ .hp = 30, .max_hp = 40, .species = 280, .level = 10, .pos = 0, .party_index = 0 };
    s_rf.b[1] = (RunBattler){ .hp = 20, .max_hp = 20, .species = species, .level = 5, .pos = 1, .party_index = 0 };
}

static void leave_battle(uint8_t outcome) {
    s_nf.battle_outcome = outcome;
    step_n(1);
    s_nf.in_battle = false;
    s_nf.enemy_species = 0;
    s_nf.battle_flags = 0;
    s_rf.battlers = 0;
    s_rf.opponent = 0;
    step_n(1);
    s_nf.battle_outcome = 0;
}

TEST(track_first_attach_backfills) {
    setup();
    party_mon(0, 1, 280, 30);
    party_mon(1, 2, 283, 30);
    s_rf.badges = 0x07;
    step_n(30);
    ASSERT_EQ(count(RUN_EV_JOIN), 2);
    ASSERT_EQ(find(RUN_EV_JOIN, 0)->how, RUN_HOW_BACKFILL);
    ASSERT_EQ(find(RUN_EV_JOIN, 1)->how, RUN_HOW_BACKFILL);
    ASSERT_EQ(count(RUN_EV_BADGE), 3);
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(find(RUN_EV_BADGE, i)->untimed);
        ASSERT_EQ(find(RUN_EV_BADGE, i)->id, i + 1);
    }
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    ASSERT_EQ(s_log.member_count, 2);
}

TEST(track_badge_timed) {
    attached();
    s_rf.badges = 1u << 3;
    uint32_t set_pt = s_nf.play_time + 1;
    step_n(20);
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
    const RunEvent* b = find(RUN_EV_BADGE, 0);
    ASSERT_EQ(b->id, 4);
    ASSERT_TRUE(!b->untimed);
    ASSERT_EQ(b->pt, set_pt + RUN_CONFIRM_FRAMES - 1);
    ASSERT_EQ(b->steps, STEPS);
    ASSERT_EQ(b->team_max, 10);
    ASSERT_EQ(b->team_avg, 10);
}

TEST(track_badge_needs_play_time) {
    attached();
    s_rf.badges = 1u << 0;
    for (int i = 0; i < 20; i++) frame(); /* play time stopped */
    ASSERT_EQ(count(RUN_EV_BADGE), 0);
    step_n(RUN_CONFIRM_FRAMES);
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
}

TEST(track_sb_move_garbage_ignored) {
    attached();
    s_rf.sb1 = SB1 + 0x40;
    s_rf.badges = 0xFF;
    s_rf.story = 0xFFFF;
    s_rf.stats[RUN_STAT_STEPS] = 0xDEADBEEF;
    step_n(2);
    s_rf.badges = 0;
    s_rf.story = 0;
    s_rf.stats[RUN_STAT_STEPS] = STEPS;
    step_n(30);
    ASSERT_EQ(count(RUN_EV_BADGE), 0);
    ASSERT_EQ(count(RUN_EV_STORY), 0);
    /* Garbage that holds while the pointer keeps moving is still ignored. */
    s_rf.badges = 0xFF;
    for (int i = 0; i < 20; i++) {
        s_rf.sb1 = SB1 + 0x40 * (uint32_t)(i & 1);
        step_n(1);
    }
    s_rf.badges = 0;
    step_n(20);
    ASSERT_EQ(count(RUN_EV_BADGE), 0);
}

/* Badge 5 at ~1000, play to 1400, then a reload back to 900. Returns the badge pt. */
static uint32_t earn_then_reload(void) {
    attached();
    goto_pt(960);
    step_n(30); /* the jump itself is a discontinuity; let its resync end */
    s_rf.badges = 1u << 4;
    step_n(20);
    const RunEvent* b = find(RUN_EV_BADGE, 0);
    uint32_t bpt = b ? b->pt : 0;
    goto_pt(1400);
    step_n(1);
    mark();
    goto_pt(900);
    s_rf.badges = 0;
    step_n(61);
    return bpt;
}

TEST(track_reload_keeps_rewon_badge) {
    uint32_t bpt = earn_then_reload();
    ASSERT_TRUE(bpt > 990 && bpt < 1100);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    const RunEvent* r = find(RUN_EV_RELOAD, 0);
    ASSERT_EQ(r->pt, 1400);
    ASSERT_EQ(r->to_pt, 900);
    ASSERT_TRUE(!s_log.badge[4]);
    ASSERT_TRUE(s_log.orphan[4]);
    ASSERT_EQ(s_log.orphan_pt[4], bpt);
    step_n(100);
    ASSERT_EQ(count(RUN_EV_BADGE), 0);
    goto_pt(1100);
    s_rf.badges = 1u << 4;
    step_n(20);
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
    ASSERT_TRUE(!find(RUN_EV_BADGE, 0)->untimed);
    ASSERT_EQ(find(RUN_EV_BADGE, 0)->pt, 1100 + RUN_CONFIRM_FRAMES - 1);
}

TEST(track_reload_forward_uses_orphan) {
    uint32_t bpt = earn_then_reload();
    ASSERT_TRUE(!s_log.badge[4]);
    goto_pt(5000);
    s_rf.badges = 1u << 4;
    step_n(30);
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
    ASSERT_TRUE(!find(RUN_EV_BADGE, 0)->untimed);
    ASSERT_EQ(find(RUN_EV_BADGE, 0)->pt, bpt);
}

TEST(track_attach_older_save_reloads) {
    /* A log that already reaches pt 2000; the save now loaded is at 500. */
    attached();
    goto_pt(2000);
    step_n(30);
    s_rf.badges = 1u << 0;
    step_n(20);
    ASSERT_TRUE(s_log.badge[0]);
    run_tracker_reset(&s_t);
    mark();
    goto_pt(500);
    s_rf.badges = 0;
    step_n(80);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    ASSERT_EQ(find(RUN_EV_RELOAD, 0)->to_pt, 500);
    ASSERT_TRUE(!s_log.badge[0]);
}

TEST(track_hof_untimed_uses_stat) {
    setup();
    party_mon(0, 1, 280, 30);
    s_rf.story = 1u << RUN_STORY_HOF;
    goto_pt(60u * 60u * 60u * 5u);
    s_rf.stats[RUN_STAT_ENTERED_HOF] = 1;
    s_rf.stats[RUN_STAT_FIRST_HOF_TIME] = 2u << 16 | 3u << 8 | 4u;
    step_n(30);
    ASSERT_EQ(count(RUN_EV_STORY), 1);
    const RunEvent* s = find(RUN_EV_STORY, 0);
    ASSERT_EQ(s->id, RUN_STORY_HOF);
    ASSERT_TRUE(!s->untimed);
    ASSERT_EQ(s->pt, ((2u * 60u + 3u) * 60u + 4u) * 60u);
}

TEST(track_softreset_no_fake_faint) {
    attached();
    step_n(10);
    goto_pt(50);
    s_nf.party[0].hp = 0;
    step_n(200);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    /* Same with invalid frames between (title screen). */
    attached();
    s_nf.valid = false;
    step_n(5);
    s_nf.valid = true;
    s_nf.party[0].hp = 0;
    goto_pt(60);
    step_n(200);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
}

TEST(track_faint_rearm) {
    attached();
    s_nf.party[0].hp = 0;
    step_n(RUN_FAINT_FRAMES - 1);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    const RunEvent* f = find(RUN_EV_FAINT, 0);
    ASSERT_TRUE(key_eq(f->key, key(1)));
    ASSERT_EQ(f->species, 280);
    ASSERT_EQ(f->level, 10);
    ASSERT_EQ(f->sec, 16);
    ASSERT_EQ(f->foe, 0);
    step_n(100);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    s_nf.party[0].hp = 10;
    step_n(1);
    s_nf.party[0].hp = 0;
    step_n(RUN_FAINT_FRAMES);
    ASSERT_EQ(count(RUN_EV_FAINT), 2);
    ASSERT_EQ(s_log.faints, 2);
}

TEST(track_steven_gate) {
    attached();
    party_mon(1, 2, 283, 30);
    party_mon(2, 3, 286, 30);
    step_n(10);
    mark();
    for (uint8_t i = 3; i < 6; i++) {
        party_mon(i, 0x100u + i, 300, 30);
        s_nf.party[i].key.ot = RUN_STEVEN_OTID;
    }
    enter_battle(320, 0x999);
    s_nf.battle_flags = (1u << 3) | B_DOUBLE | B_PARTNER;
    s_rf.battlers = 4;
    s_rf.b[2] = (RunBattler){ .hp = 30, .max_hp = 30, .species = 300, .level = 40, .pos = 2, .party_index = 4 };
    s_rf.b[3] = (RunBattler){ .hp = 20, .max_hp = 20, .species = 320, .level = 30, .pos = 3, .party_index = 1 };
    step_n(5);
    s_nf.party[0].hp = 0;
    s_nf.party[4].hp = 0;
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(40);
    leave_battle(WON);
    s_nf.party_count = 3;
    step_n(40);
    ASSERT_EQ(count(RUN_EV_JOIN), 0);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    ASSERT_EQ(count(RUN_EV_BATTLE), 0);
    ASSERT_EQ(s_log.member_count, 3);
}

TEST(track_facility_gate) {
    attached();
    party_mon(1, 2, 283, 30);
    step_n(10);
    mark();
    NuzMon saved[2] = { s_nf.party[0], s_nf.party[1] };
    s_rf.facility = true;
    for (uint8_t i = 0; i < 3; i++) party_mon(i, 0x500u + i, 150, 30);
    s_nf.party[2].key.ot = 0x12345678u; /* rentals carry their own OT */
    step_n(10);
    enter_battle(151, 0x777);
    step_n(5);
    s_nf.party[0].hp = 0;
    s_rf.b[1].hp = 0;
    step_n(40);
    leave_battle(WON);
    step_n(10);
    s_rf.facility = false;
    s_nf.party[0] = saved[0];
    s_nf.party[1] = saved[1];
    s_nf.party_count = 2;
    step_n(40);
    ASSERT_EQ(count(RUN_EV_JOIN), 0);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    ASSERT_EQ(count(RUN_EV_BATTLE), 0);
    ASSERT_EQ(s_log.member_count, 2);
}

TEST(track_ko_single) {
    attached();
    enter_battle(10, 0x999);
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(5);
    leave_battle(WON);
    ASSERT_EQ(count(RUN_EV_BATTLE), 1);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_EQ(b->kind, RUN_BATTLE_WILD);
    ASSERT_EQ(b->foe, 10);
    ASSERT_EQ(b->outcome, WON);
    ASSERT_EQ(b->ko_count, 1);
    ASSERT_TRUE(key_eq(b->kos[0].key, key(1)));
    ASSERT_EQ(b->kos[0].count, 1);
    ASSERT_EQ(b->kos[0].level, 10);
    ASSERT_EQ(b->indirect, 0);
    ASSERT_EQ(b->upset, 0);
    ASSERT_EQ(b->low, 750);   /* party hp 30 of battler max 40 */
    ASSERT_TRUE(key_eq(b->low_by, key(1)));
    ASSERT_EQ(s_log.members[0].kos, 1);
}

TEST(track_ko_upset_and_ace) {
    attached();
    enter_battle(287, 0x999);
    s_nf.battle_flags = 1u << 3;
    s_rf.opponent = 269;
    s_rf.ace_species = 289;
    s_rf.b[1].level = 25;
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(3);
    s_rf.b[1] = (RunBattler){ .hp = 50, .max_hp = 50, .species = 289, .level = 31, .pos = 1 };
    step_n(3);
    s_rf.b[1].hp = 0;
    step_n(3);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->kind, RUN_BATTLE_TRAINER);
    ASSERT_EQ(b->trainer, 269);
    ASSERT_EQ(b->kos[0].count, 2);
    ASSERT_EQ(b->upset, 21);
    ASSERT_TRUE(b->has_ace);
    ASSERT_TRUE(key_eq(b->ace_by, key(1)));
    ASSERT_EQ(b->ace_species, 289);
}

/* A plain trainer's last Pokemon is no ace: only leaders, the Elite Four and the Champion count. */
TEST(track_plain_trainer_no_ace) {
    attached();
    enter_battle(287, 0x999);
    s_nf.battle_flags = 1u << 3;
    s_rf.opponent = 100;
    s_rf.ace_species = 289;
    step_n(5);
    s_rf.b[1] = (RunBattler){ .hp = 50, .max_hp = 50, .species = 289, .level = 31, .pos = 1 };
    step_n(3);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(3);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->kind, RUN_BATTLE_TRAINER);
    ASSERT_EQ(b->trainer, 100);
    ASSERT_EQ(b->ko_count, 1);
    ASSERT_TRUE(!b->has_ace);
    ASSERT_EQ(b->ace_species, 0);
}

TEST(track_ko_indirect) {
    attached();
    enter_battle(10, 0x999);
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 1;
    step_n(5);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->ko_count, 0);
    ASSERT_EQ(b->indirect, 1);
}

TEST(track_ko_partner) {
    attached();
    enter_battle(10, 0x999);
    s_nf.battle_flags = B_PARTNER;
    s_rf.b[0].party_index = 4;
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(5);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->ko_count, 0);
    ASSERT_EQ(b->indirect, 1);
}

TEST(track_ko_safari_none) {
    attached();
    enter_battle(10, 0x999);
    s_nf.battle_flags = B_SAFARI;
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(5);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->ko_count, 0);
    ASSERT_EQ(b->indirect, 0);
}

TEST(track_ko_double_both_same_frame) {
    attached();
    party_mon(1, 2, 283, 30);
    step_n(10);
    mark();
    enter_battle(10, 0x999);
    s_nf.battle_flags = B_DOUBLE;
    s_rf.battlers = 4;
    s_rf.b[2] = (RunBattler){ .hp = 30, .max_hp = 30, .species = 283, .level = 12, .pos = 2, .party_index = 1 };
    s_rf.b[3] = (RunBattler){ .hp = 20, .max_hp = 20, .species = 11, .level = 5, .pos = 3, .party_index = 1 };
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.b[3].hp = 0;
    s_rf.attacker = 0;
    step_n(5);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->ko_count, 1);
    ASSERT_EQ(b->kos[0].count, 2);
    ASSERT_EQ(b->indirect, 0);
}

TEST(track_battle_reload_midbattle) {
    attached();
    goto_pt(980);
    step_n(20);
    enter_battle(10, 0x999); /* starts at pt 1000 */
    step_n(50);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(60);  /* a rewind shorter than RUN_RELOAD_SETTLE of play time is a glitch */
    /* Rewind to start pt + 10, foe back up. */
    goto_pt(1010);
    s_rf.b[1].hp = 20;
    step_n(61);
    leave_battle(WON);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    ASSERT_EQ(count(RUN_EV_BATTLE), 1);
    ASSERT_TRUE(index_of(RUN_EV_RELOAD) < index_of(RUN_EV_BATTLE));
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_EQ(b->ko_count, 0);
    ASSERT_EQ(b->indirect, 0);

    /* Reload to before the battle started, still inside a battle: discarded. */
    attached();
    goto_pt(1980);
    step_n(20);
    enter_battle(10, 0x999); /* starts at pt 2000 */
    step_n(30);
    goto_pt(1940);              /* 90 frames back, before the start */
    step_n(61);
    leave_battle(WON);
    step_n(5);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    ASSERT_EQ(count(RUN_EV_BATTLE), 0);
}

TEST(track_catch_join_caught) {
    attached();
    enter_battle(280, 0xCA7C4);
    s_rf.enemy_owned = false;
    step_n(5);
    s_rf.enemy_owned = true; /* the catch registers it */
    s_nf.battle_outcome = CAUGHT;
    step_n(1);
    s_nf.in_battle = false;
    s_rf.battlers = 0;
    party_mon(1, 0xCA7C4, 280, 20);
    step_n(5);
    ASSERT_EQ(count(RUN_EV_JOIN), 1);
    const RunEvent* j = find(RUN_EV_JOIN, 0);
    ASSERT_EQ(j->how, RUN_HOW_CAUGHT);
    ASSERT_TRUE(j->newdex);
    ASSERT_TRUE(!j->untimed);
    ASSERT_TRUE(index_of(RUN_EV_BATTLE) < index_of(RUN_EV_JOIN));
    ASSERT_EQ(find(RUN_EV_BATTLE, 0)->outcome, CAUGHT);

    /* Full party: the catch goes to the PC and shows up on a later box read. */
    attached();
    enter_battle(281, 0xB0C5);
    s_rf.enemy_owned = true;
    step_n(5);
    s_nf.battle_outcome = CAUGHT;
    step_n(1);
    s_nf.in_battle = false;
    s_rf.battlers = 0;
    s_nf.has_boxes = false;
    step_n(30);
    fill_mon(&s_nf.boxes[0], 0xB0C5, 281, 0);
    s_nf.box_count = 1;
    s_nf.has_boxes = true;
    step_n(1);
    ASSERT_EQ(count(RUN_EV_JOIN), 1);
    ASSERT_EQ(find(RUN_EV_JOIN, 0)->how, RUN_HOW_CAUGHT);
    ASSERT_TRUE(!find(RUN_EV_JOIN, 0)->newdex);
}

TEST(track_join_traded_hatched) {
    attached();
    party_mon(1, 0x10, 25, 30)->met = 0xFE;
    party_mon(2, 0x11, 26, 30)->key.ot = 0x0BADF00Du;
    party_mon(3, 0x12, 27, 30)->met_level = 0;
    party_mon(4, 0x13, 28, 30);
    NuzMon* egg = party_mon(5, 0x14, 29, 30);
    egg->egg = true;
    step_n(3);
    ASSERT_EQ(count(RUN_EV_JOIN), 4);
    ASSERT_EQ(find(RUN_EV_JOIN, 0)->how, RUN_HOW_TRADED);
    ASSERT_EQ(find(RUN_EV_JOIN, 1)->how, RUN_HOW_TRADED);
    ASSERT_EQ(find(RUN_EV_JOIN, 2)->how, RUN_HOW_HATCHED);
    ASSERT_EQ(find(RUN_EV_JOIN, 3)->how, RUN_HOW_GIFT);
    const RunEvent* g = find(RUN_EV_JOIN, 3);
    ASSERT_EQ(g->species, 28);
    ASSERT_EQ(g->level, 10);
    ASSERT_EQ(g->sec, 16);
    ASSERT_EQ(g->ball, 4);
    ASSERT_TRUE(!g->untimed);
}

TEST(track_evolve_and_shedinja) {
    setup();
    party_mon(0, 1, 301, 30); /* Nincada */
    step_n(20);
    mark();
    s_nf.party[0].species = 302;
    step_n(2);
    s_nf.party[0].species = 301; /* a two-frame flicker is not an evolution */
    step_n(10);
    ASSERT_EQ(count(RUN_EV_EVOLVE), 0);
    s_nf.party[0].species = 302;
    s_nf.party[0].level = 20;
    step_n(10);
    NuzMon* shed = party_mon(1, 1, NUZ_SPECIES_SHEDINJA, 1);
    shed->key.variant = 1;
    step_n(10);
    ASSERT_EQ(count(RUN_EV_EVOLVE), 1);
    const RunEvent* e = find(RUN_EV_EVOLVE, 0);
    ASSERT_TRUE(key_eq(e->key, key(1)));
    ASSERT_EQ(e->from, 301);
    ASSERT_EQ(e->species, 302);
    ASSERT_EQ(e->level, 20);
    ASSERT_EQ(count(RUN_EV_JOIN), 1);
    ASSERT_EQ(find(RUN_EV_JOIN, 0)->how, RUN_HOW_GIFT);
    ASSERT_EQ(find(RUN_EV_JOIN, 0)->key.variant, 1);
}

TEST(track_whiteout_edge) {
    attached();
    s_rf.whiteout = true;
    step_n(120);
    s_rf.whiteout = false;
    step_n(10);
    ASSERT_EQ(count(RUN_EV_WHITEOUT), 1);
    ASSERT_EQ(find(RUN_EV_WHITEOUT, 0)->sec, 16);
}

static void replay_matches(void) {
    run_log_init(&s_replay);
    RunEvent start;
    memset(&start, 0, sizeof(start));
    start.type = RUN_EV_START;
    start.pt = 101;
    start.tid = TID;
    start.low = 1000;
    run_apply(&s_replay, &start);
    for (uint16_t i = 0; i < s_all_n; i++) run_apply(&s_replay, &s_all[i]);
}

TEST(track_reload_freezes_then_faint_replays) {
    attached();
    goto_pt(1000);
    step_n(30);
    mark();
    /* A faint inside the settle window is frozen, and the mon is at 0 HP when the reload
     * settles, so it is not armed: nothing. */
    goto_pt(900);
    step_n(5);
    s_nf.party[0].hp = 0;
    step_n(56);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    step_n(60);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    /* After the reload a real faint is logged, after the RELOAD, and replay agrees. */
    s_nf.party[0].hp = 30;
    step_n(1);
    s_nf.party[0].hp = 0;
    step_n(RUN_FAINT_FRAMES);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    ASSERT_TRUE(index_of(RUN_EV_RELOAD) < index_of(RUN_EV_FAINT));
    ASSERT_EQ(s_log.faints, 1);
    replay_matches();
    ASSERT_EQ(s_replay.faints, 1);
    ASSERT_EQ(s_replay.members[0].faints, 1);
    ASSERT_EQ(s_replay.member_count, s_log.member_count);
}

TEST(track_one_frame_dip_is_not_a_reload) {
    attached();
    goto_pt(1000);
    step_n(30);
    mark();
    goto_pt(10);
    step_n(1);
    goto_pt(1031);
    step_n(100);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    ASSERT_EQ(s_all_n, s_mark);   /* nothing at all: no reload, no resync side effects */
}

/* n frames whose play time reads v (garbage), while real play time keeps running. */
static void garbage_frames(uint32_t n, uint32_t v) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t real = s_nf.play_time + 1;
        s_nf.play_time = v;
        frame();
        s_nf.play_time = real;
    }
}

TEST(track_garbage_dip_no_reload) {
    attached();
    step_n(30);
    mark();
    uint16_t ev_before = s_log.ev_count;
    s_nf.party[0].hp = 0;
    uint32_t p = s_nf.play_time + 1;
    step_n(10);                 /* faint debounce under way */
    garbage_frames(5, 7);
    step_n(19);                 /* 29 counted frames */
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    ASSERT_EQ(find(RUN_EV_FAINT, 0)->pt, p + 34);
    step_n(100);
    ASSERT_EQ(s_all_n - s_mark, 1);
    ASSERT_EQ(s_log.ev_count, ev_before + 1);
}

TEST(track_long_garbage_dip_no_reload) {
    attached();
    step_n(30);
    mark();
    uint16_t ev_before = s_log.ev_count;
    garbage_frames(30, 7);
    step_n(100);
    /* Varying garbage that never runs on like play time. */
    for (uint32_t i = 0; i < 90; i++) garbage_frames(1, (i * 37u) % 50u);
    step_n(100);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    ASSERT_EQ(s_all_n, s_mark);
    ASSERT_EQ(s_log.ev_count, ev_before);
}

TEST(track_real_reload_after_settle) {
    attached();
    goto_pt(2000);
    step_n(30);
    mark();
    uint32_t from = s_nf.play_time;
    goto_pt(from - 600);
    step_n(RUN_RELOAD_SETTLE - 1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    ASSERT_EQ(find(RUN_EV_RELOAD, 0)->pt, from);
    ASSERT_EQ(find(RUN_EV_RELOAD, 0)->to_pt, from - 600);
    step_n(200);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
}

TEST(track_flat_garbage_no_reload) {
    attached();
    step_n(30);
    mark();
    uint16_t ev_before = s_log.ev_count;
    s_nf.party[0].hp = 0;
    step_n(10);                 /* faint debounce under way */
    garbage_frames(200, 7);
    step_n(19);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    step_n(100);
    ASSERT_EQ(s_all_n - s_mark, 1);
    ASSERT_EQ(s_log.ev_count, ev_before + 1);
}

TEST(track_reload_after_garbage_start) {
    attached();
    goto_pt(2000);
    step_n(30);
    mark();
    uint32_t from = s_nf.play_time;
    uint32_t r = from - 600;
    garbage_frames(3, 7);       /* the first behind frames are misreads */
    goto_pt(r);
    step_n(RUN_RELOAD_SETTLE - 1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
    ASSERT_EQ(find(RUN_EV_RELOAD, 0)->pt, from);
    ASSERT_EQ(find(RUN_EV_RELOAD, 0)->to_pt, r);
    step_n(200);
    ASSERT_EQ(count(RUN_EV_RELOAD), 1);
}

TEST(track_dip_mid_faint_debounce) {
    attached();
    s_nf.party[0].hp = 0;
    uint32_t p = s_nf.play_time + 1;
    step_n(10);                 /* zero for p .. p+9 */
    goto_pt(10);
    step_n(1);                  /* one-frame misread */
    goto_pt(p + 11);
    step_n(19);                 /* 29 counted frames */
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    ASSERT_EQ(find(RUN_EV_FAINT, 0)->pt, p + 30);
    step_n(100);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
}

TEST(track_badge_after_dip_timed) {
    attached();
    s_nf.has_boxes = false;     /* boxes are read every 60 frames in play, not every frame */
    uint32_t p = s_nf.play_time;
    goto_pt(10);
    step_n(1);
    goto_pt(p + 2);
    step_n(10);
    s_rf.badges = 1u << 1;
    step_n(20);
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
    ASSERT_TRUE(!find(RUN_EV_BADGE, 0)->untimed);
}

TEST(track_dip_below_battle_start_cancels) {
    attached();
    enter_battle(10, 0x999);
    step_n(5);
    s_rf.b[1].hp = 0;
    s_rf.attacker = 0;
    step_n(3);
    uint32_t p = s_nf.play_time;
    goto_pt(50);                /* below the battle's start */
    step_n(1);
    goto_pt(p + 2);
    step_n(3);
    leave_battle(WON);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
    ASSERT_EQ(count(RUN_EV_BATTLE), 1);
    ASSERT_EQ(find(RUN_EV_BATTLE, 0)->ko_count, 1);
    ASSERT_EQ(find(RUN_EV_BATTLE, 0)->kos[0].count, 1);
}

TEST(track_invalid_short_freezes) {
    attached();
    s_nf.party[0].hp = 0;
    step_n(10);
    s_nf.valid = false;
    step_n(RUN_INVALID_FRAMES);
    s_nf.valid = true;
    step_n(19);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);
    step_n(1);
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
}

TEST(track_invalid_long_is_discontinuity) {
    attached();
    s_nf.party[0].hp = 0;
    step_n(10);
    s_rf.valid = false;
    step_n(RUN_INVALID_FRAMES + 40);
    s_rf.valid = true;
    step_n(100);
    ASSERT_EQ(count(RUN_EV_FAINT), 0);   /* re-baselined at 0 HP: not armed */
    /* A badge found right after it is a resync discovery. */
    attached();
    s_rf.valid = false;
    step_n(RUN_INVALID_FRAMES + 1);
    s_rf.valid = true;
    s_rf.badges = 1u << 2;
    step_n(30);
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
    ASSERT_TRUE(find(RUN_EV_BADGE, 0)->untimed);
}

TEST(track_badge_steps_while_walking) {
    attached();
    s_rf.badges = 1u << 5;
    for (int i = 0; i < 20; i++) {
        s_rf.stats[RUN_STAT_STEPS]++;
        step_n(1);
        if (count(RUN_EV_BADGE)) break;
    }
    ASSERT_EQ(count(RUN_EV_BADGE), 1);
    ASSERT_EQ(find(RUN_EV_BADGE, 0)->steps, s_rf.stats[RUN_STAT_STEPS]);
}

TEST(track_max_pt_ignores_immune) {
    /* Attached late: everything logged is backfill/untimed, so an older save is no reload. */
    setup();
    party_mon(0, 1, 280, 30);
    s_rf.badges = 0x03;
    goto_pt(5000);
    step_n(30);
    ASSERT_EQ(count(RUN_EV_BADGE), 2);
    run_tracker_reset(&s_t);
    mark();
    goto_pt(1000);
    step_n(80);
    ASSERT_EQ(count(RUN_EV_RELOAD), 0);
}

TEST(track_member_cap_no_repeat) {
    setup();
    for (uint32_t i = 0; i < RUN_MEMBERS_MAX; i++) {
        RunEvent j;
        memset(&j, 0, sizeof(j));
        j.type = RUN_EV_JOIN;
        j.pt = 101;
        j.key = key(0x9000u + i);
        j.species = 25;
        j.how = RUN_HOW_BACKFILL;
        j.low = 1000;
        run_apply(&s_log, &j);
    }
    ASSERT_EQ(s_log.member_count, RUN_MEMBERS_MAX);
    party_mon(0, 1, 280, 30);
    step_n(120);
    ASSERT_EQ(count(RUN_EV_JOIN), 0);
}

TEST(track_events_full_no_repeat) {
    setup();
    while (!s_log.events_full) {
        RunEvent w;
        memset(&w, 0, sizeof(w));
        w.type = RUN_EV_WHITEOUT;
        w.pt = 101;
        w.low = 1000;
        run_apply(&s_log, &w);
    }
    party_mon(0, 1, 280, 30);
    s_rf.badges = 0x01;
    s_rf.story = 1u << RUN_STORY_STARTER;
    step_n(120);
    ASSERT_EQ(count(RUN_EV_JOIN), 0);
    ASSERT_EQ(count(RUN_EV_BADGE), 0);
    ASSERT_EQ(count(RUN_EV_STORY), 0);
}

TEST(track_battle_faints_wound_back) {
    attached();
    enter_battle(10, 0x999);
    step_n(5);
    s_nf.party[0].hp = 0;
    step_n(RUN_FAINT_FRAMES + 5);
    leave_battle(1); /* a no-reload control */
    ASSERT_EQ(find(RUN_EV_BATTLE, 0)->faints, 1);

    attached();
    goto_pt(980);
    step_n(20);
    enter_battle(10, 0x999);    /* starts at pt 1000 */
    step_n(5);
    s_nf.party[0].hp = 0;
    step_n(RUN_FAINT_FRAMES + 60);   /* FAINT at ~1034, play on to ~1094 */
    ASSERT_EQ(count(RUN_EV_FAINT), 1);
    goto_pt(1020);
    s_nf.party[0].hp = 30;
    step_n(61);
    leave_battle(WON);
    ASSERT_EQ(count(RUN_EV_BATTLE), 1);
    ASSERT_EQ(find(RUN_EV_BATTLE, 0)->faints, 0);
    ASSERT_EQ(s_log.faints, 0);
}

TEST(track_stale_battle_mons_no_ko) {
    attached();
    enter_battle(50, 0x999);
    s_rf.b[1] = (RunBattler){ .hp = 20, .max_hp = 20, .species = 10, .level = 5, .pos = 1 };  /* last battle's foe */
    step_n(3);
    s_rf.b[1].hp = 0;           /* the game clears it */
    step_n(3);
    s_rf.b[1] = (RunBattler){ .hp = 20, .max_hp = 20, .species = 50, .level = 5, .pos = 1 };
    step_n(1);
    s_rf.b[1].hp = 0;           /* one live frame is not enough to arm */
    step_n(3);
    s_rf.b[1].hp = 20;
    step_n(3);
    s_rf.b[1].hp = 0;           /* the real KO */
    s_rf.attacker = 0;
    step_n(3);
    leave_battle(WON);
    const RunEvent* b = find(RUN_EV_BATTLE, 0);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->ko_count, 1);
    ASSERT_EQ(b->kos[0].count, 1);
    ASSERT_EQ(b->indirect, 0);
}

TEST(track_backfill_many_boxes) {
    setup();
    party_mon(0, 1, 280, 30);
    for (uint16_t i = 0; i < 100; i++) fill_mon(&s_nf.boxes[i], 0x1000u + i, 25, 0);
    s_nf.box_count = 100;
    s_nf.has_boxes = false;
    step_n(3);
    s_nf.has_boxes = true;
    step_n(1);
    s_nf.has_boxes = false;
    step_n(40);
    ASSERT_EQ(count(RUN_EV_JOIN), 101);
    for (int i = 0; i < 101; i++) ASSERT_EQ(find(RUN_EV_JOIN, i)->how, RUN_HOW_BACKFILL);
    ASSERT_EQ(s_log.member_count, 101);
}

TEST(track_events_in_range) {
    /* Everything emitted already survives normalisation unchanged. */
    attached();
    enter_battle(10, 0x999);
    step_n(3);
    s_rf.b[1].hp = 0;
    step_n(3);
    leave_battle(WON);
    s_nf.party[0].hp = 0;
    step_n(40);
    for (uint16_t i = 0; i < s_all_n; i++) {
        const RunEvent* e = &s_all[i];
        ASSERT_TRUE(e->type < RUN_EV_COUNT);
        ASSERT_TRUE(e->how < RUN_HOW_COUNT);
        ASSERT_TRUE(e->kind <= RUN_BATTLE_OTHER);
        ASSERT_TRUE(e->ko_count <= RUN_KO_SLOTS);
        ASSERT_TRUE(e->key.variant <= 1);
        ASSERT_TRUE(e->low <= 1000);
    }
}

void run_run_track_tests(void) {
    RUN_TEST(track_first_attach_backfills);
    RUN_TEST(track_badge_timed);
    RUN_TEST(track_badge_needs_play_time);
    RUN_TEST(track_sb_move_garbage_ignored);
    RUN_TEST(track_reload_keeps_rewon_badge);
    RUN_TEST(track_reload_forward_uses_orphan);
    RUN_TEST(track_attach_older_save_reloads);
    RUN_TEST(track_hof_untimed_uses_stat);
    RUN_TEST(track_softreset_no_fake_faint);
    RUN_TEST(track_faint_rearm);
    RUN_TEST(track_steven_gate);
    RUN_TEST(track_facility_gate);
    RUN_TEST(track_ko_single);
    RUN_TEST(track_ko_upset_and_ace);
    RUN_TEST(track_plain_trainer_no_ace);
    RUN_TEST(track_ko_indirect);
    RUN_TEST(track_ko_partner);
    RUN_TEST(track_ko_safari_none);
    RUN_TEST(track_ko_double_both_same_frame);
    RUN_TEST(track_battle_reload_midbattle);
    RUN_TEST(track_catch_join_caught);
    RUN_TEST(track_join_traded_hatched);
    RUN_TEST(track_evolve_and_shedinja);
    RUN_TEST(track_whiteout_edge);
    RUN_TEST(track_reload_freezes_then_faint_replays);
    RUN_TEST(track_one_frame_dip_is_not_a_reload);
    RUN_TEST(track_dip_mid_faint_debounce);
    RUN_TEST(track_garbage_dip_no_reload);
    RUN_TEST(track_long_garbage_dip_no_reload);
    RUN_TEST(track_real_reload_after_settle);
    RUN_TEST(track_flat_garbage_no_reload);
    RUN_TEST(track_reload_after_garbage_start);
    RUN_TEST(track_badge_after_dip_timed);
    RUN_TEST(track_dip_below_battle_start_cancels);
    RUN_TEST(track_invalid_short_freezes);
    RUN_TEST(track_invalid_long_is_discontinuity);
    RUN_TEST(track_badge_steps_while_walking);
    RUN_TEST(track_max_pt_ignores_immune);
    RUN_TEST(track_member_cap_no_repeat);
    RUN_TEST(track_events_full_no_repeat);
    RUN_TEST(track_battle_faints_wound_back);
    RUN_TEST(track_stale_battle_mons_no_ko);
    RUN_TEST(track_backfill_many_boxes);
    RUN_TEST(track_events_in_range);
}
