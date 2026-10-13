#include "game/hunt.h"
#include "game/nuzlocke_read.h"
#include "game/rng3.h"
#include "test_harness.h"
#include <math.h>

#define OTID 0x59CDEE45u
#define SPECIES 286

/* Route 101 shaped grass with one species in every slot (rate 2880: every step battles), no
 * lead effects. */
static void overworld(HuntFacts* f) {
    memset(f, 0, sizeof(*f));
    f->valid = true;
    f->rng = 0x6226B90Fu;
    f->rand_count = 2349;
    f->otid = OTID;
    f->map_num = 16;
    f->on_overworld = true;
    f->predictable = true;
    f->wild.method = WILD_LAND;
    for (int i = 0; i < WILD_SLOTS; i++) {
        f->wild.slots[i].species = SPECIES;
        f->wild.slots[i].min_level = 2;
        f->wild.slots[i].max_level = 3;
        f->wild.slots[i].gender_ratio = 127;
    }
    f->wild.rate = 2880;
    f->wild.otid = OTID;
}

static void battle(HuntFacts* f, uint8_t kind, uint32_t pid, bool complete) {
    overworld(f);
    f->in_battle = true;
    f->on_overworld = false;
    f->predictable = false;
    f->battle_kind = kind;
    f->enemy_complete = complete;
    if (complete) {
        f->enemy_species = 290;
        f->enemy_pid = pid;
        f->enemy_ot = 0x12345678u;
        f->enemy_level = 3;
    }
}

/* n frames of the same facts; returns how many counted an encounter (last one in *ev). */
static int frames(HuntState* h, const HuntFacts* f, int n, bool rewinding, HuntEvent* ev) {
    int counted = 0;
    for (int i = 0; i < n; i++)
        if (hunt_frame(h, f, rewinding, ev))
            counted++;
    return counted;
}

TEST(hunt_counts_wild_once) {
    static HuntState h;
    static HuntFacts f;
    HuntEvent ev;
    hunt_init(&h);
    overworld(&f);
    ASSERT_EQ(frames(&h, &f, 3, false, &ev), 0);
    battle(&f, HUNT_BATTLE_WILD, 0xAAAA0001u, true);
    ASSERT_TRUE(!hunt_frame(&h, &f, false, &ev)); /* edge frame: pid seen once */
    ASSERT_TRUE(hunt_frame(&h, &f, false, &ev));  /* stable for 2 frames */
    ASSERT_EQ(ev.kind, HUNT_EV_ENCOUNTER);
    ASSERT_EQ(ev.species, 290);
    ASSERT_EQ_HEX(ev.pid, 0xAAAA0001u);
    ASSERT_EQ(ev.method, HUNT_METHOD_WILD);
    ASSERT_EQ(ev.shiny, nuz_is_shiny(0xAAAA0001u, 0x12345678u));
    ASSERT_EQ(h.total, 1);
    ASSERT_EQ(h.session, 1);
    ASSERT_EQ(frames(&h, &f, 50, false, &ev), 0); /* staying in battle */
    overworld(&f);
    ASSERT_EQ(frames(&h, &f, 3, false, &ev), 0);
    ASSERT_EQ(h.total, 1);

    /* Shiny uses the enemy's own OT id (review F8). */
    battle(&f, HUNT_BATTLE_WILD, 0x12345678u, true);
    ASSERT_EQ(frames(&h, &f, 2, false, &ev), 1);
    ASSERT_TRUE(ev.shiny);
    ASSERT_EQ(h.shiny_total, 1);
    ASSERT_EQ(h.total, 2);
}

TEST(hunt_waits_for_complete_enemy) {
    static HuntState h;
    static HuntFacts f;
    HuntEvent ev;
    hunt_init(&h);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0, false);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 0);
    battle(&f, HUNT_BATTLE_WILD, 0xBBBB0001u, true);
    ASSERT_TRUE(!hunt_frame(&h, &f, false, &ev));
    battle(&f, HUNT_BATTLE_WILD, 0xBBBB0002u, true); /* pid moved: start over */
    ASSERT_TRUE(!hunt_frame(&h, &f, false, &ev));
    ASSERT_TRUE(hunt_frame(&h, &f, false, &ev));
    ASSERT_EQ_HEX(ev.pid, 0xBBBB0002u);
    ASSERT_EQ(h.total, 1);
}

TEST(hunt_classify_by_saved_callback) {
    static HuntState h;
    static HuntFacts f;
    HuntEvent ev;
    hunt_init(&h);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_OTHER, 0xCCCC0001u, true);
    ASSERT_EQ(frames(&h, &f, 10, false, &ev), 0);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_STATIC, 0xCCCC0002u, true);
    ASSERT_EQ(frames(&h, &f, 10, false, &ev), 1);
    ASSERT_EQ(ev.method, HUNT_METHOD_STATIC);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_SAFARI, 0xCCCC0003u, true);
    ASSERT_EQ(frames(&h, &f, 10, false, &ev), 1);
    ASSERT_EQ(ev.method, HUNT_METHOD_SAFARI);
    ASSERT_EQ(h.total, 2);
}

TEST(hunt_dedupe_after_discontinuity) {
    static HuntState h;
    static HuntFacts f;
    HuntEvent ev;
    hunt_init(&h);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0xDDDD0001u, true);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 1);

    /* Load a state from before the battle and walk into the same battle again. */
    hunt_discontinuity(&h);
    overworld(&f);
    frames(&h, &f, 3, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0xDDDD0001u, true);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 0);
    ASSERT_EQ(h.total, 1);

    /* Load a state saved mid-battle: no edge, even with a new pid. */
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    hunt_discontinuity(&h);
    battle(&f, HUNT_BATTLE_WILD, 0xDDDD0002u, true);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 0);
    ASSERT_EQ(h.total, 1);

    /* A new step after the load is a new attempt. */
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0xDDDD0003u, true);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 1);
    ASSERT_EQ(h.total, 2);
}

TEST(hunt_rewinding_no_edges) {
    static HuntState h;
    static HuntFacts f;
    HuntEvent ev;
    hunt_init(&h);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0xEEEE0001u, true);
    ASSERT_EQ(frames(&h, &f, 5, true, &ev), 0);  /* rewound into a battle */
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 0); /* released inside it */
    ASSERT_EQ(h.total, 0);

    /* No counting while rewinding, even in a battle armed going forwards. */
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0xEEEE0002u, true);
    ASSERT_TRUE(!hunt_frame(&h, &f, false, &ev));
    ASSERT_EQ(frames(&h, &f, 5, true, &ev), 0);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 1);
    ASSERT_EQ(h.total, 1);
}

TEST(hunt_ignores_invalid_frames) {
    static HuntState h;
    static HuntFacts f, bad;
    HuntEvent ev;
    memset(&bad, 0, sizeof(bad));
    hunt_init(&h);
    overworld(&f);
    frames(&h, &f, 1, false, &ev);
    battle(&f, HUNT_BATTLE_WILD, 0xFFFF0001u, true);
    ASSERT_TRUE(!hunt_frame(&h, &f, false, &ev));
    ASSERT_EQ(frames(&h, &bad, 3, false, &ev), 0);
    ASSERT_TRUE(hunt_frame(&h, &f, false, &ev));
    ASSERT_EQ(frames(&h, &bad, 3, false, &ev), 0);
    ASSERT_EQ(frames(&h, &f, 5, false, &ev), 0);
    ASSERT_EQ(h.total, 1);
}

static HuntEvent enc(uint16_t species, bool shiny) {
    HuntEvent ev = {HUNT_EV_ENCOUNTER, species, 0x1000u + species, shiny, HUNT_METHOD_WILD};
    return ev;
}

TEST(hunt_tally_and_target) {
    static HuntState h;
    hunt_init(&h);
    static const uint16_t seq[] = {1, 2, 2, 3, 2, 3};
    for (int i = 0; i < 6; i++) {
        HuntEvent ev = enc(seq[i], i == 4);
        hunt_apply(&h, &ev);
    }
    ASSERT_EQ(h.total, 6);
    ASSERT_EQ(h.shiny_total, 1);
    ASSERT_EQ(h.session, 0); /* replay is not this session */
    ASSERT_EQ(h.tally_n, 3);
    ASSERT_EQ(h.tally_species[0], 2);
    ASSERT_EQ(h.tally_count[0], 3);
    ASSERT_EQ(h.tally_species[1], 3);
    ASSERT_EQ(h.tally_count[1], 2);
    ASSERT_EQ(h.tally_species[2], 1);
    ASSERT_EQ(h.tally_count[2], 1);

    for (uint16_t sp = 10; sp < 40; sp++) {
        HuntEvent ev = enc(sp, false);
        hunt_apply(&h, &ev);
    }
    ASSERT_EQ(h.tally_n, HUNT_TALLY_MAX);
    ASSERT_EQ(h.total, 36);

    h.target = 2;
    h.nature = 5;
    uint32_t v = h.version;
    hunt_reset_counts(&h);
    ASSERT_TRUE(h.version != v);
    ASSERT_EQ(h.total, 0);
    ASSERT_EQ(h.shiny_total, 0);
    ASSERT_EQ(h.tally_n, 0);
    ASSERT_EQ(h.target, 2);
    ASSERT_EQ(h.nature, 5);
}

TEST(hunt_scan_respects_budget) {
    static HuntState h;
    static HuntFacts f;
    hunt_init(&h);
    overworld(&f);
    h.target = 999; /* never on this table: nothing matches */
    hunt_scan(&h, &f, 100);
    ASSERT_EQ(h.scanned, 100);
    ASSERT_TRUE(!h.found);
    ASSERT_EQ(h.anchor_count, 2349);
    ASSERT_EQ_HEX(h.anchor_rng, 0x6226B90Fu);
    ASSERT_EQ_HEX(hunt_match_in(&h, &f), UINT32_MAX);
    hunt_scan(&h, &f, 100);
    ASSERT_EQ(h.scanned, 200);

    /* No work in battle, off the overworld, or when not predictable. */
    f.in_battle = true;
    hunt_scan(&h, &f, 100);
    f.in_battle = false;
    f.on_overworld = false;
    hunt_scan(&h, &f, 100);
    f.on_overworld = true;
    f.predictable = false;
    hunt_scan(&h, &f, 100);
    ASSERT_EQ(h.scanned, 200);
}

/* First advance from s whose step gives a certain shiny battle, by plain stepping. */
static uint32_t brute_first(const WildInput* in, uint32_t s, WildPreview* out) {
    for (uint32_t k = 0; k < HUNT_SCAN_LIMIT; k++, s = rng3_next(s)) {
        wildgen_step(in, s, out);
        if (out->kind == WG_BATTLE && out->certain && out->shiny)
            return k;
    }
    return UINT32_MAX;
}

/* At least one call: a passed match is still found until hunt_scan looks at it. */
static void scan_until_found(HuntState* h, const HuntFacts* f) {
    int i = 0;
    do
        hunt_scan(h, f, 16384);
    while (++i < 128 && !h->found);
}

TEST(hunt_scan_finds_known_match) {
    static HuntState h;
    static HuntFacts f;
    WildPreview want;
    hunt_init(&h);
    overworld(&f);
    uint32_t k = brute_first(&f.wild, f.rng, &want);
    ASSERT_TRUE(k != UINT32_MAX);
    scan_until_found(&h, &f);
    ASSERT_TRUE(h.found);
    ASSERT_EQ(hunt_match_in(&h, &f), k);
    ASSERT_EQ(h.match_at, 2349 + k);
    ASSERT_EQ_HEX(h.match.pid, want.pid);
    ASSERT_EQ(h.match.species, SPECIES);
    ASSERT_TRUE(hunt_matches(&h, &h.match));

    /* Filters: nature and target. */
    h.nature = (uint8_t)((want.nature + 1) % 25);
    ASSERT_TRUE(!hunt_matches(&h, &want));
    h.nature = HUNT_NATURE_ANY;
    h.target = SPECIES + 1;
    ASSERT_TRUE(!hunt_matches(&h, &want));
    h.target = SPECIES;
    ASSERT_TRUE(hunt_matches(&h, &want));
    want.certain = false;
    ASSERT_TRUE(!hunt_matches(&h, &want));
}

TEST(hunt_cache_follows_advances) {
    static HuntState h;
    static HuntFacts f;
    WildPreview want;
    hunt_init(&h);
    overworld(&f);
    uint32_t k = brute_first(&f.wild, f.rng, &want);
    ASSERT_TRUE(k > 50 && k != UINT32_MAX);
    scan_until_found(&h, &f);
    uint32_t scanned = h.scanned, v = h.version;

    f.rng = rng3_jump(f.rng, 50);
    f.rand_count += 50;
    ASSERT_EQ(hunt_match_in(&h, &f), k - 50);
    hunt_scan(&h, &f, 16384);
    ASSERT_EQ(h.anchor_count, 2349);
    ASSERT_EQ(h.scanned, scanned);
    ASSERT_EQ(h.version, v);

    /* Step past the match: the search goes on from there to the next one. */
    uint32_t past = k - 50 + 1;
    f.rng = rng3_jump(f.rng, past);
    f.rand_count += past;
    ASSERT_EQ_HEX(hunt_match_in(&h, &f), UINT32_MAX);
    uint32_t k2 = brute_first(&f.wild, f.rng, &want);
    ASSERT_TRUE(k2 != UINT32_MAX);
    scan_until_found(&h, &f);
    ASSERT_EQ(h.anchor_count, 2349);
    ASSERT_EQ(hunt_match_in(&h, &f), k2);
    ASSERT_EQ_HEX(h.match.pid, want.pid);
}

TEST(hunt_cache_rescans_on_jump) {
    static HuntState h;
    static HuntFacts f;
    hunt_init(&h);
    overworld(&f);
    scan_until_found(&h, &f);
    ASSERT_TRUE(h.found);

    /* State not on the anchor's sequence (soft reset, savestate) */
    f.rng = 0x01234567u;
    f.rand_count += 10;
    ASSERT_EQ_HEX(hunt_match_in(&h, &f), UINT32_MAX);
    hunt_scan(&h, &f, 100);
    ASSERT_EQ(h.anchor_count, 2359);
    ASSERT_EQ_HEX(h.anchor_rng, 0x01234567u);
    ASSERT_EQ(h.scanned, 100);

    /* Count going backwards */
    f.rand_count -= 5;
    f.rng = rng3_jump(0x01234567u, 3);
    hunt_scan(&h, &f, 10);
    ASSERT_EQ(h.anchor_count, 2354);
    ASSERT_EQ(h.scanned, 10);

    /* Filter, lead and Repel changes change the key */
    uint32_t key = h.key;
    h.target = SPECIES;
    hunt_scan(&h, &f, 10);
    ASSERT_TRUE(h.key != key);
    ASSERT_EQ(h.scanned, 10);
    key = h.key;
    f.wild.repel_active = true;
    f.wild.repel_level = 5;
    hunt_scan(&h, &f, 10);
    ASSERT_TRUE(h.key != key);
    key = h.key;
    f.wild.lead_ok = true;
    f.wild.lead_ability = 28;
    hunt_scan(&h, &f, 10);
    ASSERT_TRUE(h.key != key);
    key = h.key;
    f.otid = OTID + 1;
    hunt_scan(&h, &f, 10);
    ASSERT_TRUE(h.key != key);
    ASSERT_EQ(h.scanned, 10);

    /* A discontinuity drops the cache */
    hunt_discontinuity(&h);
    ASSERT_TRUE(!h.found);
    ASSERT_EQ_HEX(hunt_match_in(&h, &f), UINT32_MAX);
}

TEST(hunt_next_rows_follow_rng) {
    static HuntFacts f;
    WildPreview rows[HUNT_NEXT_ROWS], one;
    overworld(&f);
    hunt_next_rows(&f, rows);
    for (uint32_t i = 0; i < HUNT_NEXT_ROWS; i++) {
        wildgen_step(&f.wild, rng3_jump(f.rng, i), &one);
        ASSERT_EQ(rows[i].kind, WG_BATTLE);
        ASSERT_EQ_HEX(rows[i].pid, one.pid);
    }
    f.predictable = false;
    hunt_next_rows(&f, rows);
    for (int i = 0; i < HUNT_NEXT_ROWS; i++)
        ASSERT_EQ(rows[i].kind, WG_NONE);
}

TEST(hunt_chance_values) {
    ASSERT_TRUE(fabs(hunt_chance(1284) - 0.1451) < 0.0005);
    ASSERT_TRUE(hunt_chance(0) == 0.0);
    ASSERT_TRUE(fabs(hunt_chance(8192) - 0.6321) < 0.0005);
}

void run_hunt_tests(void) {
    RUN_TEST(hunt_counts_wild_once);
    RUN_TEST(hunt_waits_for_complete_enemy);
    RUN_TEST(hunt_classify_by_saved_callback);
    RUN_TEST(hunt_dedupe_after_discontinuity);
    RUN_TEST(hunt_rewinding_no_edges);
    RUN_TEST(hunt_ignores_invalid_frames);
    RUN_TEST(hunt_tally_and_target);
    RUN_TEST(hunt_scan_respects_budget);
    RUN_TEST(hunt_scan_finds_known_match);
    RUN_TEST(hunt_cache_follows_advances);
    RUN_TEST(hunt_cache_rescans_on_jump);
    RUN_TEST(hunt_next_rows_follow_rng);
    RUN_TEST(hunt_chance_values);
}
