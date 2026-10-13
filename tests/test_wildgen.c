#include "game/pokemon.h"
#include "game/rng3.h"
#include "game/wildgen.h"
#include "test_harness.h"

#define ABIL_STATIC 9
#define ABIL_INTIMIDATE 22
#define ABIL_CLEAR_BODY 29
#define TYPE_ELECTRIC 13

/* Route 101 land table from the fixtures file (species, min, max). There is no ROM in tests, so
 * types and gender ratios are placeholders: type 0 for every slot (no Steel/Electric species on
 * Route 101, and the fixture leads are Clear Body/Intimidate, so slot choice never reads types),
 * gender ratio 127 (only Cute Charm reads it, and no fixture lead has it). */
static void route101(WildInput* in, uint8_t ability, bool metatile) {
    static const uint16_t sp[WILD_SLOTS] = {290, 286, 290, 290, 286, 286,
                                            290, 286, 288, 288, 288, 288};
    static const uint8_t lv[WILD_SLOTS] = {2, 2, 2, 3, 3, 3, 3, 3, 2, 2, 3, 3};
    memset(in, 0, sizeof(*in));
    in->method = WILD_LAND;
    for (int i = 0; i < WILD_SLOTS; i++) {
        in->slots[i].species = sp[i];
        in->slots[i].min_level = in->slots[i].max_level = lv[i];
        in->slots[i].gender_ratio = 127;
    }
    in->rate = 20 * 16; /* encounter rate byte 20 */
    in->metatile_roll = metatile;
    in->lead_ok = true;
    in->lead_ability = ability;
    in->lead_level = 70;
    in->otid = 0x59CDEE45u;
}

typedef struct {
    const char* name;
    uint32_t s160;
    bool metatile;
    uint32_t slot_state;
    uint8_t ability;
    uint8_t slot;
    uint16_t species;
    uint8_t level, nature;
    uint16_t iters, skip;
    uint32_t pid, packed;
    uint8_t ivs[6];
} Fixture;

/* docs/superpowers/specs/2026-10-13-shiny-hunt-fixtures.md, battles only */
static const Fixture fixtures[] = {
    {"baseline r3",
     0x8E849797u,
     false,
     0x7C9F7E59u,
     ABIL_CLEAR_BODY,
     4,
     286,
     3,
     23,
     1,
     5,
     0x44C54C0Cu,
     0x38A7E046u,
     {6, 2, 24, 15, 10, 28}},
    {"baseline r8",
     0xA2A3FE4Eu,
     false,
     0x4F18B568u,
     ABIL_CLEAR_BODY,
     0,
     290,
     2,
     15,
     32,
     67,
     0xFB4D8248u,
     0x1A2CC2E4u,
     {4, 23, 16, 25, 2, 13}},
    {"baseline r14",
     0xCE435690u,
     false,
     0xEC4F9C7Au,
     ABIL_CLEAR_BODY,
     1,
     286,
     2,
     18,
     53,
     109,
     0x378AC42Cu,
     0x1484CF53u,
     {19, 26, 19, 9, 8, 10}},
    {"baseline r0",
     0x8748B331u,
     false,
     0xA91D7483u,
     ABIL_CLEAR_BODY,
     7,
     286,
     3,
     1,
     94,
     115,
     0x601534C5u,
     0x08FF8EB5u,
     {21, 21, 3, 31, 15, 4}},
    {"intimidate r1",
     0x5BEE4E7Au,
     false,
     0x16A60F74u,
     ABIL_INTIMIDATE,
     5,
     286,
     3,
     14,
     37,
     78,
     0x184A462Eu,
     0x2FFA4616u,
     {22, 16, 17, 20, 31, 23}},
    {"intimidate r22",
     0x9586AE9Bu,
     true,
     0xB493802Cu,
     ABIL_INTIMIDATE,
     5,
     286,
     3,
     15,
     2,
     8,
     0x65813651u,
     0x33BC53FCu,
     {28, 31, 20, 24, 27, 25}},
};
#define NUM_FIXTURES (int)(sizeof(fixtures) / sizeof(fixtures[0]))

static int check_preview(const Fixture* f, const WildPreview* p) {
    if (p->kind != WG_BATTLE || p->slot != f->slot || p->species != f->species ||
        p->level != f->level || p->nature != f->nature || p->pid != f->pid)
        return 0;
    uint8_t packed[6];
    pokemon_unpack_ivs(f->packed, packed);
    for (int i = 0; i < 6; i++)
        if (p->ivs[i] != f->ivs[i] || p->ivs[i] != packed[i])
            return 0;
    return 1;
}

TEST(wildgen_fixtures_from_slot_state) {
    for (int i = 0; i < NUM_FIXTURES; i++) {
        const Fixture* f = &fixtures[i];
        WildInput in;
        WildPreview p;
        route101(&in, f->ability, f->metatile);
        wildgen_from_slot_state(&in, f->slot_state, f->skip, &p);
        if (!check_preview(f, &p))
            printf("\n    mismatch: %s pid %08X", f->name, (unsigned)p.pid);
        ASSERT_TRUE(check_preview(f, &p));
        ASSERT_EQ(p.loop_iters, f->iters);
        ASSERT_EQ(p.certain, f->iters <= 14);
        ASSERT_TRUE(!p.shiny);
    }
}

/* Every fixture except r0 had the VBlank skip right after the PID, which is what wildgen_step
 * models. r0's skip fell inside its 94-iteration loop, so the step model must say "uncertain". */
TEST(wildgen_fixtures_step) {
    for (int i = 0; i < NUM_FIXTURES; i++) {
        const Fixture* f = &fixtures[i];
        WildInput in;
        WildPreview p;
        route101(&in, f->ability, f->metatile);
        wildgen_step(&in, f->s160, &p);
        if (f->skip == 3 + (f->ability == ABIL_INTIMIDATE) + 2 * f->iters) {
            if (!check_preview(f, &p))
                printf("\n    mismatch: %s pid %08X", f->name, (unsigned)p.pid);
            ASSERT_TRUE(check_preview(f, &p));
            ASSERT_EQ(p.loop_iters, f->iters);
            ASSERT_EQ(p.certain, f->iters <= 14);
        } else {
            ASSERT_EQ(p.kind, WG_BATTLE);
            ASSERT_TRUE(!p.certain);
        }
    }
}

/* Intimidate r10: level-2 Poochyena, lead 70, abort draw 0x9F3C % 2 == 0. */
TEST(wildgen_intimidate_abort) {
    WildInput in;
    WildPreview p;
    route101(&in, ABIL_INTIMIDATE, true);
    wildgen_step(&in, 0xD02F1C1Cu, &p);
    ASSERT_EQ(p.kind, WG_NO_BATTLE);
    wildgen_from_slot_state(&in, 0xD610A861u, UINT16_MAX, &p);
    ASSERT_EQ(p.kind, WG_NO_BATTLE);
    /* Same roll with a lead too low to trigger the check: the battle goes ahead. */
    in.lead_level = 6; /* level 2 > 6 - 5: no check */
    wildgen_from_slot_state(&in, 0xD610A861u, UINT16_MAX, &p);
    ASSERT_EQ(p.kind, WG_BATTLE);
    ASSERT_EQ(p.slot, 1);
    ASSERT_EQ(p.level, 2);
}

TEST(wildgen_long_loop_uncertain) {
    WildInput in;
    WildPreview p;
    route101(&in, ABIL_CLEAR_BODY, false);
    int found = 0;
    for (uint32_t s = 0; s < 1000 && !found; s++) {
        wildgen_from_slot_state(&in, s * 0x9E3779B9u, UINT16_MAX, &p);
        if (p.loop_iters > 14) {
            ASSERT_TRUE(!p.certain);
            found = 1;
        } else {
            ASSERT_TRUE(p.certain);
        }
    }
    ASSERT_TRUE(found);
}

TEST(wildgen_rate_roll_blocks) {
    WildInput in;
    WildPreview p;
    route101(&in, ABIL_CLEAR_BODY, false);
    in.rate = 0;
    for (uint32_t s = 0; s < 2000; s++) {
        wildgen_step(&in, s * 0x9E3779B9u, &p);
        ASSERT_EQ(p.kind, WG_NO_BATTLE);
    }
}

TEST(wildgen_roamer_quarter) {
    WildInput in;
    WildPreview p;
    route101(&in, ABIL_CLEAR_BODY, false);
    in.rate = 2880;
    in.roamer_here = true;
    in.roamer_species = 408;
    in.roamer_level = 40;
    int roamers = 0, battles = 0;
    for (uint32_t s = 0; s < 4000; s++) {
        wildgen_step(&in, s * 0x9E3779B9u, &p);
        if (p.kind == WG_ROAMER) {
            roamers++;
            ASSERT_EQ(p.species, 408);
            ASSERT_EQ(p.level, 40);
            ASSERT_TRUE(p.certain);
        }
        if (p.kind == WG_ROAMER || p.kind == WG_BATTLE)
            battles++;
    }
    ASSERT_EQ(battles, 4000);
    ASSERT_TRUE(roamers * 100 >= battles * 20 && roamers * 100 <= battles * 30);
}

/* StandardWildEncounter (wild_encounter.c:604): a roamer hit under Repel checks the roamer's
 * level and ends with no battle when it is lower than the lead's; no extra draw. */
TEST(wildgen_roamer_blocked_by_repel) {
    WildInput in;
    WildPreview p, open;
    route101(&in, ABIL_CLEAR_BODY, false);
    in.rate = 2880;
    in.roamer_here = true;
    in.roamer_species = 408;
    in.roamer_level = 40;
    in.repel_active = true;
    in.repel_level = 41;
    int blocked = 0;
    for (uint32_t s = 0; s < 4000; s++) {
        in.repel_active = false;
        wildgen_step(&in, s * 0x9E3779B9u, &open);
        in.repel_active = true;
        wildgen_step(&in, s * 0x9E3779B9u, &p);
        ASSERT_TRUE(p.kind != WG_ROAMER);
        if (open.kind == WG_ROAMER) {
            ASSERT_EQ(p.kind, WG_NO_BATTLE);
            ASSERT_TRUE(p.certain);
            blocked++;
        }
    }
    ASSERT_TRUE(blocked > 0);
    in.repel_level = 40; /* equal level passes */
    for (uint32_t s = 0; s < 64; s++) {
        in.repel_active = false;
        wildgen_step(&in, s * 0x9E3779B9u, &open);
        in.repel_active = true;
        wildgen_step(&in, s * 0x9E3779B9u, &p);
        if (open.kind == WG_ROAMER)
            ASSERT_EQ(p.kind, WG_ROAMER);
    }
}

/* Static lead: draw % 2 == 0 picks validIndexes[draw % n] of the Electric slots
 * (TryGetRandomWildMonIndexByType); otherwise the normal % 100 slot roll. */
TEST(wildgen_static_picks_electric) {
    WildInput in;
    WildPreview p;
    route101(&in, ABIL_STATIC, false);
    in.slots[3].type2 = TYPE_ELECTRIC;
    in.slots[7].type1 = TYPE_ELECTRIC;
    in.slots[10].type1 = TYPE_ELECTRIC;
    static const uint8_t electric[3] = {3, 7, 10};
    int pulled = 0;
    for (uint32_t s = 0; s < 500; s++) {
        uint32_t st = s * 0x9E3779B9u;
        uint32_t a = rng3_next(st), b = rng3_next(a);
        wildgen_from_slot_state(&in, st, UINT16_MAX, &p);
        ASSERT_EQ(p.kind, WG_BATTLE);
        if (rng3_hi(a) % 2 == 0) {
            ASSERT_EQ(p.slot, electric[rng3_hi(b) % 3]);
            pulled++;
        }
    }
    ASSERT_TRUE(pulled > 0);
    /* All 12 slots Electric: the type pick declines and the % 100 roll decides. */
    for (int i = 0; i < WILD_SLOTS; i++)
        in.slots[i].type1 = TYPE_ELECTRIC;
    for (uint32_t s = 0; s < 200; s++) {
        uint32_t st = s * 0x9E3779B9u;
        uint32_t a = rng3_next(st), b = rng3_next(a);
        if (rng3_hi(a) % 2 != 0)
            continue;
        wildgen_from_slot_state(&in, st, UINT16_MAX, &p);
        uint16_t r = rng3_hi(b) % 100;
        ASSERT_EQ(p.slot, r < 20   ? 0
                          : r < 40 ? 1
                          : r < 50 ? 2
                          : r < 60 ? 3
                          : r < 70 ? 4
                          : r < 80 ? 5
                          : r < 85 ? 6
                          : r < 90 ? 7
                          : r < 94 ? 8
                          : r < 98 ? 9
                          : r < 99 ? 10
                                   : 11);
    }
}

/* include/pokemon.h GET_SHINY_VALUE < 8, otid 0x59CDEE45 from the fixtures */
TEST(wildgen_shiny_formula) {
    ASSERT_TRUE(wildgen_shiny(0x59CDEE45u, 0x59CDEE45u));  /* xor 0 */
    ASSERT_TRUE(wildgen_shiny(0x59CDEE42u, 0x59CDEE45u));  /* xor 7 */
    ASSERT_TRUE(!wildgen_shiny(0x59CDEE4Du, 0x59CDEE45u)); /* xor 8 */
    ASSERT_TRUE(wildgen_shiny(0xEE4559CDu, 0x59CDEE45u));  /* halves swapped, xor 0 */
    ASSERT_TRUE(!wildgen_shiny(0x44C54C0Cu, 0x59CDEE45u)); /* fixture r3 */
}

void run_wildgen_tests(void) {
    RUN_TEST(wildgen_fixtures_from_slot_state);
    RUN_TEST(wildgen_fixtures_step);
    RUN_TEST(wildgen_intimidate_abort);
    RUN_TEST(wildgen_long_loop_uncertain);
    RUN_TEST(wildgen_rate_roll_blocks);
    RUN_TEST(wildgen_roamer_quarter);
    RUN_TEST(wildgen_roamer_blocked_by_repel);
    RUN_TEST(wildgen_static_picks_electric);
    RUN_TEST(wildgen_shiny_formula);
}
