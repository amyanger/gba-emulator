#include "game/wildgen.h"

#include <string.h>

#include "game/pokemon.h"
#include "game/rng3.h"

/* pret include/constants/abilities.h, pokemon.h */
#define ABILITY_STATIC 9
#define ABILITY_INTIMIDATE 22
#define ABILITY_SYNCHRONIZE 28
#define ABILITY_MAGNET_PULL 42
#define ABILITY_PRESSURE 46
#define ABILITY_KEEN_EYE 51
#define ABILITY_HUSTLE 55
#define ABILITY_CUTE_CHARM 56
#define ABILITY_VITAL_SPIRIT 72
#define TYPE_STEEL 8
#define TYPE_ELECTRIC 13

#define CERTAIN_MAX_ITERS 14 /* R2: loops this short always ended before the VBlank draw */
#define NO_SKIP UINT32_MAX

/* Random() stream; the draw at index `skip` (counted from the slot roll) is the VBlank IRQ's.
 * skip_after_pid (wildgen_step): it lands between the PID and IV1 instead. */
typedef struct {
    uint32_t s;
    uint32_t idx, skip;
    bool skip_after_pid;
} Draws;

static uint16_t wg_draw(Draws* d) {
    if (d->idx == d->skip) {
        d->s = rng3_next(d->s);
        d->idx++;
    }
    d->s = rng3_next(d->s);
    d->idx++;
    return rng3_hi(d->s);
}

static bool lead_has(const WildInput* in, uint8_t ability) {
    return in->lead_ok && in->lead_ability == ability;
}

/* Rule 5, TryGetRandomWildMonIndexByType (wild_encounter.c): validIndexes[Random() % count],
 * i.e. the n-th matching slot in index order; declines with no draw when 0 or all 12 match
 * (retail passes 12 for land). */
static bool slot_by_type(const WildInput* in, uint8_t type, Draws* d, uint8_t* slot) {
    uint8_t valid[WILD_SLOTS], n = 0;
    for (uint8_t i = 0; i < WILD_SLOTS; i++)
        if (in->slots[i].type1 == type || in->slots[i].type2 == type)
            valid[n++] = i;
    if (n == 0 || n == WILD_SLOTS)
        return false;
    *slot = valid[wg_draw(d) % n];
    return true;
}

/* Rule 5, TryGenerateWildMon land case (:422): Magnet Pull, then Static, then
 * ChooseWildMonIndex_Land (:182). Only the lead's own ability draws its % 2. */
static uint8_t choose_slot(const WildInput* in, Draws* d) {
    uint8_t slot;
    if (lead_has(in, ABILITY_MAGNET_PULL) && wg_draw(d) % 2 == 0 &&
        slot_by_type(in, TYPE_STEEL, d, &slot))
        return slot;
    if (lead_has(in, ABILITY_STATIC) && wg_draw(d) % 2 == 0 &&
        slot_by_type(in, TYPE_ELECTRIC, d, &slot))
        return slot;
    static const uint8_t cut[WILD_SLOTS] = {20, 40, 50, 60, 70, 80, 85, 90, 94, 98, 99, 100};
    uint16_t r = wg_draw(d) % 100;
    for (slot = 0; r >= cut[slot]; slot++) {
    }
    return slot;
}

/* Rule 6, ChooseWildMonLevel (:268) */
static uint8_t choose_level(const WildInput* in, const WildSlot* ws, Draws* d) {
    uint8_t min = ws->min_level, max = ws->max_level;
    if (min > max) {
        min = ws->max_level;
        max = ws->min_level;
    }
    uint8_t r = (uint8_t)(wg_draw(d) % (uint16_t)(max - min + 1));
    if (lead_has(in, ABILITY_HUSTLE) || lead_has(in, ABILITY_VITAL_SPIRIT) ||
        lead_has(in, ABILITY_PRESSURE)) {
        if (wg_draw(d) % 2 == 0)
            return max;
        if (r)
            r--;
    }
    return (uint8_t)(min + r);
}

/* Rule 7, IsWildLevelAllowedByRepel and IsAbilityAllowingEncounter (:897) */
static bool encounter_allowed(const WildInput* in, uint8_t level, Draws* d) {
    if (in->repel_active && level < in->repel_level)
        return false;
    if (lead_has(in, ABILITY_KEEN_EYE) || lead_has(in, ABILITY_INTIMIDATE)) {
        if (in->lead_level > 5 && level <= in->lead_level - 5 && wg_draw(d) % 2 == 0)
            return false;
    }
    return true;
}

/* Rule 8, CreateWildMon (:379): returns the gender to force, or MON_GENDER_NONE. */
static MonGender cute_charm_gender(const WildInput* in, const WildSlot* ws, Draws* d) {
    if (ws->gender_ratio == 0 || ws->gender_ratio == 254 || ws->gender_ratio == 255)
        return MON_GENDER_NONE;
    if (!lead_has(in, ABILITY_CUTE_CHARM) || wg_draw(d) % 3 == 0)
        return MON_GENDER_NONE;
    return in->lead_gender == MON_GENDER_FEMALE ? MON_GENDER_MALE : MON_GENDER_FEMALE;
}

/* Rule 9, PickWildMonNature (:335), outside the Safari Zone */
static uint8_t choose_nature(const WildInput* in, Draws* d) {
    if (lead_has(in, ABILITY_SYNCHRONIZE) && wg_draw(d) % 2 == 0)
        return (uint8_t)(in->lead_nature % 25); /* lead PID % 25; mod keeps the loop finite */
    return (uint8_t)(wg_draw(d) % 25);
}

/* Rule 10, CreateMonWithNature / CreateMonWithGenderNatureLetter (pokemon.c:2305, :2318).
 * Random32() takes the low half first (factcheck F5). */
static uint32_t pid_loop(uint8_t nature, MonGender want, uint8_t ratio, Draws* d, uint16_t* iters) {
    uint32_t pid;
    *iters = 0;
    do {
        uint32_t lo = wg_draw(d);
        pid = lo | (uint32_t)wg_draw(d) << 16;
        if (*iters < UINT16_MAX)
            (*iters)++;
    } while (pokemon_nature(pid) != nature ||
             (want != MON_GENDER_NONE && pokemon_gender(ratio, pid) != want));
    return pid;
}

/* Rules 5-12 from the slot roll. */
static void generate(const WildInput* in, Draws* d, WildPreview* out) {
    uint8_t slot = choose_slot(in, d);
    const WildSlot* ws = &in->slots[slot];
    uint8_t level = choose_level(in, ws, d);
    if (!encounter_allowed(in, level, d)) {
        out->kind = WG_NO_BATTLE;
        out->certain = true;
        return;
    }
    MonGender want = cute_charm_gender(in, ws, d);
    uint8_t nature = choose_nature(in, d);
    uint32_t pid = pid_loop(nature, want, ws->gender_ratio, d, &out->loop_iters);
    /* Rule 11, CreateBoxMon (pokemon.c:2206): IV1 = HP/Atk/Def, IV2 = Spe/SpA/SpD, 5 bits each. */
    if (d->skip_after_pid)
        d->skip = d->idx;
    uint32_t iv1 = wg_draw(d), iv2 = wg_draw(d);
    pokemon_unpack_ivs((iv1 & 0x7FFF) | (iv2 & 0x7FFF) << 15, out->ivs);

    out->kind = WG_BATTLE;
    out->slot = slot;
    out->species = ws->species;
    out->level = level;
    out->nature = nature;
    out->pid = pid;
    out->shiny = wildgen_shiny(pid, in->otid);
    out->certain = out->loop_iters <= CERTAIN_MAX_ITERS;
}

void wildgen_step(const WildInput* in, uint32_t s160, WildPreview* out) {
    memset(out, 0, sizeof(*out));
    if (in->method != WILD_LAND || in->outbreak_here)
        return; /* WG_NONE: not predicted in v1 (R4) */
    Draws d = {s160, 0, NO_SKIP, false};
    wg_draw(&d); /* the VBlank IRQ's draw */
    /* Rules 2-4: AllowWildCheckOnNewMetatile (:533), EncounterOddsCheck (:502), roamer.c:216 */
    if ((in->metatile_roll && wg_draw(&d) % 100 >= 60) || wg_draw(&d) % 2880 >= in->rate) {
        out->kind = WG_NO_BATTLE;
        out->certain = true;
        return;
    }
    if (in->roamer_here && wg_draw(&d) % 4 == 0) {
        out->certain = true;
        /* StandardWildEncounter (:604): Repel checks the roamer's level, no draw */
        if (in->repel_active && in->roamer_level < in->repel_level) {
            out->kind = WG_NO_BATTLE;
            return;
        }
        out->kind = WG_ROAMER;
        out->species = in->roamer_species;
        out->level = in->roamer_level;
        return;
    }
    d.idx = 0;
    d.skip_after_pid = true;
    generate(in, &d, out);
}

void wildgen_from_slot_state(const WildInput* in, uint32_t slot_state, uint16_t skip,
                             WildPreview* out) {
    memset(out, 0, sizeof(*out));
    if (in->method != WILD_LAND || in->outbreak_here)
        return;
    Draws d = {slot_state, 0, skip == UINT16_MAX ? NO_SKIP : skip, false};
    generate(in, &d, out);
}

/* include/pokemon.h GET_SHINY_VALUE < SHINY_ODDS (factcheck F6) */
bool wildgen_shiny(uint32_t pid, uint32_t otid) {
    return ((otid >> 16) ^ (otid & 0xFFFF) ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8;
}
