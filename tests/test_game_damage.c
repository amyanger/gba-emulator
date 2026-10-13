#include "test_harness.h"
#include "game/damage.h"
#include "game/damage_read.h"

/* Fixtures from docs damage-formula.md section 6 and damage-fixtures-verified.md.
 * Types: NORMAL 0, FIGHTING 1, FLYING 2, GROUND 4, ROCK 5, BUG 6, GHOST 7, STEEL 8, FIRE 10,
 * WATER 11, GRASS 12, ELECTRIC 13, PSYCHIC 14, ICE 15, DRAGON 16. */
enum { NOR = 0, FIG = 1, FLY = 2, GRO = 4, ROC = 5, BUG = 6, GHO = 7, STE = 8, FIR = 10,
       WAT = 11, GRA = 12, ELE = 13, PSY = 14, ICE = 15, DRA = 16 };

/* Game-code ids used by the fixtures (pret include/constants/). */
/* battle_move_effects.h */
#define EFF_HIT 0
#define EFF_BURN_HIT 4
#define EFF_PARALYZE_HIT 6
#define EFF_EXPLOSION 7
#define EFF_DREAM_EATER 8
#define EFF_MULTI_HIT 29
#define EFF_OHKO 38
#define EFF_SUPER_FANG 40
#define EFF_DRAGON_RAGE 41
#define EFF_TRAP 42
#define EFF_RECOIL 48
#define EFF_SPDEF_DOWN_HIT 72
#define EFF_LEVEL_DAMAGE 87
#define EFF_PSYWAVE 88
#define EFF_SNORE 92
#define EFF_FLAIL 99
#define EFF_FALSE_SWIPE 101
#define EFF_TRIPLE_KICK 104
#define EFF_RETURN 121
#define EFF_FRUSTRATION 123
#define EFF_MAGNITUDE 126
#define EFF_SONICBOOM 130
#define EFF_HIDDEN_POWER 135
#define EFF_TWISTER 146
#define EFF_EARTHQUAKE 147
#define EFF_GUST 149
#define EFF_FLINCH_MINIMIZE_HIT 150
#define EFF_SOLAR_BEAM 151
#define EFF_THUNDER 152
#define EFF_FAKE_OUT 158
#define EFF_SPIT_UP 161
#define EFF_FACADE 169
#define EFF_SMELLINGSALT 171
#define EFF_BRICK_BREAK 186
#define EFF_ENDEAVOR 189
#define EFF_ERUPTION 190
#define EFF_LOW_KICK 196
#define EFF_WEATHER_BALL 203
/* moves.h */
#define MV_FIRE_PUNCH 7
#define MV_GUST 16
#define MV_STOMP 23
#define MV_FURY_ATTACK 31
#define MV_TACKLE 33
#define MV_SONIC_BOOM 49
#define MV_EMBER 52
#define MV_FLAMETHROWER 53
#define MV_HYDRO_PUMP 56
#define MV_SURF 57
#define MV_LOW_KICK 67
#define MV_SEISMIC_TOSS 69
#define MV_SOLAR_BEAM 76
#define MV_DRAGON_RAGE 82
#define MV_THUNDERBOLT 85
#define MV_THUNDER 87
#define MV_EARTHQUAKE 89
#define MV_FISSURE 90
#define MV_PSYCHIC 94
#define MV_NIGHT_SHADE 101
#define MV_DREAM_EATER 138
#define MV_PSYWAVE 149
#define MV_EXPLOSION 153
#define MV_SUPER_FANG 162
#define MV_STRUGGLE 165
#define MV_TRIPLE_KICK 167
#define MV_SNORE 173
#define MV_FLAIL 175
#define MV_FALSE_SWIPE 206
#define MV_RETURN 216
#define MV_FRUSTRATION 218
#define MV_MAGNITUDE 222
#define MV_MEGAHORN 224
#define MV_HIDDEN_POWER 237
#define MV_TWISTER 239
#define MV_WHIRLPOOL 250
#define MV_FAKE_OUT 252
#define MV_SPIT_UP 255
#define MV_FACADE 263
#define MV_SMELLING_SALT 265
#define MV_BRICK_BREAK 280
#define MV_ENDEAVOR 283
#define MV_ERUPTION 284
#define MV_HYPER_VOICE 304
#define MV_METEOR_MASH 309
#define MV_WEATHER_BALL 311
#define MV_SHEER_COLD 329
/* abilities.h */
#define AB_STURDY 5
#define AB_VOLT_ABSORB 10
#define AB_WATER_ABSORB 11
#define AB_FLASH_FIRE 18
#define AB_WONDER_GUARD 25
#define AB_LEVITATE 26
#define AB_HUGE_POWER 37
#define AB_SOUNDPROOF 43
#define AB_THICK_FAT 47
#define AB_HUSTLE 55
#define AB_PLUS 57
#define AB_MINUS 58
#define AB_GUTS 62
#define AB_MARVEL_SCALE 63
#define AB_BLAZE 66
#define AB_SWARM 68
#define AB_PURE_POWER 74
/* hold_effects.h, items.h, species.h */
#define HE_CHOICE_BAND 29
#define HE_SOUL_DEW 34
#define HE_WATER_POWER 52
#define HE_METAL_POWDER 64
#define ITEM_ENIGMA 175
#define ITEM_MYSTIC_WATER 209
#define SP_DITTO 132
#define SP_LATIAS 407
#define SP_LATIOS 408
/* battle.h status and field bits */
#define ST1_SLEEP_2 0x02u
#define ST1_POISON 0x08u
#define ST1_BURN 0x10u
#define ST1_FREEZE 0x20u
#define ST1_PARALYSIS 0x40u
#define ST2_MULTIPLETURNS (1u << 12)
#define ST2_SUBSTITUTE (1u << 24)
#define ST2_FORESIGHT (1u << 29)
#define ST3_ON_AIR (1u << 6)
#define ST3_UNDERGROUND (1u << 7)
#define ST3_MINIMIZED (1u << 8)
#define ST3_CHARGED_UP (1u << 9)
#define ST3_UNDERWATER (1u << 18)
#define WX_RAIN 0x01u          /* B_WEATHER_RAIN_TEMPORARY */
#define WX_RAIN_DRIZZLE 0x05u  /* TEMPORARY | PERMANENT */
#define WX_SANDSTORM 0x08u
#define WX_SUN 0x20u
#define WX_HAIL 0x80u
#define SIDE_REFLECT 0x01u
#define SIDE_LIGHTSCREEN 0x02u
#define BT_DOUBLE 0x01u
#define BT_SAFARI 0x80u
#define BT_BATTLE_TOWER 0x100u /* part of BATTLE_TYPE_FRONTIER */
#define TGT_SELECTED 0x00
#define TGT_BOTH 0x08
#define TGT_FOES_AND_ALLY 0x20

/* Only the rows the fixtures need, in the same relative order as gTypeEffectiveness. */
static const uint8_t k_chart[] = {
    NOR, ROC, 5,   NOR, STE, 5,
    FIR, WAT, 5,   FIR, ICE, 20,  FIR, BUG, 20,  FIR, DRA, 5,
    WAT, FIR, 20,  WAT, GRO, 20,
    ELE, WAT, 20,  ELE, ELE, 5,   ELE, GRO, 0,
    GRO, FLY, 0,   GRO, STE, 20,
    PSY, FIG, 20,
    BUG, PSY, 20,
    GHO, NOR, 0,   GHO, GHO, 20,
    STE, WAT, 5,   STE, ICE, 20,
    0xFE, 0xFE, 0,
    NOR, GHO, 0,   FIG, GHO, 0,
    0xFF, 0xFF, 0,
};

static DamageTables tables(void) {
    static const uint8_t ratio[13][2] = {
        {10, 40}, {10, 35}, {10, 30}, {10, 25}, {10, 20}, {10, 15}, {10, 10},
        {15, 10}, {20, 10}, {25, 10}, {30, 10}, {35, 10}, {40, 10},
    };
    /* sFlailHpScaleToPowerTable and sWeightToDamageTable (battle_script_commands.c:749, 774) */
    static const uint8_t flail[6][2] = {{1, 200}, {4, 150}, {9, 100}, {16, 80}, {32, 40}, {48, 20}};
    static const uint16_t weight[6][2] = {
        {100, 20}, {250, 40}, {500, 60}, {1000, 80}, {2000, 100}, {0xFFFF, 0xFFFF},
    };
    DamageTables t;
    memset(&t, 0, sizeof(t));
    t.valid = true;
    memcpy(t.type_chart, k_chart, sizeof(k_chart));
    t.type_len = sizeof(k_chart);
    memcpy(t.stat_ratio, ratio, sizeof(ratio));
    t.hold_type[0][0] = HE_WATER_POWER;
    t.hold_type[0][1] = WAT;
    t.hold_type_count = 1;
    memcpy(t.flail, flail, sizeof(flail));
    memcpy(t.weight, weight, sizeof(weight));
    return t;
}

/* battler 0/2 player side, 1/3 enemy side. Stats are final in-battle stats. */
static DamageMon mon(uint8_t battler, uint8_t t1, uint8_t t2, uint8_t level) {
    DamageMon m;
    memset(&m, 0, sizeof(m));
    m.battler = battler;
    m.side = battler & 1;
    m.species = 1;
    m.type1 = t1;
    m.type2 = t2;
    m.level = level;
    m.hp = m.max_hp = 100;
    m.attack = m.defense = m.speed = m.sp_attack = m.sp_defense = 100;
    for (int i = 0; i < 8; i++) m.stages[i] = 6;
    m.first_turn = true;
    return m;
}

/* Player with the given badge bits (bit 0 Stone, 4 Balance, 6 Mind). */
static void badges(DamageMon* m, uint8_t bits) {
    m->badges_apply = true;
    m->badges = bits;
}

static DamageMove move(uint8_t effect, uint8_t power, uint8_t type, uint8_t target) {
    DamageMove mv;
    memset(&mv, 0, sizeof(mv));
    mv.effect = effect;
    mv.power = power;
    mv.type = type;
    mv.accuracy = 100;
    mv.target = target;
    return mv;
}

static DamageCtx ctx0(void) {
    DamageCtx c;
    memset(&c, 0, sizeof(c));
    c.present_on_side[0] = c.present_on_side[1] = 1;
    return c;
}

static DamageResult run(const DamageCtx* c, const DamageMon* a, const DamageMon* d, uint16_t id,
                        const DamageMove* mv) {
    DamageTables t = tables();
    DamageResult r;
    damage_predict(&t, c, a, d, id, mv, &r);
    return r;
}

/* Each takes the result into a local first: no member access on a struct returned by value (MSVC C4223). */
#define ASSERT_RANGE(r, l, h) do { \
    DamageResult r_ = (r); \
    ASSERT_EQ(r_.kind, DMG_RANGE); \
    ASSERT_EQ(r_.lo, (l)); \
    ASSERT_EQ(r_.hi, (h)); \
} while (0)

#define ASSERT_FIXED(r, v) do { \
    DamageResult r_ = (r); \
    ASSERT_EQ(r_.kind, DMG_FIXED); \
    ASSERT_EQ(r_.lo, (v)); \
    ASSERT_EQ(r_.hi, (v)); \
} while (0)

#define ASSERT_KIND(r, k) do { \
    DamageResult r_ = (r); \
    ASSERT_EQ(r_.kind, (k)); \
} while (0)

/* Ex 1: player Swampert Lv70 Atk 180, Stone Badge, EQ vs enemy Metagross Def 208. */
static void ex1(DamageMon* a, DamageMon* d, DamageMove* mv) {
    *a = mon(0, WAT, GRO, 70);
    a->attack = 180;
    badges(a, 0xFF);
    *d = mon(1, STE, PSY, 70);
    d->defense = 208;
    d->hp = d->max_hp = 213;
    *mv = move(EFF_EARTHQUAKE, 100, GRO, TGT_FOES_AND_ALLY);
}

TEST(ex1_earthquake_vs_metagross) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    DamageResult r = run(&c, &a, &d, MV_EARTHQUAKE, &mv);
    ASSERT_RANGE(r, 149, 176);
    ASSERT_EQ(r.type, GRO);
    ASSERT_TRUE(!r.no_ko);
}

TEST(ex1b_defense_plus_two) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    d.stages[2] = 8;
    ASSERT_RANGE(run(&c, &a, &d, MV_EARTHQUAKE, &mv), 76, 90);
}

/* Without the badge gate: 180*100*30/208 = 2596, /50 = 51, +2 = 53, STAB 79, x2 158; 158*85/100 = 134. */
TEST(badges_gate_off_lowers_ex1) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    a.badges_apply = false;
    ASSERT_RANGE(run(&c, &a, &d, MV_EARTHQUAKE, &mv), 134, 158);
}

/* Ex 2: enemy Gardevoir SpA 201 Psychic vs player Breloom SpD 110 (Mind Badge), Light Screen, rain. */
static void ex2(DamageMon* a, DamageMon* d, DamageMove* mv, DamageCtx* c) {
    *a = mon(1, PSY, PSY, 70);
    a->sp_attack = 201;
    *d = mon(0, GRA, FIG, 70);
    d->sp_defense = 110;
    badges(d, 0xFF);
    *mv = move(EFF_SPDEF_DOWN_HIT, 90, PSY, TGT_SELECTED);
    *c = ctx0();
    c->side_status[0] = SIDE_LIGHTSCREEN;
    c->weather = WX_RAIN;
}

TEST(ex2_psychic_light_screen_rain) {
    DamageMon a, d; DamageMove mv; DamageCtx c;
    ex2(&a, &d, &mv, &c);
    ASSERT_RANGE(run(&c, &a, &d, MV_PSYCHIC, &mv), 117, 138);
    /* Rain never touches Psychic, so negating it changes nothing here. */
    c.weather_negated = true;
    ASSERT_RANGE(run(&c, &a, &d, MV_PSYCHIC, &mv), 117, 138);
}

/* Ex 3: player Swampert SpA 145, Mind Badge, Mystic Water, Surf in doubles with both foes
 * present, rain, vs enemy Camerupt SpD 131. */
static void ex3(DamageMon* a, DamageMon* d, DamageMove* mv, DamageCtx* c) {
    *a = mon(0, WAT, GRO, 70);
    a->sp_attack = 145;
    a->item = ITEM_MYSTIC_WATER;
    a->hold_effect = HE_WATER_POWER;
    a->hold_param = 10;
    badges(a, 0xFF);
    *d = mon(1, FIR, GRO, 70);
    d->sp_defense = 131;
    d->hp = d->max_hp = 199;
    *mv = move(EFF_HIT, 95, WAT, TGT_BOTH);
    *c = ctx0();
    c->battle_flags = BT_DOUBLE;
    c->present_on_side[0] = c->present_on_side[1] = 2;
    c->weather = WX_RAIN;
}

TEST(ex3_surf_doubles_rain_mystic_water) {
    DamageMon a, d; DamageMove mv; DamageCtx c;
    ex3(&a, &d, &mv, &c);
    ASSERT_RANGE(run(&c, &a, &d, MV_SURF, &mv), 289, 340);
}

/* Cloud Nine/Air Lock: spread 37, no rain boost, +2 = 39, STAB 58, x2 116, x2 232; 232*85/100 = 197. */
TEST(cloud_nine_negates_rain_on_surf) {
    DamageMon a, d; DamageMove mv; DamageCtx c;
    ex3(&a, &d, &mv, &c);
    c.weather_negated = true;
    ASSERT_RANGE(run(&c, &a, &d, MV_SURF, &mv), 197, 232);
}

/* Ex 3 in singles: no spread halving. 75, rain 112, +2 114, STAB 171, x2 342, x2 684; lo 581. */
TEST(surf_singles_has_no_spread_cut) {
    DamageMon a, d; DamageMove mv; DamageCtx c;
    ex3(&a, &d, &mv, &c);
    c.battle_flags = 0;
    ASSERT_RANGE(run(&c, &a, &d, MV_SURF, &mv), 581, 684);
}

/* Ex 4: enemy Blaziken SpA 180 at 60/213 HP (Blaze), sun, Flamethrower vs player Walrein
 * SpD 152 Thick Fat, Mind Badge. Fire>Water applies before Fire>Ice (table order). */
TEST(ex4_flamethrower_blaze_sun_thick_fat) {
    DamageMon a = mon(1, FIR, FIG, 70);
    a.sp_attack = 180;
    a.hp = 60;
    a.max_hp = 213;
    a.ability = AB_BLAZE;
    DamageMon d = mon(0, ICE, WAT, 70);
    d.sp_defense = 152;
    d.ability = AB_THICK_FAT;
    badges(&d, 0xFF);
    DamageMove mv = move(EFF_BURN_HIT, 95, FIR, TGT_SELECTED);
    DamageCtx c = ctx0();
    c.weather = WX_SUN;
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAMETHROWER, &mv), 86, 102);
}

/* XA: enemy Heracross Lv60 Atk 232 Megahorn vs player Gardevoir Lv58 Def 140, badges 1, 5, 7. */
static DamageResult xa(uint8_t ability, uint32_t status1) {
    DamageMon a = mon(1, BUG, FIG, 60);
    a.attack = 232;
    a.hp = a.max_hp = 185;
    a.ability = ability;
    a.status1 = status1;
    DamageMon d = mon(0, PSY, PSY, 58);
    d.defense = 140;
    badges(&d, 0x51);
    DamageMove mv = move(EFF_HIT, 120, BUG, TGT_SELECTED);
    DamageCtx c = ctx0();
    return run(&c, &a, &d, MV_MEGAHORN, &mv);
}

TEST(xa_burn_guts_and_swarm) {
    ASSERT_RANGE(xa(AB_GUTS, 0), 244, 288);
    ASSERT_RANGE(xa(AB_GUTS, ST1_BURN), 363, 428);  /* x1.5 Atk, no burn halving */
    ASSERT_RANGE(xa(AB_SWARM, 0), 244, 288);
    ASSERT_RANGE(xa(AB_SWARM, ST1_BURN), 124, 146); /* halved after /50 */
}

/* XB: enemy Metagross Lv55 Atk 225 Meteor Mash vs player Walrein Def 149 (Balance Badge), Reflect. */
static DamageResult xb(uint32_t flags, uint8_t present) {
    DamageMon a = mon(1, STE, PSY, 55);
    a.attack = 225;
    DamageMon d = mon(0, ICE, WAT, 57);
    d.defense = 149;
    d.ability = AB_THICK_FAT;
    badges(&d, 0x10);
    DamageMove mv = move(EFF_HIT, 100, STE, TGT_SELECTED);
    DamageCtx c = ctx0();
    c.battle_flags = flags;
    c.side_status[0] = SIDE_REFLECT;
    c.present_on_side[0] = present;
    c.present_on_side[1] = 2;
    return run(&c, &a, &d, MV_METEOR_MASH, &mv);
}

TEST(xb_reflect_two_thirds_in_doubles) {
    ASSERT_RANGE(xb(BT_DOUBLE, 2), 57, 68); /* both player battlers present: 2*(d/3) */
    ASSERT_RANGE(xb(BT_DOUBLE, 1), 44, 52); /* partner absent: d/2 */
    ASSERT_RANGE(xb(0, 1), 44, 52);         /* singles */
}

TEST(xc1_physical_min_one_then_quarter) {
    DamageMon a = mon(1, BUG, BUG, 3);
    a.attack = 7;
    DamageMon d = mon(0, STE, ROC, 60);
    d.defense = 239;
    badges(&d, 0x10);
    DamageMove mv = move(EFF_HIT, 35, NOR, TGT_SELECTED);
    DamageCtx c = ctx0();
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 1, 1);
}

static DamageMon torchic(void) {
    DamageMon a = mon(1, FIR, FIR, 12);
    a.sp_attack = 23;
    a.hp = a.max_hp = 34;
    a.ability = AB_BLAZE;
    return a;
}

static DamageMon kingdra(void) {
    DamageMon d = mon(0, WAT, DRA, 50);
    d.sp_defense = 115;
    badges(&d, 0x40);
    return d;
}

TEST(xc2_special_no_clamp_then_quarter) {
    DamageMon a = torchic(), d = kingdra();
    DamageMove mv = move(EFF_BURN_HIT, 40, FIR, TGT_SELECTED);
    DamageCtx c = ctx0();
    ASSERT_RANGE(run(&c, &a, &d, MV_EMBER, &mv), 1, 1);
}

TEST(xc3_quarter_with_varied_rolls) {
    DamageMon a = mon(1, FIR, FIG, 55);
    a.sp_attack = 194;
    a.hp = a.max_hp = 170;
    a.ability = AB_BLAZE;
    DamageMon d = kingdra();
    DamageMove mv = move(EFF_BURN_HIT, 95, FIR, TGT_SELECTED);
    DamageCtx c = ctx0();
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAMETHROWER, &mv), 22, 27);
}

TEST(xc4_tiny_damage_table_order) {
    DamageMon a = torchic();
    DamageMon d = mon(0, ICE, WAT, 50);
    d.sp_defense = 110;
    d.ability = AB_THICK_FAT;
    badges(&d, 0x40);
    DamageMove mv = move(EFF_BURN_HIT, 40, FIR, TGT_SELECTED);
    DamageCtx c = ctx0();
    ASSERT_RANGE(run(&c, &a, &d, MV_EMBER, &mv), 1, 2);
}

TEST(immune_row_levitate_and_absorb) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, NOR, NOR, 50);
    DamageMove tbolt = move(EFF_PARALYZE_HIT, 95, ELE, TGT_SELECTED);
    DamageMon d = mon(0, GRO, GRO, 50);
    ASSERT_KIND(run(&c, &a, &d, MV_THUNDERBOLT, &tbolt), DMG_IMMUNE); /* Electric > Ground x0 */

    DamageMove eq = move(EFF_EARTHQUAKE, 100, GRO, TGT_FOES_AND_ALLY);
    d = mon(0, NOR, NOR, 50);
    d.ability = AB_LEVITATE;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &eq), DMG_IMMUNE);

    d = mon(0, WAT, ELE, 50);
    d.ability = AB_VOLT_ABSORB;
    ASSERT_KIND(run(&c, &a, &d, MV_THUNDERBOLT, &tbolt), DMG_IMMUNE);

    DamageMove surf = move(EFF_HIT, 95, WAT, TGT_BOTH);
    d = mon(0, NOR, NOR, 50);
    d.ability = AB_WATER_ABSORB;
    ASSERT_KIND(run(&c, &a, &d, MV_SURF, &surf), DMG_IMMUNE);

    DamageMove hyper_voice = move(EFF_HIT, 90, NOR, TGT_BOTH);
    d.ability = AB_SOUNDPROOF;
    ASSERT_KIND(run(&c, &a, &d, MV_HYPER_VOICE, &hyper_voice), DMG_IMMUNE);
}

/* Flash Fire defender absorbs Fire unless frozen. Frozen: 100*95*22/100 = 2090, /50 = 41,
 * +2 = 43; 43*85/100 = 36. */
TEST(flash_fire_defender_unless_frozen) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, NOR, NOR, 50);
    DamageMon d = mon(0, NOR, NOR, 50);
    d.ability = AB_FLASH_FIRE;
    DamageMove mv = move(EFF_BURN_HIT, 95, FIR, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_FLAMETHROWER, &mv), DMG_IMMUNE);
    d.status1 = ST1_FREEZE;
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAMETHROWER, &mv), 36, 43);
}

/* Shedinja (Bug/Ghost, Wonder Guard) SpD 45. Fire is x2: 100*95*22/45 = 4644, /50 = 92, +2 = 94,
 * x2 = 188; 188*85/100 = 159. Water is neutral, so Wonder Guard blocks it. */
TEST(wonder_guard_needs_net_super_effective) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, NOR, NOR, 50);
    DamageMon d = mon(0, BUG, GHO, 50);
    d.ability = AB_WONDER_GUARD;
    d.sp_defense = 45;
    DamageMove fire = move(EFF_BURN_HIT, 95, FIR, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAMETHROWER, &fire), 159, 188);
    DamageMove water = move(EFF_HIT, 95, WAT, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_SURF, &water), DMG_IMMUNE);
    /* Fire vs Water/Bug: x0.5 then x2 cancels the flags, so it is not super effective. */
    d.type1 = WAT;
    d.type2 = BUG;
    ASSERT_KIND(run(&c, &a, &d, MV_FLAMETHROWER, &fire), DMG_IMMUNE);
}

/* Tackle vs Ghost: 100*35*22/100 = 770, /50 = 15, +2 = 17; 17*85/100 = 14. */
TEST(foresight_cuts_ghost_rows) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, WAT, WAT, 50);
    DamageMon d = mon(0, GHO, GHO, 50);
    DamageMove tackle = move(EFF_HIT, 35, NOR, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_TACKLE, &tackle), DMG_IMMUNE);
    d.status2 = ST2_FORESIGHT;
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &tackle), 14, 17);
}

/* Struggle skips typecalc: no STAB, hits Ghosts. 100*50*22/100 = 1100, /50 = 22, +2 = 24; lo 20. */
TEST(struggle_has_no_stab_or_chart) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, NOR, NOR, 50);
    DamageMon d = mon(0, GHO, GHO, 50);
    DamageMove mv = move(EFF_RECOIL, 50, NOR, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_STRUGGLE, &mv), 20, 24);
}

/* Thunderbolt 100*95*22/100 = 2090, /50 = 41, +2 = 43; Charge x2 = 86, lo 73. */
TEST(charge_doubles_electric) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, NOR, NOR, 50);
    DamageMon d = mon(0, NOR, NOR, 50);
    DamageMove mv = move(EFF_PARALYZE_HIT, 95, ELE, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 36, 43);
    a.status3 = ST3_CHARGED_UP;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 73, 86);
}

/* Solar Beam 100*120*22/100 = 2640, /50 = 52; rain, sandstorm or hail halve it to 26, +2 = 28,
 * STAB 42, lo 42*85/100 = 35. Clear or sun: 54, STAB 81, lo 68. */
TEST(solar_beam_halved_in_rain_sand_and_hail) {
    DamageCtx c = ctx0();
    DamageMon a = mon(1, GRA, GRA, 50);
    DamageMon d = mon(0, NOR, NOR, 50);
    DamageMove mv = move(EFF_SOLAR_BEAM, 120, GRA, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_SOLAR_BEAM, &mv), 68, 81);
    c.weather = WX_RAIN_DRIZZLE;
    ASSERT_RANGE(run(&c, &a, &d, MV_SOLAR_BEAM, &mv), 35, 42);
    c.weather = WX_SANDSTORM;
    ASSERT_RANGE(run(&c, &a, &d, MV_SOLAR_BEAM, &mv), 35, 42);
    c.weather = WX_HAIL;
    ASSERT_RANGE(run(&c, &a, &d, MV_SOLAR_BEAM, &mv), 35, 42);
    c.weather = WX_SUN;
    ASSERT_RANGE(run(&c, &a, &d, MV_SOLAR_BEAM, &mv), 68, 81);
}

/* Fighting 75 into Reflect: 100*75*22/100 = 1650, /50 = 33. Brick Break ignores Reflect: +2 = 35,
 * lo 29. A plain move halves: 16, +2 = 18, lo 15. */
TEST(brick_break_ignores_reflect) {
    DamageCtx c = ctx0();
    c.side_status[0] = SIDE_REFLECT;
    DamageMon a = mon(1, NOR, NOR, 50);
    DamageMon d = mon(0, WAT, WAT, 50);
    DamageMove bb = move(EFF_BRICK_BREAK, 75, FIG, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_BRICK_BREAK, &bb), 29, 35);
    DamageMove plain = move(EFF_HIT, 75, FIG, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_FIRE_PUNCH, &plain), 15, 18);
}

TEST(fainted_absent_and_invalid_fall_back) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    d.hp = 0;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &mv), DMG_NONE);
    ex1(&a, &d, &mv);
    a.absent = true;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &mv), DMG_NONE);
    ex1(&a, &d, &mv);
    DamageTables t = tables();
    t.valid = false;
    DamageResult r;
    damage_predict(&t, &c, &a, &d, MV_EARTHQUAKE, &mv, &r);
    ASSERT_EQ(r.kind, DMG_NONE);
}

TEST(enigma_berry_falls_back) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    d.item = ITEM_ENIGMA;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &mv), DMG_NONE);
    ex1(&a, &d, &mv);
    a.item = ITEM_ENIGMA;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &mv), DMG_NONE);
}

TEST(substitute_target_never_ko) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    d.status2 = ST2_SUBSTITUTE;
    DamageResult r = run(&c, &a, &d, MV_EARTHQUAKE, &mv);
    ASSERT_RANGE(r, 149, 176);
    ASSERT_TRUE(r.no_ko);
}

/* Defense 1 at stage -6: 1*10/40 = 0, a zero divisor. */
TEST(zero_divisor_falls_back) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    a.badges_apply = false;
    d.defense = 1;
    d.stages[2] = 0;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &mv), DMG_NONE);
}

TEST(false_swipe_leaves_one) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    DamageMove fs = move(EFF_FALSE_SWIPE, 40, NOR, TGT_SELECTED);
    /* 198*40*30/208 = 1142, /50 = 22, +2 = 24, no STAB, Normal>Steel x0.5 = 12; lo 10. */
    DamageResult r = run(&c, &a, &d, MV_FALSE_SWIPE, &fs);
    ASSERT_RANGE(r, 10, 12);
    ASSERT_TRUE(r.leaves_one);
}

/* Effects on the spec's fallback list, and a Safari battle, give no number. */
TEST(fallback_effects_and_safari) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    DamageMove multi = move(EFF_MULTI_HIT, 15, NOR, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_FURY_ATTACK, &multi), DMG_NONE);
    DamageMove magnitude = move(EFF_MAGNITUDE, 1, GRO, TGT_FOES_AND_ALLY);
    ASSERT_KIND(run(&c, &a, &d, MV_MAGNITUDE, &magnitude), DMG_NONE);
    DamageMove psywave = move(EFF_PSYWAVE, 1, PSY, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_PSYWAVE, &psywave), DMG_NONE);
    DamageMove triple = move(EFF_TRIPLE_KICK, 10, FIG, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_TRIPLE_KICK, &triple), DMG_NONE);
    c.battle_flags = BT_SAFARI;
    ASSERT_KIND(run(&c, &a, &d, MV_EARTHQUAKE, &mv), DMG_NONE);
}

/* ---- Task 1 paths without exact tests yet. Base fixture: Lv50 (level factor 22), all stats 100,
 * attacker Water type (no STAB on Normal/Fire/Electric), defender Normal (neutral to all used). */

static void plain(DamageMon* a, DamageMon* d, DamageCtx* c) {
    *a = mon(1, WAT, WAT, 50);
    *d = mon(0, NOR, NOR, 50);
    *c = ctx0();
}

/* Physical Normal 100: 100*100*22/100 = 2200, /50 = 44, +2 = 46, lo 46*85/100 = 39.
 * Atk 150: 150*100*22/100 = 3300, /50 = 66, +2 = 68, lo 57. Atk 200: 4400/50 = 88, +2 = 90, lo 76. */
TEST(choice_band_hustle_huge_power) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_HIT, 100, NOR, TGT_SELECTED);
    plain(&a, &d, &c);
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 39, 46);
    a.hold_effect = HE_CHOICE_BAND; /* 150*100/100 = 150 */
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 57, 68);
    plain(&a, &d, &c);
    a.ability = AB_HUSTLE;          /* 150*100/100 = 150 */
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 57, 68);
    a.ability = AB_HUGE_POWER;      /* 100*2 = 200 */
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 76, 90);
    a.ability = AB_PURE_POWER;
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 76, 90);
}

/* Special Electric 100: SpA 100 -> 39..46; SpA 150 (Plus/Minus x1.5) -> 57..68 as above. */
TEST(plus_minus_need_the_other_on_field) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_PARALYZE_HIT, 100, ELE, TGT_SELECTED);
    plain(&a, &d, &c);
    a.ability = AB_PLUS;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 39, 46);
    c.plus_on_field = true;     /* only Plus around: no boost */
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 39, 46);
    c.minus_on_field = true;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 57, 68);
    a.ability = AB_MINUS;
    c.minus_on_field = false;   /* Minus boosted by a Plus on the field */
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 57, 68);
}

/* Power 100 -> 50: 100*50*22/100 = 1100, /50 = 22, +2 = 24, lo 24*85/100 = 20. */
TEST(mud_sport_and_water_sport_halve_power) {
    DamageMon a, d; DamageCtx c;
    DamageMove elec = move(EFF_PARALYZE_HIT, 100, ELE, TGT_SELECTED);
    DamageMove fire = move(EFF_BURN_HIT, 100, FIR, TGT_SELECTED);
    plain(&a, &d, &c);
    c.mud_sport = true;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &elec), 20, 24);
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAMETHROWER, &fire), 39, 46); /* Mud Sport leaves Fire */
    c.mud_sport = false;
    c.water_sport = true;
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAMETHROWER, &fire), 20, 24);
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &elec), 39, 46);
}

/* Marvel Scale with any status: Def 150. 100*100*22 = 220000, /150 = 1466, /50 = 29, +2 = 31,
 * lo 31*85/100 = 26. Without status 39..46. */
TEST(marvel_scale_needs_status) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_HIT, 100, NOR, TGT_SELECTED);
    plain(&a, &d, &c);
    d.ability = AB_MARVEL_SCALE;
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 39, 46);
    d.status1 = ST1_POISON;
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 26, 31);
}

/* Soul Dew: Latias/Latios SpA or SpD x1.5, never in a Frontier battle.
 * SpA 150: 57..68. SpD 150: 220000/150 = 1466, /50 = 29, +2 = 31, lo 26. */
TEST(soul_dew_latias_latios_not_frontier) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_HIT, 100, ELE, TGT_SELECTED);
    plain(&a, &d, &c);
    a.hold_effect = HE_SOUL_DEW;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 39, 46); /* not Latias/Latios */
    a.species = SP_LATIAS;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 57, 68);
    c.battle_flags = BT_BATTLE_TOWER;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 39, 46);
    plain(&a, &d, &c);
    d.species = SP_LATIOS;
    d.hold_effect = HE_SOUL_DEW;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 26, 31);
    c.battle_flags = BT_BATTLE_TOWER;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 39, 46);
}

/* Light Screen, special 100: 44 before screens. Doubles with both defenders present:
 * 2*(44/3) = 28, +2 = 30, lo 30*85/100 = 25. Partner absent: 44/2 = 22, +2 = 24, lo 20. */
TEST(light_screen_two_thirds_in_doubles) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_HIT, 100, ELE, TGT_SELECTED);
    plain(&a, &d, &c);
    c.side_status[0] = SIDE_LIGHTSCREEN;
    c.battle_flags = BT_DOUBLE;
    c.present_on_side[0] = 2;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 25, 30);
    c.present_on_side[0] = 1;
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDERBOLT, &mv), 20, 24);
}

/* Water 100 special, attacker Normal (no STAB): 44. Sun halves: 22, +2 = 24, lo 20.
 * Rain: 15*44/10 = 66, +2 = 68, lo 57. Burn never halves a special move: 46, lo 39. */
TEST(sun_halves_water_and_burn_spares_special) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_HIT, 100, WAT, TGT_SELECTED);
    plain(&a, &d, &c);
    a.type1 = a.type2 = NOR;
    c.weather = WX_SUN;
    ASSERT_RANGE(run(&c, &a, &d, MV_SURF, &mv), 20, 24);
    c.weather = WX_RAIN;
    ASSERT_RANGE(run(&c, &a, &d, MV_SURF, &mv), 57, 68);
    c.weather = 0;
    a.status1 = ST1_BURN;
    ASSERT_RANGE(run(&c, &a, &d, MV_SURF, &mv), 39, 46);
}

/* Metal Powder checks the defender's battle species (a Transformed Ditto has the target's species).
 * Def 200: 100*100*22/200 = 1100, /50 = 22, +2 = 24, lo 20. Def 100: 39..46. */
TEST(metal_powder_uses_battle_species) {
    DamageMon a, d; DamageCtx c;
    DamageMove mv = move(EFF_HIT, 100, NOR, TGT_SELECTED);
    plain(&a, &d, &c);
    d.hold_effect = HE_METAL_POWDER;
    d.species = SP_DITTO;
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 20, 24);
    d.species = 1;
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &mv), 39, 46);
}

/* ---- Task 2: variable power ---- */

/* Return friendship 255: 10*255/25 = 102. 100*102*22/100 = 2244, /50 = 44, +2 = 46, lo 39.
 * Frustration friendship 255: 10*0/25 = 0, so the table power 1 is used:
 * 100*1*22/100 = 22, /50 = 0 -> physical min 1, +2 = 3, lo 3*85/100 = 2.
 * Frustration friendship 0 = power 102 again. */
TEST(return_and_frustration_power) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove ret = move(EFF_RETURN, 1, NOR, TGT_SELECTED);
    DamageMove fru = move(EFF_FRUSTRATION, 1, NOR, TGT_SELECTED);
    a.friendship = 255;
    ASSERT_RANGE(run(&c, &a, &d, MV_RETURN, &ret), 39, 46);
    ASSERT_RANGE(run(&c, &a, &d, MV_FRUSTRATION, &fru), 2, 3);
    a.friendship = 0;
    ASSERT_RANGE(run(&c, &a, &d, MV_FRUSTRATION, &fru), 39, 46);
}

/* Flail: f = hp*48/maxHP as u8, 1 when 0 and hp > 0; first row with f <= key.
 * hp 1/100: f 0 -> 1 -> power 200: 100*200*22/100 = 4400, /50 = 88, +2 = 90, lo 76.
 * hp 33/100: f 1584/100 = 15 -> 80: 1760/50 = 35, +2 = 37, lo 31.
 * hp 100/100: f 48 -> 20: 440/50 = 8, +2 = 10, lo 8. */
TEST(flail_power_by_hp) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_FLAIL, 1, NOR, TGT_SELECTED);
    a.hp = 1;
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAIL, &mv), 76, 90);
    a.hp = 33;
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAIL, &mv), 31, 37);
    a.hp = 100;
    ASSERT_RANGE(run(&c, &a, &d, MV_FLAIL, &mv), 8, 10);
}

/* Eruption (Fire 150, special, target BOTH): power hp*150/maxHP, min 1.
 * hp 1/100: 150/100 = 1; 100*1*22/100 = 22, /50 = 0 (no special clamp), +2 = 2, lo 1.
 * Full HP: 3300/50 = 66, +2 = 68, lo 57. Doubles, both present: spread 33, +2 = 35, lo 29. */
TEST(eruption_power_by_hp_and_spread) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_ERUPTION, 150, FIR, TGT_BOTH);
    a.hp = 1;
    ASSERT_RANGE(run(&c, &a, &d, MV_ERUPTION, &mv), 1, 2);
    a.hp = 100;
    ASSERT_RANGE(run(&c, &a, &d, MV_ERUPTION, &mv), 57, 68);
    c.battle_flags = BT_DOUBLE;
    c.present_on_side[0] = 2;
    ASSERT_RANGE(run(&c, &a, &d, MV_ERUPTION, &mv), 29, 35);
}

/* IVs packed like BattlePokemon +0x14: HP, Atk, Def, Spe, SpA, SpD, 5 bits each. */
static uint32_t ivs(uint32_t hp, uint32_t atk, uint32_t def, uint32_t spe, uint32_t spa, uint32_t spd) {
    return hp | atk << 5 | def << 10 | spe << 15 | spa << 20 | spd << 25;
}

/* IVs 31/30/31/30/31/30: power bits (bit 1) all set -> 40*63/63+30 = 70; type bits (bit 0)
 * 1,0,1,0,1,0 = 21 -> 15*21/63+1 = 6 = Bug, physical. Atk 200: 200*70*22/100 = 3080, /50 = 61,
 * +2 = 63, no STAB, Bug>Psychic x2 = 126, lo 126*85/100 = 107. */
TEST(hidden_power_bug_uses_attack_and_chart) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.iv_word = ivs(31, 30, 31, 30, 31, 30);
    a.attack = 200;
    a.sp_attack = 50;
    d.type1 = d.type2 = PSY;
    DamageMove mv = move(EFF_HIDDEN_POWER, 1, NOR, TGT_SELECTED);
    DamageResult r = run(&c, &a, &d, MV_HIDDEN_POWER, &mv);
    ASSERT_EQ(r.type, BUG);
    ASSERT_RANGE(r, 107, 126);
}

/* IVs 31/31/31/31/30/31: type bits 101111b = 47 -> 15*47/63 = 11, +1 = 12, >= 9 so 13 Electric,
 * power 70, special. 100*70*22/100 = 1540, /50 = 30, +2 = 32, lo 27. Charge keys off the table
 * type (Normal), so it does not double. */
TEST(hidden_power_electric_ignores_charge) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.iv_word = ivs(31, 31, 31, 31, 30, 31);
    DamageMove mv = move(EFF_HIDDEN_POWER, 1, NOR, TGT_SELECTED);
    DamageResult r = run(&c, &a, &d, MV_HIDDEN_POWER, &mv);
    ASSERT_EQ(r.type, ELE);
    ASSERT_RANGE(r, 27, 32);
    a.status3 = ST3_CHARGED_UP;
    ASSERT_RANGE(run(&c, &a, &d, MV_HIDDEN_POWER, &mv), 27, 32);
}

/* Low Kick vs a Water target (no Fighting row): first row with key > weight.
 * 1200 hg -> 100: 100*100*22/100 = 2200, /50 = 44, +2 = 46, lo 39.
 * 99 hg -> 20: 440/50 = 8, +2 = 10, lo 8. 2000 hg -> end of table -> 120: 2640/50 = 52, +2 = 54,
 * lo 45. Unknown weight (0) -> no number. */
TEST(low_kick_by_target_weight) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    d.type1 = d.type2 = WAT;
    DamageMove mv = move(EFF_LOW_KICK, 1, FIG, TGT_SELECTED);
    d.weight_hg = 1200;
    ASSERT_RANGE(run(&c, &a, &d, MV_LOW_KICK, &mv), 39, 46);
    d.weight_hg = 99;
    ASSERT_RANGE(run(&c, &a, &d, MV_LOW_KICK, &mv), 8, 10);
    d.weight_hg = 2000;
    ASSERT_RANGE(run(&c, &a, &d, MV_LOW_KICK, &mv), 45, 54);
    d.weight_hg = 0;
    ASSERT_KIND(run(&c, &a, &d, MV_LOW_KICK, &mv), DMG_NONE);
}

/* Facade (Normal 70): 100*70*22/100 = 1540, /50 = 30. Healthy: +2 = 32, lo 27.
 * Burned: burn halves to 15, +2 = 17, x2 = 34, lo 28. Paralysed: 32 x2 = 64, lo 54. */
TEST(facade_doubles_including_the_two) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_FACADE, 70, NOR, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_FACADE, &mv), 27, 32);
    a.status1 = ST1_BURN;
    ASSERT_RANGE(run(&c, &a, &d, MV_FACADE, &mv), 28, 34);
    a.status1 = ST1_PARALYSIS;
    ASSERT_RANGE(run(&c, &a, &d, MV_FACADE, &mv), 54, 64);
    a.status1 = ST1_SLEEP_2; /* sleep is not one of Facade's statuses */
    ASSERT_RANGE(run(&c, &a, &d, MV_FACADE, &mv), 27, 32);
}

/* SmellingSalt (Normal 60): 1320/50 = 26, +2 = 28, lo 23. Paralysed target: 56, lo 47.
 * Behind a Substitute: no doubling. */
TEST(smellingsalt_vs_paralysis) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_SMELLINGSALT, 60, NOR, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_SMELLING_SALT, &mv), 23, 28);
    d.status1 = ST1_PARALYSIS;
    ASSERT_RANGE(run(&c, &a, &d, MV_SMELLING_SALT, &mv), 47, 56);
    d.status2 = ST2_SUBSTITUTE;
    ASSERT_RANGE(run(&c, &a, &d, MV_SMELLING_SALT, &mv), 23, 28);
}

/* Weather Ball (50) in rain: Water, special, x2. Attacker Normal. 100*50*22/100 = 1100, /50 = 22,
 * rain 15*22/10 = 33, +2 = 35, x2 = 70, no STAB, Water vs Ghost neutral; lo 70*85/100 = 59.
 * Sandstorm: Rock, physical: 22, +2 = 24, x2 = 48, lo 40.
 * No weather effect (Cloud Nine): Normal, so a Ghost is immune; vs Normal STAB 24*15/10 = 36, lo 30. */
TEST(weather_ball_type_and_doubling) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.type1 = a.type2 = NOR;
    d.type1 = d.type2 = GHO;
    DamageMove mv = move(EFF_WEATHER_BALL, 50, NOR, TGT_SELECTED);
    c.weather = WX_RAIN;
    c.weather_has_effect = true;
    DamageResult r = run(&c, &a, &d, MV_WEATHER_BALL, &mv);
    ASSERT_EQ(r.type, WAT);
    ASSERT_RANGE(r, 59, 70);
    d.ability = AB_WATER_ABSORB;
    ASSERT_KIND(run(&c, &a, &d, MV_WEATHER_BALL, &mv), DMG_IMMUNE);
    d = mon(0, NOR, NOR, 50);
    c.weather = WX_SANDSTORM;
    r = run(&c, &a, &d, MV_WEATHER_BALL, &mv);
    ASSERT_EQ(r.type, ROC);
    ASSERT_RANGE(r, 40, 48);
    c.weather = WX_RAIN;
    c.weather_has_effect = false;
    c.weather_negated = true;
    ASSERT_RANGE(run(&c, &a, &d, MV_WEATHER_BALL, &mv), 30, 36);
    d.type1 = d.type2 = GHO;
    ASSERT_KIND(run(&c, &a, &d, MV_WEATHER_BALL, &mv), DMG_IMMUNE);
}

/* Stomp (Normal 65) vs a Minimized target: 1430/50 = 28, +2 = 30, x2 = 60, lo 51. Plain 25..30. */
TEST(stomp_doubles_vs_minimize) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_FLINCH_MINIMIZE_HIT, 65, NOR, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_STOMP, &mv), 25, 30);
    d.status3 = ST3_MINIMIZED;
    DamageResult r = run(&c, &a, &d, MV_STOMP, &mv);
    ASSERT_RANGE(r, 51, 60);
    ASSERT_TRUE(!r.no_ko);
}

/* ---- R10 semi-invulnerable targets ---- */

/* Ex 1 vs Dig: x1 149..176; x2: 59*2 = 118, STAB 177, x2 354. Union 149..354, never a KO colour. */
TEST(earthquake_vs_underground_shows_union) {
    DamageMon a, d; DamageMove mv; DamageCtx c = ctx0();
    ex1(&a, &d, &mv);
    d.status3 = ST3_UNDERGROUND;
    DamageResult r = run(&c, &a, &d, MV_EARTHQUAKE, &mv);
    ASSERT_RANGE(r, 149, 354);
    ASSERT_TRUE(r.no_ko);
    /* Earthquake vs a Flying target in the air: no doubling, still no KO colour. */
    d.status3 = ST3_ON_AIR;
    r = run(&c, &a, &d, MV_EARTHQUAKE, &mv);
    ASSERT_RANGE(r, 149, 176);
    ASSERT_TRUE(r.no_ko);
}

/* Tackle (35) vs a target using Fly: 770/50 = 15, +2 = 17, lo 14, no_ko.
 * Gust (Flying 40): 880/50 = 17, +2 = 19, lo 16; x2 = 38. Union 16..38.
 * Thunder and Sky Uppercut only skip the miss; no doubling (battle_scripts_1.s:1920, 2712). */
TEST(on_air_target_tackle_gust_thunder) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    d.type1 = d.type2 = WAT;
    DamageMove tackle = move(EFF_HIT, 35, NOR, TGT_SELECTED);
    DamageMove gust = move(EFF_GUST, 40, FLY, TGT_SELECTED);
    DamageMove twister = move(EFF_TWISTER, 40, FLY, TGT_BOTH); /* Flying here to reuse the math */
    DamageMove thunder = move(EFF_THUNDER, 40, FLY, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_GUST, &gust), 16, 19);
    d.status3 = ST3_ON_AIR;
    DamageResult r = run(&c, &a, &d, MV_TACKLE, &tackle);
    ASSERT_RANGE(r, 14, 17);
    ASSERT_TRUE(r.no_ko);
    r = run(&c, &a, &d, MV_GUST, &gust);
    ASSERT_RANGE(r, 16, 38);
    ASSERT_TRUE(r.no_ko);
    ASSERT_RANGE(run(&c, &a, &d, MV_TWISTER, &twister), 16, 38);
    ASSERT_RANGE(run(&c, &a, &d, MV_THUNDER, &thunder), 16, 19);
}

/* Surf (95) vs Dive: attacker Normal, 2090/50 = 41, +2 = 43, lo 36; x2 86. Union 36..86.
 * Whirlpool (Water 15, EFFECT_TRAP): 330/50 = 6, +2 = 8, lo 6; x2 16. Union 6..16.
 * Hydro Pump-like plain Water move: 36..43 with no_ko. */
TEST(underwater_target_surf_whirlpool) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.type1 = a.type2 = NOR;
    d.status3 = ST3_UNDERWATER;
    DamageMove surf = move(EFF_HIT, 95, WAT, TGT_BOTH);
    DamageMove whirlpool = move(EFF_TRAP, 15, WAT, TGT_SELECTED);
    DamageResult r = run(&c, &a, &d, MV_SURF, &surf);
    ASSERT_RANGE(r, 36, 86);
    ASSERT_TRUE(r.no_ko);
    ASSERT_RANGE(run(&c, &a, &d, MV_WHIRLPOOL, &whirlpool), 6, 16);
    r = run(&c, &a, &d, MV_HYDRO_PUMP, &surf); /* same row data, another move id: no doubling */
    ASSERT_RANGE(r, 36, 43);
    ASSERT_TRUE(r.no_ko);
}

/* R11: attacker locked into a multi-turn move -> variable-power rows fall back, plain rows stay. */
TEST(locked_attacker_drops_variable_power) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.friendship = 255;
    a.status2 = ST2_MULTIPLETURNS;
    DamageMove ret = move(EFF_RETURN, 1, NOR, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_RETURN, &ret), DMG_NONE);
    DamageMove gust = move(EFF_GUST, 40, FLY, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_GUST, &gust), DMG_NONE);
    DamageMove tackle = move(EFF_HIT, 35, NOR, TGT_SELECTED);
    ASSERT_RANGE(run(&c, &a, &d, MV_TACKLE, &tackle), 14, 17);
}

/* ---- Explosion, Dream Eater, Snore, Fake Out ---- */

/* Explosion 250 (FOES_AND_ALLY, no spread cut), defense halved to 50:
 * 100*250*22/50 = 11000, /50 = 220, +2 = 222, lo 222*85/100 = 188. Damp anywhere: fails. */
TEST(explosion_halves_defense_and_damp_fails) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_EXPLOSION, 250, NOR, TGT_FOES_AND_ALLY);
    ASSERT_RANGE(run(&c, &a, &d, MV_EXPLOSION, &mv), 188, 222);
    c.damp_on_field = true;
    ASSERT_KIND(run(&c, &a, &d, MV_EXPLOSION, &mv), DMG_FAILS);
}

/* Dream Eater (Psychic 100): 2200/50 = 44, +2 = 46, lo 39 vs a sleeping target. */
TEST(dream_eater_needs_sleeping_target) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_DREAM_EATER, 100, PSY, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_DREAM_EATER, &mv), DMG_FAILS);
    d.status1 = ST1_SLEEP_2;
    ASSERT_RANGE(run(&c, &a, &d, MV_DREAM_EATER, &mv), 39, 46);
    d.status2 = ST2_SUBSTITUTE;
    ASSERT_KIND(run(&c, &a, &d, MV_DREAM_EATER, &mv), DMG_FAILS);
}

/* Snore and Fake Out (Normal 40): 880/50 = 17, +2 = 19, lo 16. */
TEST(snore_and_fake_out_preconditions) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove snore = move(EFF_SNORE, 40, NOR, TGT_SELECTED);
    DamageMove fake_out = move(EFF_FAKE_OUT, 40, NOR, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_SNORE, &snore), DMG_FAILS);
    ASSERT_RANGE(run(&c, &a, &d, MV_FAKE_OUT, &fake_out), 16, 19);
    a.status1 = ST1_SLEEP_2;
    ASSERT_RANGE(run(&c, &a, &d, MV_SNORE, &snore), 16, 19);
    a.first_turn = false;
    ASSERT_KIND(run(&c, &a, &d, MV_FAKE_OUT, &fake_out), DMG_FAILS);
    d.ability = AB_SOUNDPROOF;
    ASSERT_KIND(run(&c, &a, &d, MV_SNORE, &snore), DMG_IMMUNE);
}

/* ---- Fixed damage ---- */

TEST(fixed_damage_moves) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.level = 43;
    DamageMove toss = move(EFF_LEVEL_DAMAGE, 1, FIG, TGT_SELECTED);
    DamageMove shade = move(EFF_LEVEL_DAMAGE, 1, GHO, TGT_SELECTED);
    DamageMove rage = move(EFF_DRAGON_RAGE, 1, DRA, TGT_SELECTED);
    DamageMove boom = move(EFF_SONICBOOM, 1, NOR, TGT_SELECTED);
    DamageMove fang = move(EFF_SUPER_FANG, 1, NOR, TGT_SELECTED);
    ASSERT_FIXED(run(&c, &a, &d, MV_SEISMIC_TOSS, &toss), 43);
    ASSERT_KIND(run(&c, &a, &d, MV_NIGHT_SHADE, &shade), DMG_IMMUNE); /* Ghost > Normal x0 */
    ASSERT_FIXED(run(&c, &a, &d, MV_DRAGON_RAGE, &rage), 40);
    ASSERT_FIXED(run(&c, &a, &d, MV_SONIC_BOOM, &boom), 20);
    d.hp = 77;
    ASSERT_FIXED(run(&c, &a, &d, MV_SUPER_FANG, &fang), 38);
    d.hp = 1;
    ASSERT_FIXED(run(&c, &a, &d, MV_SUPER_FANG, &fang), 1);
    d.hp = 100;
    d.type1 = d.type2 = GHO;
    ASSERT_KIND(run(&c, &a, &d, MV_SEISMIC_TOSS, &toss), DMG_IMMUNE); /* Fighting > Ghost x0 */
    ASSERT_FIXED(run(&c, &a, &d, MV_NIGHT_SHADE, &shade), 43);
    d.status2 = ST2_FORESIGHT;
    ASSERT_FIXED(run(&c, &a, &d, MV_SEISMIC_TOSS, &toss), 43);
}

/* Shedinja: Wonder Guard stops Dragon Rage (neutral), Night Shade is super effective and hits. */
TEST(fixed_damage_vs_wonder_guard) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    d = mon(0, BUG, GHO, 50);
    d.ability = AB_WONDER_GUARD;
    d.hp = d.max_hp = 1;
    DamageMove rage = move(EFF_DRAGON_RAGE, 1, DRA, TGT_SELECTED);
    DamageMove shade = move(EFF_LEVEL_DAMAGE, 1, GHO, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_DRAGON_RAGE, &rage), DMG_IMMUNE);
    ASSERT_FIXED(run(&c, &a, &d, MV_NIGHT_SHADE, &shade), 50);
}

TEST(endeavor_hp_difference) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove mv = move(EFF_ENDEAVOR, 1, NOR, TGT_SELECTED);
    a.hp = 30;
    ASSERT_FIXED(run(&c, &a, &d, MV_ENDEAVOR, &mv), 70);
    a.hp = 100;
    ASSERT_KIND(run(&c, &a, &d, MV_ENDEAVOR, &mv), DMG_FAILS); /* equal hp */
    d.hp = 40;
    ASSERT_KIND(run(&c, &a, &d, MV_ENDEAVOR, &mv), DMG_FAILS); /* no unsigned underflow */
    a.hp = 30;
    d.hp = 100;
    d.type1 = d.type2 = GHO;
    ASSERT_KIND(run(&c, &a, &d, MV_ENDEAVOR, &mv), DMG_IMMUNE);
}

/* Spit Up (Normal 100, table type), attacker Normal: 2200/50 = 44, +2 = 46, x stockpile 2 = 92,
 * STAB 92*15/10 = 138, neutral, no random roll: 138. Stockpile 0 fails before the type check. */
TEST(spit_up_stockpile) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    a.type1 = a.type2 = NOR;
    DamageMove mv = move(EFF_SPIT_UP, 100, NOR, TGT_SELECTED);
    ASSERT_KIND(run(&c, &a, &d, MV_SPIT_UP, &mv), DMG_FAILS);
    a.stockpile = 2;
    ASSERT_FIXED(run(&c, &a, &d, MV_SPIT_UP, &mv), 138);
    /* Normal > Steel x0.5: 138*5/10 = 69. */
    d.type1 = d.type2 = STE;
    ASSERT_FIXED(run(&c, &a, &d, MV_SPIT_UP, &mv), 69);
    d.type1 = d.type2 = GHO;
    ASSERT_KIND(run(&c, &a, &d, MV_SPIT_UP, &mv), DMG_IMMUNE);
    a.stockpile = 0;
    ASSERT_KIND(run(&c, &a, &d, MV_SPIT_UP, &mv), DMG_FAILS);
}

/* ---- OHKO ---- */

TEST(ohko_rules) {
    DamageMon a, d; DamageCtx c;
    plain(&a, &d, &c);
    DamageMove fissure = move(EFF_OHKO, 1, GRO, TGT_SELECTED);
    DamageMove sheer_cold = move(EFF_OHKO, 1, ICE, TGT_SELECTED);
    DamageResult r = run(&c, &a, &d, MV_FISSURE, &fissure);
    ASSERT_EQ(r.kind, DMG_OHKO);
    ASSERT_EQ(r.lo, 100);
    a.level = 49;
    ASSERT_KIND(run(&c, &a, &d, MV_FISSURE, &fissure), DMG_FAILS);
    a.level = 50;
    d.ability = AB_STURDY;
    ASSERT_KIND(run(&c, &a, &d, MV_FISSURE, &fissure), DMG_FAILS);
    d.ability = AB_LEVITATE;
    ASSERT_KIND(run(&c, &a, &d, MV_FISSURE, &fissure), DMG_IMMUNE);
    d.ability = 0;
    d.type1 = d.type2 = FLY;
    a.level = 10; /* immunity is checked before the level rule */
    ASSERT_KIND(run(&c, &a, &d, MV_FISSURE, &fissure), DMG_IMMUNE);
    d = mon(0, BUG, GHO, 50);
    d.ability = AB_WONDER_GUARD;
    a.level = 50;
    ASSERT_KIND(run(&c, &a, &d, MV_SHEER_COLD, &sheer_cold), DMG_IMMUNE);
}

/* damage_tables_load copies the ROM tables at the profile's addresses. */
TEST(tables_load_from_rom) {
    static uint8_t rom[0x400];
    memset(rom, 0, sizeof(rom));
    GameProfile p;
    memset(&p, 0, sizeof(p));
    p.type_effectiveness = 0x08000000; p.type_effectiveness_len = 6;
    p.stat_stage_ratios = 0x08000100;
    p.hold_effect_to_type = 0x08000200; p.hold_effect_to_type_count = 2;
    p.flail_table = 0x08000300;
    p.weight_table = 0x08000340;
    const uint8_t chart[6] = {WAT, FIR, 20, 0xFF, 0xFF, 0};
    memcpy(rom, chart, sizeof(chart));
    for (int i = 0; i < 26; i++) rom[0x100 + i] = (uint8_t)(i + 1);
    const uint8_t hold[4] = {52, WAT, 58, FIR};
    memcpy(rom + 0x200, hold, sizeof(hold));
    for (int i = 0; i < 12; i++) rom[0x300 + i] = (uint8_t)(100 + i);
    for (int i = 0; i < 12; i++) { rom[0x340 + 2 * i] = (uint8_t)i; rom[0x341 + 2 * i] = 0x10; }
    GameMem m = { NULL, NULL, rom, sizeof(rom) };
    DamageTables t;
    ASSERT_TRUE(damage_tables_load(&m, &p, &t));
    ASSERT_TRUE(t.valid);
    ASSERT_EQ(t.type_len, 6);
    ASSERT_EQ(t.type_chart[0], WAT);
    ASSERT_EQ(t.type_chart[2], 20);
    ASSERT_EQ(t.stat_ratio[0][0], 1);
    ASSERT_EQ(t.stat_ratio[0][1], 2);
    ASSERT_EQ(t.stat_ratio[12][1], 26);
    ASSERT_EQ(t.hold_type_count, 2);
    ASSERT_EQ(t.hold_type[1][0], 58);
    ASSERT_EQ(t.hold_type[1][1], FIR);
    ASSERT_EQ(t.flail[0][0], 100);
    ASSERT_EQ(t.flail[5][1], 111);
    ASSERT_EQ(t.weight[0][0], 0x1000);
    ASSERT_EQ(t.weight[5][1], 0x100B);

    p.hold_effect_to_type_count = DMG_HOLD_TYPE_MAX + 1;   /* would not fit */
    ASSERT_TRUE(!damage_tables_load(&m, &p, &t));
    ASSERT_TRUE(!t.valid);
    p.hold_effect_to_type_count = 2;
    p.type_effectiveness_len = DMG_TYPE_TABLE_MAX + 1;
    ASSERT_TRUE(!damage_tables_load(&m, &p, &t));
    p.type_effectiveness_len = 6;
    p.weight_table = 0x080003F0;                           /* runs off the ROM */
    ASSERT_TRUE(!damage_tables_load(&m, &p, &t));
    ASSERT_TRUE(!t.valid);
    ASSERT_TRUE(!damage_tables_load(&m, NULL, &t));
}

void run_game_damage_tests(void) {
    TEST_SUITE("game_damage");
    RUN_TEST(tables_load_from_rom);
    RUN_TEST(ex1_earthquake_vs_metagross);
    RUN_TEST(ex1b_defense_plus_two);
    RUN_TEST(badges_gate_off_lowers_ex1);
    RUN_TEST(ex2_psychic_light_screen_rain);
    RUN_TEST(ex3_surf_doubles_rain_mystic_water);
    RUN_TEST(cloud_nine_negates_rain_on_surf);
    RUN_TEST(surf_singles_has_no_spread_cut);
    RUN_TEST(ex4_flamethrower_blaze_sun_thick_fat);
    RUN_TEST(xa_burn_guts_and_swarm);
    RUN_TEST(xb_reflect_two_thirds_in_doubles);
    RUN_TEST(xc1_physical_min_one_then_quarter);
    RUN_TEST(xc2_special_no_clamp_then_quarter);
    RUN_TEST(xc3_quarter_with_varied_rolls);
    RUN_TEST(xc4_tiny_damage_table_order);
    RUN_TEST(immune_row_levitate_and_absorb);
    RUN_TEST(flash_fire_defender_unless_frozen);
    RUN_TEST(wonder_guard_needs_net_super_effective);
    RUN_TEST(foresight_cuts_ghost_rows);
    RUN_TEST(struggle_has_no_stab_or_chart);
    RUN_TEST(charge_doubles_electric);
    RUN_TEST(solar_beam_halved_in_rain_sand_and_hail);
    RUN_TEST(brick_break_ignores_reflect);
    RUN_TEST(fainted_absent_and_invalid_fall_back);
    RUN_TEST(enigma_berry_falls_back);
    RUN_TEST(substitute_target_never_ko);
    RUN_TEST(zero_divisor_falls_back);
    RUN_TEST(false_swipe_leaves_one);
    RUN_TEST(fallback_effects_and_safari);
    RUN_TEST(choice_band_hustle_huge_power);
    RUN_TEST(plus_minus_need_the_other_on_field);
    RUN_TEST(mud_sport_and_water_sport_halve_power);
    RUN_TEST(marvel_scale_needs_status);
    RUN_TEST(soul_dew_latias_latios_not_frontier);
    RUN_TEST(light_screen_two_thirds_in_doubles);
    RUN_TEST(sun_halves_water_and_burn_spares_special);
    RUN_TEST(metal_powder_uses_battle_species);
    RUN_TEST(return_and_frustration_power);
    RUN_TEST(flail_power_by_hp);
    RUN_TEST(eruption_power_by_hp_and_spread);
    RUN_TEST(hidden_power_bug_uses_attack_and_chart);
    RUN_TEST(hidden_power_electric_ignores_charge);
    RUN_TEST(low_kick_by_target_weight);
    RUN_TEST(facade_doubles_including_the_two);
    RUN_TEST(smellingsalt_vs_paralysis);
    RUN_TEST(weather_ball_type_and_doubling);
    RUN_TEST(stomp_doubles_vs_minimize);
    RUN_TEST(earthquake_vs_underground_shows_union);
    RUN_TEST(on_air_target_tackle_gust_thunder);
    RUN_TEST(underwater_target_surf_whirlpool);
    RUN_TEST(locked_attacker_drops_variable_power);
    RUN_TEST(explosion_halves_defense_and_damp_fails);
    RUN_TEST(dream_eater_needs_sleeping_target);
    RUN_TEST(snore_and_fake_out_preconditions);
    RUN_TEST(fixed_damage_moves);
    RUN_TEST(fixed_damage_vs_wonder_guard);
    RUN_TEST(endeavor_hp_difference);
    RUN_TEST(spit_up_stockpile);
    RUN_TEST(ohko_rules);
}
