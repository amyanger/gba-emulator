#include "test_harness.h"
#include "game/nuzlocke.h"

#define TID 0x0000BEEFu
#define SECS 100

/* Large: keep off the stack. */
static NuzRun s_run;
static NuzTracker s_t;
static NuzFacts s_f;
static uint16_t s_fam[NUZ_SPECIES_MAX];
static NuzEvent s_ev[NUZ_EVENTS_MAX];
static uint8_t s_n;

/* Species 10 -> 11 -> 12 is one line; everything else stands alone. */
static void setup(void) {
    for (int s = 0; s < NUZ_SPECIES_MAX; s++) s_fam[s] = (uint16_t)s;
    s_fam[11] = s_fam[12] = 10;
    nuz_run_init(&s_run);
    memset(&s_f, 0, sizeof(s_f));
    s_f.valid = true;
    s_f.trainer_id = TID;
    s_f.play_time = 1000;
    s_f.mapsec = 16;
    s_f.active_party = -1;
    s_f.has_boxes = true;
}

static NuzMon* party_mon(uint8_t slot, uint32_t pid, uint16_t species, uint16_t hp) {
    if (s_f.party_count <= slot) s_f.party_count = (uint8_t)(slot + 1);
    NuzMon* m = &s_f.party[slot];
    memset(m, 0, sizeof(*m));
    m->key.pid = pid;
    m->key.ot = TID;
    m->species = species;
    m->level = 10;
    m->met = 16;
    m->hp = hp;
    return m;
}

/* Starts a run with whatever the facts hold, then resets the tracker. */
static void start_run(void) {
    static NuzEvent ev[NUZ_BOX_MAX + 8];
    uint16_t n = nuz_backfill(&s_f, SECS, ev, NUZ_BOX_MAX + 8);
    for (uint16_t i = 0; i < n; i++) nuz_apply(&s_run, &ev[i]);
    nuz_tracker_reset(&s_t, &s_run);
}

/* One frame: run the tracker, apply what it found, advance play time. */
static void step(void) {
    s_n = nuz_track_step(&s_t, &s_run, &s_f, s_fam, SECS, s_ev);
    for (uint8_t i = 0; i < s_n; i++) nuz_apply(&s_run, &s_ev[i]);
    s_f.play_time++;
}

static int count_type(uint8_t type) {
    int c = 0;
    for (uint8_t i = 0; i < s_n; i++) c += s_ev[i].type == type;
    return c;
}

static void enter_battle(uint16_t species, uint32_t pid, uint32_t flags) {
    s_f.in_battle = true;
    s_f.battle_flags = flags;
    s_f.battle_outcome = 0;
    s_f.enemy_species = species;
    s_f.enemy_level = 5;
    s_f.enemy_pid = pid;
    s_f.enemy_ot = TID;
}

static void leave_battle(uint8_t outcome) {
    s_f.battle_outcome = outcome;
    step();
    s_f.in_battle = false;
    s_f.enemy_species = 0;
    step();
}

/* A pid that isn't shiny for TID. */
#define PLAIN(n) (0x11110000u + (n))

TEST(nuz_backfill_marks_owned_routes) {
    setup();
    party_mon(0, PLAIN(1), 10, 20);
    s_f.box_count = 1;
    s_f.boxes[0].key.pid = PLAIN(2);
    s_f.boxes[0].key.ot = TID;
    s_f.boxes[0].species = 20;
    s_f.boxes[0].met = 30;
    start_run();
    ASSERT_TRUE(s_run.active);
    ASSERT_EQ_HEX(s_run.tid, TID);
    ASSERT_EQ(s_run.member_count, 2);
    ASSERT_TRUE(s_run.routes[16].used);
    ASSERT_EQ(s_run.routes[16].result, NUZ_RES_OWNED);
    ASSERT_TRUE(s_run.routes[30].used);
}

TEST(nuz_first_encounter_then_second_is_extra) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    s_f.mapsec = 40;
    start_run();
    step();
    enter_battle(30, PLAIN(9), 0);
    step();
    ASSERT_EQ(count_type(NUZ_EV_ENC), 1);
    ASSERT_EQ(s_ev[0].kind, NUZ_KIND_FIRST);
    ASSERT_EQ(s_run.routes[40].result, NUZ_RES_PENDING);
    leave_battle(4);  /* ran */
    ASSERT_EQ(s_run.routes[40].result, NUZ_RES_MISSED);
    enter_battle(31, PLAIN(10), 0);
    step();
    ASSERT_EQ(s_ev[0].kind, NUZ_KIND_EXTRA);
    leave_battle(7);
    ASSERT_EQ(s_run.routes[40].result, NUZ_RES_MISSED);
}

TEST(nuz_catch_joins_after_battle_and_links_route) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    s_f.mapsec = 41;
    start_run();
    step();
    enter_battle(30, PLAIN(9), 0);
    step();
    party_mon(1, PLAIN(9), 30, 15);  /* the game adds it mid-battle */
    step();
    ASSERT_EQ(count_type(NUZ_EV_CAUGHT) + count_type(NUZ_EV_GIFT), 0);
    leave_battle(7);
    ASSERT_EQ(count_type(NUZ_EV_CAUGHT), 1);
    ASSERT_EQ(s_run.routes[41].result, NUZ_RES_CAUGHT);
    ASSERT_TRUE(s_run.routes[41].member >= 0);
    ASSERT_EQ(s_run.members[s_run.routes[41].member].join, NUZ_JOIN_CAUGHT);
}

TEST(nuz_shiny_and_dupe_leave_route_open) {
    setup();
    party_mon(0, PLAIN(1), 11, 20);  /* owns the 10-11-12 line */
    s_f.mapsec = 42;
    start_run();
    step();
    enter_battle(12, PLAIN(9), 0);
    step();
    ASSERT_EQ(s_ev[0].kind, NUZ_KIND_DUPE);
    leave_battle(4);
    ASSERT_TRUE(!s_run.routes[42].used);
    enter_battle(50, TID, 0);  /* pid == ot: shiny */
    step();
    ASSERT_EQ(s_ev[0].kind, NUZ_KIND_SHINY);
    ASSERT_TRUE(s_ev[0].shiny);
    leave_battle(4);
    ASSERT_TRUE(!s_run.routes[42].used);
}

TEST(nuz_trainer_first_battle_and_wally_ignored) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    start_run();
    step();
    uint32_t flags[3] = { NUZ_BATTLE_TRAINER, NUZ_BATTLE_FIRST_BATTLE, NUZ_BATTLE_WALLY_TUTORIAL };
    for (int i = 0; i < 3; i++) {
        enter_battle(30, PLAIN(9), flags[i]);
        step();
        ASSERT_EQ(count_type(NUZ_EV_ENC), 0);
        leave_battle(1);
        ASSERT_EQ(count_type(NUZ_EV_OUT), 0);
    }
}

TEST(nuz_gift_does_not_use_route) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    start_run();
    step();
    NuzMon* g = party_mon(1, PLAIN(2), 60, 20);
    g->met = 55;
    step();
    ASSERT_EQ(count_type(NUZ_EV_GIFT), 1);
    ASSERT_TRUE(!s_run.routes[55].used);
    ASSERT_EQ(s_run.routes[55].gift, 1);
}

TEST(nuz_move_to_pc_is_not_a_new_member) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    party_mon(1, PLAIN(2), 21, 20);
    start_run();
    step();
    /* deposited: gone from the party, shows up in the box */
    s_f.party_count = 1;
    s_f.box_count = 1;
    s_f.boxes[0] = (NuzMon){ .key = { PLAIN(2), TID, 0 }, .species = 21, .met = 16 };
    for (int i = 0; i < 40; i++) step();
    ASSERT_EQ(s_run.member_count, 2);
    ASSERT_EQ(s_run.deaths, 0);
}

TEST(nuz_death_debounced_and_permanent) {
    setup();
    NuzMon* m = party_mon(0, PLAIN(1), 20, 20);
    party_mon(1, PLAIN(2), 21, 20);
    start_run();
    step();
    m->hp = 0;
    for (int i = 0; i < NUZ_DEATH_FRAMES - 1; i++) step();
    ASSERT_EQ(s_run.deaths, 0);
    step();
    ASSERT_EQ(s_run.deaths, 1);
    ASSERT_TRUE(s_run.members[0].dead);
    m->hp = 20;  /* Pokemon Center */
    for (int i = 0; i < 40; i++) step();
    ASSERT_TRUE(s_run.members[0].dead);
    ASSERT_EQ(s_run.deadused, 0);
    ASSERT_TRUE(!s_run.wiped);
}

TEST(nuz_brief_zero_hp_is_not_a_death) {
    setup();
    NuzMon* m = party_mon(0, PLAIN(1), 20, 20);
    start_run();
    step();
    m->hp = 0;
    for (int i = 0; i < 5; i++) step();
    m->hp = 20;
    for (int i = 0; i < 40; i++) step();
    ASSERT_EQ(s_run.deaths, 0);
}

TEST(nuz_bad_slot_is_never_a_death) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    start_run();
    step();
    memset(&s_f.party[0], 0, sizeof(s_f.party[0]));  /* checksum failure hole */
    for (int i = 0; i < 40; i++) step();
    ASSERT_EQ(s_run.deaths, 0);
    ASSERT_EQ(s_run.member_count, 1);
}

TEST(nuz_battle_death_records_foe_and_deadused_once) {
    setup();
    NuzMon* m = party_mon(0, PLAIN(1), 20, 20);
    party_mon(1, PLAIN(2), 21, 20);
    s_f.mapsec = 44;
    start_run();
    step();
    enter_battle(30, PLAIN(9), NUZ_BATTLE_TRAINER);
    s_f.active_party = 0;
    step();
    m->hp = 0;
    for (int i = 0; i < NUZ_DEATH_FRAMES; i++) step();
    ASSERT_EQ(s_run.deaths, 1);
    ASSERT_EQ(s_run.members[0].foe, 30);
    ASSERT_EQ(s_run.members[0].death_sec, 44);
    ASSERT_EQ(s_run.deadused, 0);  /* fainted, still on the field at 0 HP */
    leave_battle(1);
    m->hp = 20;
    step();
    enter_battle(31, PLAIN(10), NUZ_BATTLE_TRAINER);
    s_f.active_party = 0;
    for (int i = 0; i < 10; i++) step();
    ASSERT_EQ(s_run.deadused, 1);
    ASSERT_TRUE(s_run.members[0].used_dead);
}

TEST(nuz_wipe_logged_once) {
    setup();
    NuzMon* a = party_mon(0, PLAIN(1), 20, 20);
    NuzMon* b = party_mon(1, PLAIN(2), 21, 20);
    start_run();
    step();
    a->hp = 0;
    b->hp = 0;
    int wipes = 0;
    for (int i = 0; i < 80; i++) { step(); wipes += count_type(NUZ_EV_WIPE); }
    ASSERT_EQ(s_run.deaths, 2);
    ASSERT_EQ(wipes, 1);
    ASSERT_TRUE(s_run.wiped);
}

TEST(nuz_held_rewind_logs_one_reload_and_marks_death) {
    setup();
    NuzMon* m = party_mon(0, PLAIN(1), 20, 20);
    party_mon(1, PLAIN(2), 21, 20);
    start_run();
    for (int i = 0; i < 100; i++) step();
    m->hp = 0;
    for (int i = 0; i < NUZ_DEATH_FRAMES; i++) step();
    ASSERT_EQ(s_run.deaths, 1);
    uint32_t death_pt = s_run.members[0].death_pt;
    /* rewind: play time steps back a little every frame for a while */
    m->hp = 20;
    int reloads = 0;
    for (int i = 0; i < 120; i++) {
        s_f.play_time -= 3;
        s_n = nuz_track_step(&s_t, &s_run, &s_f, s_fam, SECS, s_ev);
        for (uint8_t j = 0; j < s_n; j++) nuz_apply(&s_run, &s_ev[j]);
        reloads += count_type(NUZ_EV_RELOAD);
    }
    ASSERT_TRUE(s_f.play_time < death_pt);
    for (int i = 0; i < NUZ_RELOAD_SETTLE + 5; i++) { step(); reloads += count_type(NUZ_EV_RELOAD); }
    ASSERT_EQ(reloads, 1);
    ASSERT_EQ(s_run.reloads, 1);
    ASSERT_TRUE(s_run.reloaded_after_death);
    ASSERT_TRUE(s_run.members[0].reloaded_after);
    ASSERT_TRUE(s_run.members[0].dead);  /* history sticks */
}

TEST(nuz_one_frame_misread_is_not_a_reload) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    start_run();
    for (int i = 0; i < 10; i++) step();
    uint32_t real = s_f.play_time;
    s_f.play_time = 3;  /* garbage for one frame */
    step();
    s_f.play_time = real + 1;
    for (int i = 0; i < NUZ_RELOAD_SETTLE + 10; i++) step();
    ASSERT_EQ(s_run.reloads, 0);
}

TEST(nuz_reroll_after_reload) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    s_f.mapsec = 45;
    start_run();
    for (int i = 0; i < 10; i++) step();
    enter_battle(30, PLAIN(9), 0);
    step();
    leave_battle(4);
    s_f.play_time -= 200;  /* load an older state */
    for (int i = 0; i < NUZ_RELOAD_SETTLE + 2; i++) step();
    enter_battle(31, PLAIN(10), 0);
    step();
    ASSERT_EQ(s_ev[0].kind, NUZ_KIND_REROLL);
    party_mon(1, PLAIN(10), 31, 20);
    leave_battle(7);
    ASSERT_EQ(s_run.rerolls, 1);
    ASSERT_TRUE(s_run.routes[45].rerolled);
    ASSERT_TRUE(s_run.members[1].reroll);
    ASSERT_EQ(s_run.routes[45].result, NUZ_RES_MISSED);  /* the first one still counts */
}

TEST(nuz_battle_running_at_attach_is_ignored) {
    setup();
    party_mon(0, PLAIN(1), 20, 20);
    start_run();
    enter_battle(30, PLAIN(9), 0);
    step();
    ASSERT_EQ(count_type(NUZ_EV_ENC), 0);
    leave_battle(7);
    ASSERT_EQ(count_type(NUZ_EV_OUT), 0);
}

TEST(nuz_format_parse_round_trip) {
    NuzEvent all[NUZ_EV_COUNT];
    memset(all, 0, sizeof(all));
    for (int i = 0; i < NUZ_EV_COUNT; i++) {
        NuzEvent* e = &all[i];
        e->type = (uint8_t)i;
        e->pt = 123456u + (uint32_t)i;
        e->key = (NuzKey){ 0xDEADBEEF, 0x0000BEEF, 1 };
        e->species = 277;
        e->level = 7;
        e->sec = 16;
        e->kind = NUZ_KIND_REROLL;
        e->shiny = true;
        e->caught = true;
        e->foe = 290;
        e->tid = 0x12345678;
        e->from = 999;
        e->to = 500;
        for (int j = 0; j < 10; j++) e->nick[j] = (uint8_t)(0xBB + j);
    }
    for (int i = 0; i < NUZ_EV_COUNT; i++) {
        char line[256];
        NuzEvent back;
        ASSERT_TRUE(nuz_format(&all[i], line, sizeof(line)) > 0);
        ASSERT_TRUE(nuz_parse(line, &back));
        ASSERT_EQ(back.type, i);
        ASSERT_EQ(back.pt, all[i].pt);
        char again[256];
        ASSERT_TRUE(nuz_format(&back, again, sizeof(again)) > 0);
        ASSERT_STR_EQ(again, line);
    }
}

TEST(nuz_parse_rejects_malformed_lines) {
    NuzEvent e;
    ASSERT_TRUE(!nuz_parse("", &e));
    ASSERT_TRUE(!nuz_parse("explode pt=1", &e));
    ASSERT_TRUE(!nuz_parse("wipe", &e));                    /* no pt */
    ASSERT_TRUE(!nuz_parse("wipe pt=abc", &e));
    ASSERT_TRUE(!nuz_parse("wipe pt=-1", &e));
    ASSERT_TRUE(!nuz_parse("wipe pt=99999999999", &e));       /* > u32: rejected, not clamped */
    ASSERT_TRUE(!nuz_parse("wipe pt=4294967296", &e));
    ASSERT_TRUE(!nuz_parse("wipe pt", &e));
    ASSERT_TRUE(!nuz_parse("enc pt=1 sec=999", &e));
    ASSERT_TRUE(!nuz_parse("enc pt=1 kind=sideways", &e));
    ASSERT_TRUE(!nuz_parse("death pt=1 key=12:34", &e));
    ASSERT_TRUE(!nuz_parse("gift pt=1 nick=ZZ", &e));
    ASSERT_TRUE(nuz_parse("wipe pt=5 future=thing\r\n", &e));  /* unknown keys are skipped */
    ASSERT_EQ(e.pt, 5);
}

TEST(nuz_replay_equals_live_state) {
    /* Drive a short run live, record the events as lines, replay them into a new run. */
    static char lines[256][256];
    int count = 0;
    setup();
    NuzMon* m = party_mon(0, PLAIN(1), 20, 20);
    party_mon(1, PLAIN(2), 21, 20);
    static NuzEvent bf[16];
    uint16_t nb = nuz_backfill(&s_f, SECS, bf, 16);
    for (uint16_t i = 0; i < nb; i++) {
        nuz_apply(&s_run, &bf[i]);
        nuz_format(&bf[i], lines[count++], 256);
    }
    nuz_tracker_reset(&s_t, &s_run);
    s_f.mapsec = 47;
    for (int frame = 0; frame < 200; frame++) {
        if (frame == 10) enter_battle(30, PLAIN(9), 0);
        if (frame == 20) party_mon(2, PLAIN(9), 30, 10);
        if (frame == 25) { s_f.battle_outcome = 7; }
        if (frame == 26) s_f.in_battle = false;
        if (frame == 60) m->hp = 0;
        if (frame == 120) s_f.play_time -= 100;
        s_n = nuz_track_step(&s_t, &s_run, &s_f, s_fam, SECS, s_ev);
        for (uint8_t i = 0; i < s_n; i++) {
            nuz_apply(&s_run, &s_ev[i]);
            ASSERT_TRUE(nuz_format(&s_ev[i], lines[count++], 256) > 0);
        }
        s_f.play_time++;
    }
    static NuzRun replay;
    nuz_run_init(&replay);
    for (int i = 0; i < count; i++) {
        NuzEvent e;
        ASSERT_TRUE(nuz_parse(lines[i], &e));
        nuz_apply(&replay, &e);
    }
    ASSERT_EQ(s_run.deaths, 1);
    ASSERT_EQ(s_run.reloads, 1);
    ASSERT_EQ(s_run.member_count, 3);
    ASSERT_TRUE(memcmp(&replay, &s_run, sizeof(replay)) == 0);
}

void run_nuzlocke_tests(void) {
    RUN_TEST(nuz_backfill_marks_owned_routes);
    RUN_TEST(nuz_first_encounter_then_second_is_extra);
    RUN_TEST(nuz_catch_joins_after_battle_and_links_route);
    RUN_TEST(nuz_shiny_and_dupe_leave_route_open);
    RUN_TEST(nuz_trainer_first_battle_and_wally_ignored);
    RUN_TEST(nuz_gift_does_not_use_route);
    RUN_TEST(nuz_move_to_pc_is_not_a_new_member);
    RUN_TEST(nuz_death_debounced_and_permanent);
    RUN_TEST(nuz_brief_zero_hp_is_not_a_death);
    RUN_TEST(nuz_bad_slot_is_never_a_death);
    RUN_TEST(nuz_battle_death_records_foe_and_deadused_once);
    RUN_TEST(nuz_wipe_logged_once);
    RUN_TEST(nuz_held_rewind_logs_one_reload_and_marks_death);
    RUN_TEST(nuz_one_frame_misread_is_not_a_reload);
    RUN_TEST(nuz_reroll_after_reload);
    RUN_TEST(nuz_battle_running_at_attach_is_ignored);
    RUN_TEST(nuz_format_parse_round_trip);
    RUN_TEST(nuz_parse_rejects_malformed_lines);
    RUN_TEST(nuz_replay_equals_live_state);
}
