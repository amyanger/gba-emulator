#include "test_harness.h"
#include "game_fixture.h"
#include "frontend/nuzlocke_session.h"
#include "frontend/run_session.h"
#include <stdio.h>
#include <string.h>

static uint8_t s_ewram[0x40000];
static uint8_t s_iwram[0x8000];
static uint8_t s_rom[0x8000];

#define SB1 0x02010000u
#define SB2 0x02000100u
#define TRAINERS 0x08004000u
#define ITEMS    0x08006000u
#define ROM "run_session_test.gba"
#define FILE_A "run_test_a.run"
#define FILE_B "run_test_b.run"
#define DOT "\xC2\xB7"

static const GameProfile k_profile = {
    .name = "test",
    .battle_type_flags = 0x02000E10, .battlers_count = 0x02000E00,
    .battler_party_indexes = 0x02000E08, .battler_positions = 0x02000E04,
    .battle_mons = 0x02001000, .battler_attacker = 0x02000E40, .trainer_battle_opponent_a = 0x02000E44,
    .player_party_count = 0x02000000, .player_party = 0x02000400, .enemy_party = 0x02000800,
    .main_in_battle = 0x03000010, .main_callback2 = 0x03000020, .cb2_whiteout = 0x08001235,
    .save_block1_ptr = 0x03000000, .save_block2_ptr = 0x03000004,
    .map_header = 0x02000E20, .battle_outcome = 0x02000E30, .pokemon_storage_ptr = 0x03000008,
    .species_names = 0x08000100, .species_count = 8, .species_info = 0x08000C00,
    .experience_tables = 0x08000D00, .evolution_table = 0x08002000,
    .region_map_entries = 0x08001C00, .region_map_count = 20,
    .trainers = TRAINERS, .trainer_count = 400,
    .items = ITEMS, .item_count = 20,
};

static uint8_t* at(uint32_t addr) {
    if (addr >= 0x08000000) return s_rom + (addr - 0x08000000);
    if (addr >= 0x03000000) return s_iwram + (addr - 0x03000000);
    return s_ewram + (addr - 0x02000000);
}

static GameMem mem(void) {
    GameMem m = { s_ewram, s_iwram, s_rom, sizeof(s_rom) };
    return m;
}

static void put_str(uint32_t addr, const char* s) {
    uint8_t* p = at(addr);
    for (; *s; s++, p++) *p = (*s == ' ') ? 0x00 : (uint8_t)(0xBB + (*s - 'A'));
    *p = 0xFF;
}

static uint32_t s_pt;

static void set_pt(uint32_t f) {
    uint32_t s = f / 60;
    s_pt = f;
    fixture_put16(at(SB2 + 0x0E), (uint16_t)(s / 3600));
    at(SB2 + 0x10)[0] = (uint8_t)(s / 60 % 60);
    at(SB2 + 0x11)[0] = (uint8_t)(s % 60);
    at(SB2 + 0x12)[0] = (uint8_t)(f % 60);
}

static void set_flag(uint16_t id) { at(SB1 + 0x1270 + (id >> 3))[0] |= (uint8_t)(1u << (id & 7)); }
static void set_badge(int n) { set_flag((uint16_t)(0x867 + n - 1)); }
static void clear_badge(int n) {
    uint16_t id = (uint16_t)(0x867 + n - 1);
    at(SB1 + 0x1270 + (id >> 3))[0] &= (uint8_t)~(1u << (id & 7));
}
static void set_tid(uint32_t tid) { fixture_put32(at(SB2 + 0x0A), tid); }
/* gameStats are stored ^ SB2.encryptionKey, which is 0 in this fixture. */
static void set_stat(int i, uint32_t v) { fixture_put32(at(SB1 + 0x159C + 4u * (uint32_t)i), v); }

static void put_mon(int slot, uint32_t pid, uint16_t species, const char* nick) {
    uint32_t addr = 0x02000400 + 100u * (uint32_t)slot;
    uint8_t plain[4][12];
    fixture_plain_mon(plain, species);
    plain[3][1] = 3;                                         /* met: ROUTE ONE */
    fixture_put16(plain[3] + 2, (uint16_t)(5 | (4u << 11))); /* met level 5, Poke Ball */
    fixture_encode_mon(at(addr), pid, 0x0000BEEF, plain, 0x02);
    put_str(addr + 0x08, nick);
    at(addr + 0x54)[0] = 5;
    fixture_put16(at(addr + 0x56), 12);
}

/* A save with 2 party mons and badges 1-2. */
static void build(void) {
    memset(s_ewram, 0, sizeof(s_ewram));
    memset(s_iwram, 0, sizeof(s_iwram));
    memset(s_rom, 0, sizeof(s_rom));
    fixture_put32(at(0x03000000), SB1);
    fixture_put32(at(0x03000004), SB2);
    fixture_put32(at(SB2 + 0x0A), 0x0000BEEF);
    set_pt(216000);  /* 1:00:00 */
    put_str(SB2, "MAY");
    at(0x02000E20 + 0x14)[0] = 3;
    put_str(0x08000100 + 11 * 2, "ZIGZAGOON");
    put_str(0x08000100 + 11 * 3, "POOCHYENA");
    fixture_put32(at(0x08001C00 + 8 * 3 + 4), 0x08001D00);
    put_str(0x08001D00, "ROUTE ONE");
    put_str(TRAINERS + 265u * 0x28 + 4, "ROXANNE");
    put_str(TRAINERS + 266u * 0x28 + 4, "BRAWLY");
    put_str(TRAINERS + 267u * 0x28 + 4, "WATTSON");
    static const char* const balls[] = { "MASTER BALL", "ULTRA BALL", "GREAT BALL", "POKE BALL",
                                         "SAFARI BALL", "NET BALL" };
    for (uint32_t i = 0; i < 6; i++) put_str(ITEMS + (i + 1) * 44, balls[i]);
    at(0x02000000)[0] = 2;
    put_mon(0, 0x11112222, 2, "ZIGGY");
    put_mon(1, 0x33334444, 3, "POOCH");
    set_badge(1);
    set_badge(2);
}

static NuzSession s_nuz;
static RunSession s_a, s_b;

static void start(RunSession* s, const char* rom, const char* fixed) {
    nuz_session_init(&s_nuz, &k_profile, NULL, NULL);
    run_session_init(s, &k_profile, rom, fixed);
}

/* n emulated frames, play time advancing step frames each. */
static void frames(RunSession* s, int n, uint32_t step) {
    GameMem m = mem();
    for (int i = 0; i < n; i++) {
        set_pt(s_pt + step);
        nuz_session_frame(&s_nuz, &m);
        run_session_frame(s, &m, &s_nuz.facts);
    }
}

/* Lines of path containing all of a and b (b may be NULL); -1 if the file is missing. */
static long lines_with(const char* path, const char* a, const char* b) {
    FILE* f = fopen(path, "r");
    char line[600];
    long n = 0;
    if (!f) return -1;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, a) && (!b || strstr(line, b))) n++;
    fclose(f);
    return n;
}

static long file_lines(const char* path) { return lines_with(path, "", NULL); }

static bool first_line_is(const char* path, const char* want) {
    FILE* f = fopen(path, "r");
    char line[64] = "";
    if (!f) return false;
    bool ok = fgets(line, sizeof(line), f) != NULL && strcmp(line, want) == 0;
    fclose(f);
    return ok;
}

TEST(run_session_attach_writes_start_and_backfill) {
    remove(FILE_A);
    build();
    start(&s_a, ROM, FILE_A);
    frames(&s_a, 120, 1);
    ASSERT_TRUE(s_a.attached);
    ASSERT_TRUE(first_line_is(FILE_A, "run 1\n"));
    ASSERT_EQ(lines_with(FILE_A, "start ", "tid=0000BEEF"), 1);
    ASSERT_EQ(lines_with(FILE_A, "join ", "how=backfill"), 2);
    ASSERT_EQ(lines_with(FILE_A, "badge ", "u=1"), 2);
    ASSERT_EQ(s_a.view.state, RUN_VIEW_RUN);
    ASSERT_EQ(s_a.view.badge_count, 2);
    ASSERT_EQ(s_a.view.team_count, 2);
    ASSERT_STR_EQ(s_a.view.team[0].name, "Ziggy");
    ASSERT_STR_EQ(s_a.view.team[0].species, "Zigzagoon");
    ASSERT_STR_EQ(s_a.view.team[0].joined, DOT);
    ASSERT_STR_EQ(s_a.view.team[0].where, "Route One");
    ASSERT_TRUE(s_a.view.team[0].in_party);
    ASSERT_STR_EQ(s_a.view.player, "May");
    ASSERT_STR_EQ(s_a.view.play_time, "1:00");
    ASSERT_TRUE(!s_a.view.write_failed);
    remove(FILE_A);
}

TEST(run_session_replay_restores) {
    remove(FILE_A);
    build();
    start(&s_a, ROM, FILE_A);
    frames(&s_a, 120, 1);
    long lines = file_lines(FILE_A);
    ASSERT_TRUE(lines >= 5);
    start(&s_b, ROM, FILE_A);
    frames(&s_b, 120, 1);
    ASSERT_TRUE(s_b.attached);
    ASSERT_EQ(file_lines(FILE_A), lines);
    ASSERT_EQ(s_b.log.member_count, s_a.log.member_count);
    ASSERT_EQ(memcmp(s_b.log.members, s_a.log.members, sizeof(RunMember) * s_a.log.member_count), 0);
    ASSERT_EQ(memcmp(s_b.log.badge, s_a.log.badge, sizeof(s_a.log.badge)), 0);
    ASSERT_EQ(memcmp(s_b.log.badge_untimed, s_a.log.badge_untimed, sizeof(s_a.log.badge_untimed)), 0);
    ASSERT_EQ(memcmp(s_b.log.badge_pt, s_a.log.badge_pt, sizeof(s_a.log.badge_pt)), 0);
    ASSERT_EQ(s_b.log.start_pt, s_a.log.start_pt);
    ASSERT_EQ(s_b.log.next_seq, s_a.log.next_seq);
    remove(FILE_A);
}

TEST(run_session_other_tid_ignored) {
    FILE* f = fopen(FILE_B, "w");
    ASSERT_TRUE(f != NULL);
    fputs("run 1\nstart pt=10 tid=0000CAFE\n", f);
    fclose(f);
    build();
    start(&s_a, ROM, FILE_B);
    frames(&s_a, 120, 1);
    ASSERT_TRUE(s_a.attached);
    ASSERT_TRUE(!s_a.path_ok);
    ASSERT_EQ(file_lines(FILE_B), 2);
    ASSERT_EQ_HEX(s_a.log.tid, 0x0000BEEF);   /* tracked in memory instead */
    ASSERT_EQ(s_a.log.member_count, 2);
    ASSERT_TRUE(s_a.view.write_failed);
    remove(FILE_B);
}

/* A crash mid-write leaves a last line with no newline: it is skipped, ended, and appends start clean. */
TEST(run_session_torn_last_line) {
    FILE* f = fopen(FILE_B, "w");
    ASSERT_TRUE(f != NULL);
    fputs("run 1\nstart pt=10 tid=0000BEEF\nbat", f);
    fclose(f);
    build();
    start(&s_a, ROM, FILE_B);
    frames(&s_a, 120, 1);
    ASSERT_TRUE(s_a.attached && s_a.path_ok && !s_a.write_failed);
    ASSERT_EQ(lines_with(FILE_B, "start ", NULL), 1);
    ASSERT_EQ(lines_with(FILE_B, "join ", "how=backfill"), 2);
    f = fopen(FILE_B, "r");
    ASSERT_TRUE(f != NULL);
    char line[600];
    bool torn_alone = false, last_nl = false;
    while (fgets(line, sizeof(line), f)) {
        if (strcmp(line, "bat\n") == 0) torn_alone = true;
        last_nl = line[strlen(line) - 1] == '\n';
    }
    fclose(f);
    ASSERT_TRUE(torn_alone);
    ASSERT_TRUE(last_nl);
    /* Replays to the same log without writing anything more. */
    long lines = file_lines(FILE_B);
    start(&s_b, ROM, FILE_B);
    frames(&s_b, 120, 1);
    ASSERT_EQ(file_lines(FILE_B), lines);
    ASSERT_EQ(s_b.log.member_count, s_a.log.member_count);
    remove(FILE_B);
}

TEST(run_session_view_badges_and_feed) {
    remove(FILE_A);
    build();
    memset(at(SB1 + 0x1270 + (0x867 >> 3)), 0, 2);
    set_badge(1);
    start(&s_a, ROM, FILE_A);
    frames(&s_a, 130, 1);            /* attach, resync ends after a box read */
    ASSERT_EQ(s_a.view.badge_count, 1);
    ASSERT_TRUE(s_a.view.badges[0].have && s_a.view.badges[0].untimed);
    ASSERT_STR_EQ(s_a.view.badges[0].split, DOT);
    ASSERT_STR_EQ(s_a.view.badges[0].name, "Stone");

    set_badge(2);
    frames(&s_a, 10, 100);
    fixture_put16(at(0x02000400 + 0x56), 0);   /* Ziggy faints */
    frames(&s_a, 40, 100);
    frames(&s_a, 22, 100);
    set_badge(3);                    /* 72 frames x 100 = 2 minutes after badge 2 */
    frames(&s_a, 10, 100);

    const RunView* v = &s_a.view;
    ASSERT_EQ(v->badge_count, 3);
    ASSERT_TRUE(!v->badges[1].untimed);
    ASSERT_STR_EQ(v->badges[1].split, "1:00");
    ASSERT_STR_EQ(v->badges[2].split, "+0:02");
    ASSERT_STR_EQ(v->badges[2].name, "Dynamo");
    ASSERT_TRUE(!v->badges[3].have);
    ASSERT_STR_EQ(v->badges[3].split, "");
    ASSERT_EQ(v->feed_count, 4);
    ASSERT_EQ(v->feed[0].icon, RUN_ICON_BADGE);
    ASSERT_STR_EQ(v->feed[0].text, "Dynamo Badge, beat Wattson");
    ASSERT_EQ(v->feed[1].icon, RUN_ICON_FAINT);
    ASSERT_STR_EQ(v->feed[1].text, "Ziggy fainted");
    ASSERT_STR_EQ(v->feed[1].where, "Route One");
    ASSERT_EQ(v->feed[2].icon, RUN_ICON_BADGE);
    ASSERT_STR_EQ(v->feed[2].text, "Knuckle Badge, beat Brawly");
    ASSERT_STR_EQ(v->feed[2].time, "1:00");
    ASSERT_EQ(v->feed[3].icon, RUN_ICON_BADGE);
    ASSERT_STR_EQ(v->feed[3].time, DOT);
    ASSERT_EQ(v->faints, 1);
    ASSERT_TRUE(v->faints_since_tracking);
    ASSERT_STR_EQ(v->tracking_since, "Tracking since 1:00");
    ASSERT_EQ(lines_with(FILE_A, "faint ", NULL), 1);
    remove(FILE_A);
}

/* path[] holds the longest name, so this is the write-failure path, not a path overflow. */
TEST(run_session_write_failure_keeps_memory) {
    static char rom[1024];
    memset(rom, 'a', sizeof(rom) - 1);
    rom[sizeof(rom) - 1] = '\0';
    build();
    start(&s_a, rom, NULL);
    frames(&s_a, 120, 1);
    /* path[] holds rom + ".0000BEEF.run", but no filesystem takes a 1023-char name: the
     * first write fails and the run stays in memory. */
    ASSERT_TRUE(s_a.attached);
    ASSERT_TRUE(s_a.write_failed || !s_a.path_ok);
    ASSERT_EQ(s_a.view.state, RUN_VIEW_RUN);
    ASSERT_TRUE(s_a.view.write_failed);
    ASSERT_EQ(s_a.view.team_count, 2);
}

TEST(run_session_off_without_profile) {
    run_session_init(&s_a, NULL, ROM, NULL);
    nuz_session_init(&s_nuz, NULL, NULL, NULL);
    frames(&s_a, 5, 1);
    ASSERT_EQ(s_a.view.state, RUN_VIEW_OFF);
    ASSERT_TRUE(!s_a.attached);
    NuzKey k;
    ASSERT_TRUE(!run_session_mvp_key(&s_a, &k));
}

/* ---- View logic, with events put straight into the log of a file-less session ---- */

#define KEY_ZIGGY 0x11112222u
#define KEY_POOCH 0x33334444u

static NuzKey key(uint32_t pid) {
    NuzKey k = { pid, 0x0000BEEF, 0 };
    return k;
}

static RunEvent ev_new(uint8_t type) {
    RunEvent e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.pt = s_pt;
    e.sec = NUZ_SEC_NONE;
    e.low = 1000;
    return e;
}

/* Apply e to s_a's log and run one frame so the view refreshes. */
static void inject(const RunEvent* e) {
    run_apply(&s_a.log, e);
    frames(&s_a, 1, 1);
}

static void join(uint32_t pid, uint16_t species, uint8_t how, uint8_t ball) {
    RunEvent e = ev_new(RUN_EV_JOIN);
    e.key = key(pid);
    e.species = species;
    e.level = 5;
    e.how = how;
    e.ball = ball;
    inject(&e);
}

static RunEvent battle(uint16_t foe) {
    RunEvent e = ev_new(RUN_EV_BATTLE);
    e.kind = RUN_BATTLE_WILD;
    e.foe = foe;
    e.outcome = 1;
    return e;
}

static void add_ko(RunEvent* e, uint32_t pid, uint16_t count, uint8_t level) {
    e->kos[e->ko_count].key = key(pid);
    e->kos[e->ko_count].count = count;
    e->kos[e->ko_count].level = level;
    e->ko_count++;
}

/* A file-less session attached to the fixture save, out of its resync window. */
static void ready(void) {
    build();
    start(&s_a, NULL, NULL);
    frames(&s_a, 130, 1);
}

static bool feed_has(const char* text) {
    for (uint8_t i = 0; i < s_a.view.feed_count; i++)
        if (strcmp(s_a.view.feed[i].text, text) == 0) return true;
    return false;
}

TEST(run_session_feed_needs_ram_bit) {
    ready();
    clear_badge(2);   /* e.g. an older save loaded: the untimed badge 2 event survived the reload */
    frames(&s_a, 30, 1);
    ASSERT_TRUE(s_a.log.badge[1]);
    ASSERT_TRUE(!feed_has("Knuckle Badge, beat Brawly"));
    ASSERT_TRUE(feed_has("Stone Badge, beat Roxanne"));
    RunEvent e = ev_new(RUN_EV_BADGE);
    e.id = 3;
    e.untimed = true;
    inject(&e);
    ASSERT_TRUE(!feed_has("Dynamo Badge, beat Wattson"));
    set_badge(3);
    frames(&s_a, 30, 1);
    ASSERT_TRUE(feed_has("Dynamo Badge, beat Wattson"));
    RunEvent st = ev_new(RUN_EV_STORY);
    st.id = RUN_STORY_E4;
    st.untimed = true;
    inject(&st);
    ASSERT_TRUE(!feed_has("Entered the Elite Four"));
}

TEST(run_session_view_waits_for_tid_switch) {
    ready();
    uint32_t version = s_a.view_version;
    uint8_t feed = s_a.view.feed_count;
    set_tid(0x0000CAFE);   /* another save: debouncing for 30 frames */
    join(0x77770001, 3, RUN_HOW_GIFT, 4);
    frames(&s_a, 20, 1);
    ASSERT_EQ(s_a.view_version, version);
    ASSERT_EQ(s_a.view.feed_count, feed);
    frames(&s_a, 20, 1);   /* switched: a new log for CAFE */
    ASSERT_EQ_HEX(s_a.tid, 0x0000CAFE);
    ASSERT_TRUE(s_a.view_version != version);
}

TEST(run_session_team_total) {
    ready();
    for (uint32_t i = 0; i < 50; i++) {
        RunEvent e = ev_new(RUN_EV_JOIN);
        e.key = key(0x50000000u + i);
        e.species = 3;
        e.how = RUN_HOW_GIFT;
        run_apply(&s_a.log, &e);
    }
    frames(&s_a, 1, 1);
    ASSERT_EQ(s_a.view.team_count, RUN_VIEW_TEAM);
    ASSERT_EQ(s_a.view.team_total, 52);
    ASSERT_TRUE(s_a.view.team[0].in_party);
    ASSERT_TRUE(s_a.view.team[2].away);
}

TEST(run_session_mvp) {
    ready();
    ASSERT_STR_EQ(s_a.view.mvp, "");   /* no KOs, no MVP */
    NuzKey mk;
    ASSERT_TRUE(!run_session_mvp_key(&s_a, &mk));
    join(0x77770001, 3, RUN_HOW_GIFT, 4);
    RunEvent b = battle(2);
    add_ko(&b, KEY_ZIGGY, 2, 7);       /* +2 levels */
    add_ko(&b, 0x77770001, 2, 10);     /* +5 levels: wins the tie */
    inject(&b);
    ASSERT_STR_EQ(s_a.view.mvp, "Poochyena" " \xC2\xB7 " "2 KOs, never fainted");
    ASSERT_TRUE(s_a.view.team[2].mvp && !s_a.view.team[0].mvp);
    ASSERT_TRUE(run_session_mvp_key(&s_a, &mk));
    ASSERT_TRUE(nuz_key_eq(mk, key(0x77770001)));
    RunEvent f = ev_new(RUN_EV_FAINT);
    f.key = key(0x77770001);
    f.species = 3;
    inject(&f);
    ASSERT_STR_EQ(s_a.view.mvp, "Poochyena" " \xC2\xB7 " "2 KOs");
    RunEvent b2 = battle(2);
    add_ko(&b2, KEY_ZIGGY, 1, 8);
    inject(&b2);
    /* Backfilled Ziggy never fainted either, but was not watched: no "never fainted". */
    ASSERT_STR_EQ(s_a.view.mvp, "Ziggy" " \xC2\xB7 " "3 KOs");
    ASSERT_TRUE(run_session_mvp_key(&s_a, &mk));
    ASSERT_TRUE(nuz_key_eq(mk, key(KEY_ZIGGY)));
}

TEST(run_session_closest_call) {
    ready();
    RunEvent b = battle(3);
    b.low = 101;
    b.low_by = key(KEY_ZIGGY);
    inject(&b);
    ASSERT_STR_EQ(s_a.view.closest, "");
    b.low = 100;
    inject(&b);
    ASSERT_STR_EQ(s_a.view.closest, "Ziggy held on with 10% HP vs Poochyena");
    RunEvent t = ev_new(RUN_EV_BATTLE);
    t.kind = RUN_BATTLE_OTHER;
    t.outcome = 1;
    t.low = 5;
    t.low_by = key(KEY_POOCH);
    inject(&t);
    ASSERT_STR_EQ(s_a.view.closest, "Pooch held on with under 1% HP");
}

TEST(run_session_caught_in_and_most_enc) {
    ready();
    ASSERT_STR_EQ(s_a.view.caught_in, "");
    ASSERT_STR_EQ(s_a.view.most_enc, "");
    static const uint8_t balls[] = { 4, 4, 4, 4, 4, 3, 3, 3, 1, 2, 5, 6 };
    for (uint32_t i = 0; i < sizeof(balls); i++) join(0x60000000u + i, 2, RUN_HOW_CAUGHT, balls[i]);
    join(0x60001000u, 2, RUN_HOW_GIFT, 1);   /* not caught: not counted */
    /* Ties go to the lower ball id; a piece that does not fit ends the list. */
    ASSERT_STR_EQ(s_a.view.caught_in, "5 Pok\xC3\xA9 \xC2\xB7 3 Great \xC2\xB7 1 Master \xC2\xB7 1 Ultra");
    RunEvent b = battle(3);
    inject(&b);
    b = battle(2);
    inject(&b);
    ASSERT_STR_EQ(s_a.view.most_enc, "Zigzagoon (1)");   /* tie: lower species id */
    b = battle(3);
    inject(&b);
    ASSERT_STR_EQ(s_a.view.most_enc, "Poochyena (2)");
}

TEST(run_session_fun_fact) {
    ready();
    ASSERT_STR_EQ(s_a.view.fun, "");
    set_stat(RUN_STAT_LEDGES, 1);
    set_stat(RUN_STAT_SPLASH, 3);
    frames(&s_a, 30, 1);
    ASSERT_EQ((s_pt / 7200u) % 2, 0);
    ASSERT_STR_EQ(s_a.view.fun, "Jumped 1 ledge");
    frames(&s_a, 72, 100);     /* 2 minutes later: the next nonzero fact */
    frames(&s_a, 30, 1);       /* let the 30-frame refresh run */
    ASSERT_EQ((s_pt / 7200u) % 2, 1);
    ASSERT_STR_EQ(s_a.view.fun, "Used Splash 3 times");
    set_stat(RUN_STAT_LEDGES, 0);
    frames(&s_a, 30, 1);
    ASSERT_STR_EQ(s_a.view.fun, "Used Splash 3 times");
}

TEST(run_session_feed_filter) {
    ready();
    uint8_t base = s_a.view.feed_count;   /* the two untimed badges */
    join(0x77770001, 3, RUN_HOW_BACKFILL, 4);
    RunEvent st = ev_new(RUN_EV_START);
    st.tid = 0x0000BEEF;
    inject(&st);
    RunEvent r = ev_new(RUN_EV_RELOAD);
    r.to_pt = s_pt;
    inject(&r);
    RunEvent b = battle(3);
    add_ko(&b, KEY_ZIGGY, 1, 6);
    inject(&b);
    ASSERT_EQ(s_a.view.feed_count, base);
    /* A plain trainer with an ace, as files written before the leader check have. */
    RunEvent p = ev_new(RUN_EV_BATTLE);
    p.kind = RUN_BATTLE_TRAINER;
    p.trainer = 100;
    p.outcome = 1;
    p.has_ace = true;
    p.ace_by = key(KEY_ZIGGY);
    p.ace_species = 3;
    inject(&p);
    ASSERT_EQ(s_a.view.feed_count, base);
    RunEvent t = ev_new(RUN_EV_BATTLE);
    t.kind = RUN_BATTLE_TRAINER;
    t.trainer = 266;
    t.outcome = 1;
    t.has_ace = true;
    t.ace_by = key(KEY_ZIGGY);
    t.ace_species = 3;
    inject(&t);
    join(0x77770002, 3, RUN_HOW_GIFT, 4);
    ASSERT_EQ(s_a.view.feed_count, base + 2);
    ASSERT_STR_EQ(s_a.view.feed[0].text, "Got Poochyena");
    ASSERT_EQ(s_a.view.feed[1].icon, RUN_ICON_ACE);
    ASSERT_STR_EQ(s_a.view.feed[1].text, "Brawly's Poochyena KO'd by Ziggy");
}

TEST(run_session_split_skips_untimed) {
    build();
    memset(at(SB1 + 0x1270 + (0x867 >> 3)), 0, 2);   /* no badges */
    start(&s_a, NULL, NULL);
    frames(&s_a, 130, 1);
    RunEvent e = ev_new(RUN_EV_BADGE);
    e.id = 1;
    e.pt = 216000 + 3600;        /* 1:01 */
    run_apply(&s_a.log, &e);
    e.id = 2;
    e.untimed = true;
    e.pt = 216000 + 3 * 3600;
    run_apply(&s_a.log, &e);
    e.id = 3;
    e.untimed = false;
    e.pt = 216000 + 5 * 3600;    /* 4 minutes after badge 1 */
    run_apply(&s_a.log, &e);
    set_badge(1);
    set_badge(2);
    set_badge(3);
    frames(&s_a, 30, 1);
    ASSERT_STR_EQ(s_a.view.badges[0].split, "1:01");
    ASSERT_STR_EQ(s_a.view.badges[1].split, DOT);
    ASSERT_STR_EQ(s_a.view.badges[2].split, "+0:04");
}

void run_run_session_tests(void) {
    RUN_TEST(run_session_attach_writes_start_and_backfill);
    RUN_TEST(run_session_replay_restores);
    RUN_TEST(run_session_other_tid_ignored);
    RUN_TEST(run_session_torn_last_line);
    RUN_TEST(run_session_view_badges_and_feed);
    RUN_TEST(run_session_write_failure_keeps_memory);
    RUN_TEST(run_session_feed_needs_ram_bit);
    RUN_TEST(run_session_view_waits_for_tid_switch);
    RUN_TEST(run_session_team_total);
    RUN_TEST(run_session_mvp);
    RUN_TEST(run_session_closest_call);
    RUN_TEST(run_session_caught_in_and_most_enc);
    RUN_TEST(run_session_fun_fact);
    RUN_TEST(run_session_feed_filter);
    RUN_TEST(run_session_split_skips_untimed);
    RUN_TEST(run_session_off_without_profile);
}
