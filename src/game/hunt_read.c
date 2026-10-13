#include "game/hunt_read.h"
#include "game/encounters.h"
#include "game/pokemon.h"
#include <string.h>

/* SaveBlock1 (include/global.h) */
#define SB1_MAP_GROUP 0x04
#define SB1_MAP_NUM 0x05
#define SB1_FLAGS 0x1270
#define SB1_VARS 0x139C
#define VARS_START 0x4000

/* struct Roamer (include/global.h) */
#define ROAMER_SPECIES 0x08
#define ROAMER_LEVEL 0x0C
#define ROAMER_ACTIVE 0x13
#define ROAMER_SIZE 0x14

/* Outbreak fields from outbreakPokemonSpecies (include/global.h 0x2B90..) */
#define OUTBREAK_MAP_NUM 0x02
#define OUTBREAK_MAP_GROUP 0x03

/* struct WildPokemonHeader {u8 group, num; pad; landMonsInfo*, ...}, WildPokemonInfo {u8 rate} */
#define WILD_HEADER_SIZE 20
#define WILD_HEADER_LAND 4

/* struct SpeciesInfo */
#define SPECIES_INFO_SIZE 28
#define SI_TYPE1 0x06
#define SI_TYPE2 0x07
#define SI_GENDER_RATIO 0x10
#define SI_ABILITY1 0x16
#define SI_ABILITY2 0x17

/* include/constants: flags.h, vars.h, global.fieldmap.h, items.h, abilities.h, battle.h */
#define FLAG_SYS_ENC_UP_ITEM 0x8AD   /* White Flute */
#define FLAG_SYS_ENC_DOWN_ITEM 0x8AE /* Black Flute */
#define VAR_ALTERING_CAVE_WILD_SET 0x403E
#define NUM_ALTERING_CAVE_TABLES 9
#define AVATAR_MACH_BIKE 0x02
#define AVATAR_ACRO_BIKE 0x04
#define AVATAR_SURFING 0x08
#define ITEM_CLEANSE_TAG 190
#define ABILITY_STENCH 1
#define ABILITY_SAND_VEIL 8
#define ABILITY_ILLUMINATE 35
#define ABILITY_ARENA_TRAP 71
#define ABILITY_WHITE_SMOKE 73
#define BATTLE_TYPE_PIKE (1u << 20)
#define BATTLE_TYPE_PYRAMID (1u << 21)
#define MAX_ENCOUNTER_RATE 2880
/* src/metatile_behavior.c sTileBitAttributes flags; field_control_avatar.c:670 */
#define TILE_FLAG_HAS_ENCOUNTERS 0x01
#define TILE_FLAG_SURFABLE 0x02
#define WILD_IMMUNITY_STEPS 4
/* struct DayCare (include/global.h): mons[2] of 0x8C bytes {BoxPokemon, DaycareMail, u32 steps} */
#define DAYCARE_MON_SIZE 0x8C
#define DAYCARE_MON_STEPS 0x88
#define DAYCARE_OFFSPRING 0x118
#define DAYCARE_STEP_COUNTER 0x11C
#define BOX_FLAGS 0x13 /* isBadEgg:1, hasSpecies:1, isEgg:1 */
#define BOX_HAS_SPECIES 0x02
#define PARTY_STATUS 0x50
#define PARTY_HP 0x56
#define STATUS1_PSN_ANY 0x88 /* include/constants/battle.h */
#define PARTY_SIZE 6
#define MATCH_CALL_STEP_COUNTER 6
#define MATCH_CALL_STEPS 10
#define FRIENDSHIP_STEPS 128
#define POISON_STEPS 4

static bool flag_get(const GameMem* m, uint32_t sb1, uint16_t flag) {
    uint8_t b;
    return game_mem_read8(m, sb1 + SB1_FLAGS + flag / 8u, &b) && (b >> (flag % 8u)) & 1;
}

static uint16_t var_get(const GameMem* m, uint32_t sb1, uint16_t var) {
    uint16_t v = 0;
    game_mem_read16(m, sb1 + SB1_VARS + 2u * (uint32_t)(var - VARS_START), &v);
    return v;
}

static bool species_info(const GameProfile* p, const GameMem* m, uint16_t sp,
                         uint8_t info[SPECIES_INFO_SIZE]) {
    return sp < p->species_count &&
           game_mem_copy(m, p->species_info + (uint32_t)sp * SPECIES_INFO_SIZE, info,
                         SPECIES_INFO_SIZE);
}

/* R7 / review F2: the battle's return callback tells wild, Safari and scripted statics apart. */
static uint8_t battle_kind(const GameProfile* p, const GameMem* m, const NuzFacts* nf) {
    uint32_t cb;
    if (!nf->in_battle)
        return HUNT_BATTLE_NONE;
    if (!game_mem_read32(m, p->main_saved_callback, &cb))
        return HUNT_BATTLE_OTHER;
    if (cb == p->cb2_end_wild_battle)
        return nf->battle_flags & (BATTLE_TYPE_PIKE | BATTLE_TYPE_PYRAMID) ? HUNT_BATTLE_OTHER
                                                                           : HUNT_BATTLE_WILD;
    if (cb == p->cb2_end_safari_battle)
        return HUNT_BATTLE_SAFARI;
    if (cb == p->cb2_end_scripted_wild_battle)
        return HUNT_BATTLE_STATIC;
    return HUNT_BATTLE_OTHER;
}

/* GetCurrentMapWildMonHeaderId (wild_encounter.c:304): first header for the map; Altering Cave
 * adds VAR_ALTERING_CAVE_WILD_SET. Returns false when the map has no header. */
static bool find_header(const GameProfile* p, const GameMem* m, uint32_t sb1, uint8_t group,
                        uint8_t num, uint16_t* index) {
    for (uint16_t i = 0; i < p->wild_header_cap; i++) {
        uint8_t g, n;
        if (!game_mem_read8(m, p->wild_mon_headers + (uint32_t)i * WILD_HEADER_SIZE, &g) ||
            !game_mem_read8(m, p->wild_mon_headers + (uint32_t)i * WILD_HEADER_SIZE + 1, &n) ||
            g == 0xFF)
            return false;
        if (g != group || n != num)
            continue;
        if (group == p->altering_cave_map[0] && num == p->altering_cave_map[1]) {
            uint16_t set = var_get(m, sb1, VAR_ALTERING_CAVE_WILD_SET);
            i = (uint16_t)(i + (set < NUM_ALTERING_CAVE_TABLES ? set : 0));
        }
        *index = i;
        return true;
    }
    return false;
}

/* The land table's 12 raw slots in index order and its encounterRate byte. */
static bool read_land(const GameProfile* p, const GameMem* m, uint16_t index, WildInput* w,
                      uint8_t* rate) {
    EncSlot slots[WILD_SLOTS];
    uint8_t g, n;
    uint32_t info;
    /* encounters_header_slots drops empty species; a retail land table has all 12 filled, so a
     * shorter list means the slot indexes would be off: no prediction. */
    if (encounters_header_slots(m, p->wild_mon_headers, index, p->species_count, ENC_LAND, &g, &n,
                                slots) != WILD_SLOTS ||
        !game_mem_read32(
            m, p->wild_mon_headers + (uint32_t)index * WILD_HEADER_SIZE + WILD_HEADER_LAND,
            &info) ||
        !game_mem_read8(m, info, rate))
        return false;
    for (int i = 0; i < WILD_SLOTS; i++) {
        uint8_t info_bytes[SPECIES_INFO_SIZE];
        WildSlot* s = &w->slots[i];
        if (!species_info(p, m, slots[i].species, info_bytes))
            return false;
        s->species = slots[i].species;
        s->min_level = slots[i].min_level;
        s->max_level = slots[i].max_level;
        s->type1 = info_bytes[SI_TYPE1];
        s->type2 = info_bytes[SI_TYPE2];
        s->gender_ratio = info_bytes[SI_GENDER_RATIO];
        s->two_abilities = info_bytes[SI_ABILITY2] != 0;
    }
    return true;
}

/* IsWildLevelAllowedByRepel (:879): the first party mon with HP that is not an egg. A hole in
 * NuzFacts (checksum failure) before that mon makes the answer unknown. */
static bool read_repel(const GameMem* m, uint32_t sb1, const GameProfile* p, const NuzFacts* nf,
                       WildInput* w) {
    w->repel_active = var_get(m, sb1, p->repel_var) != 0;
    if (!w->repel_active)
        return true;
    w->repel_level = 0xFF; /* none found: the game lets nothing through */
    for (uint8_t i = 0; i < nf->party_count; i++) {
        const NuzMon* mon = &nf->party[i];
        if (mon->species == 0)
            return false;
        if (mon->hp && !mon->egg) {
            w->repel_level = mon->level;
            break;
        }
    }
    return true;
}

static bool party_slot(const GameProfile* p, const GameMem* m, uint8_t i,
                       uint8_t raw[MON_PARTY_SIZE]) {
    return game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, MON_PARTY_SIZE);
}

/* TryStartStepCountScript (field_control_avatar.c:536) and UpdateRepelCounter run on every step
 * before CheckStandardWildEncounter. When one of them draws Random() or starts a script instead of
 * the wild check, that one step is not predicted. Not checked: one-off story calls, abnormal
 * weather, the S.S. Tidal counter. */
static const char* step_scripts(const GameProfile* p, const GameMem* m, const NuzFacts* nf,
                                uint32_t sb1) {
    static const char* const k_next = "Walk one more step to predict";
    uint8_t raw[MON_PARTY_SIZE];
    MonData mon;

    /* UpdateFriendshipStepCounter (:614): every 128th step AdjustFriendship (pokemon.c:5914)
     * draws Random() & 1 per party mon. Live, that work also runs past the next VBlank, whose draw
     * then lands inside the sequence, so the count alone can't be modelled. */
    if ((var_get(m, sb1, p->friendship_step_var) + 1u) % FRIENDSHIP_STEPS == 0)
        return k_next;
    /* UpdatePoisonStepCounter (:637): every 4th step DoPoisonFieldEffect (field_poison.c:120)
     * takes 1 HP from each poisoned mon; one dropping to 0 starts the faint script. */
    if ((var_get(m, sb1, p->poison_step_var) + 1u) % POISON_STEPS == 0) {
        for (uint8_t i = 0; i < PARTY_SIZE; i++) {
            uint32_t status;
            uint16_t hp;
            if (!party_slot(p, m, i, raw))
                return k_next;
            status = (uint32_t)raw[PARTY_STATUS] | (uint32_t)raw[PARTY_STATUS + 1] << 8 |
                     (uint32_t)raw[PARTY_STATUS + 2] << 16 | (uint32_t)raw[PARTY_STATUS + 3] << 24;
            hp = (uint16_t)(raw[PARTY_HP] | raw[PARTY_HP + 1] << 8);
            if ((raw[BOX_FLAGS] & BOX_HAS_SPECIES) && (status & STATUS1_PSN_ANY) && hp <= 1)
                return k_next;
        }
    }
    /* ShouldEggHatch -> TryProduceOrHatchEgg (daycare.c:879) */
    uint32_t dc = sb1 + p->daycare_sb1_offset;
    uint8_t f0, f1, counter;
    uint32_t offspring, steps1;
    if (!game_mem_read8(m, dc + BOX_FLAGS, &f0) ||
        !game_mem_read8(m, dc + DAYCARE_MON_SIZE + BOX_FLAGS, &f1) ||
        !game_mem_read32(m, dc + DAYCARE_OFFSPRING, &offspring) ||
        !game_mem_read32(m, dc + DAYCARE_MON_SIZE + DAYCARE_MON_STEPS, &steps1) ||
        !game_mem_read8(m, dc + DAYCARE_STEP_COUNTER, &counter))
        return k_next;
    /* The egg roll: Random() when both parents are in and no egg waits, on every 255th step of
     * the second parent; a success draws more (TriggerPendingDaycareEgg). */
    if ((f0 & BOX_HAS_SPECIES) && (f1 & BOX_HAS_SPECIES) && offspring == 0 &&
        ((steps1 + 1u) & 0xFFu) == 0xFFu)
        return k_next;
    /* The hatch check when the u8 counter reaches 255: a party Egg with 0 cycles left (the
     * friendship byte) hatches instead of the wild check. */
    if (counter == 254) {
        for (uint8_t i = 0; i < nf->party_count && i < PARTY_SIZE; i++) {
            if (!party_slot(p, m, i, raw))
                return k_next;
            if (pokemon_decode(raw, true, &mon) && !mon.bad && mon.is_egg && mon.friendship == 0)
                return k_next;
        }
    }
    /* TryStartMatchCall (match_call.c:1156): every 10th step, once 10 RTC minutes have passed,
     * Random() % 10 decides on a call. The minutes check needs the live RTC, so that step is
     * never predicted. */
    uint8_t mc_steps;
    if (flag_get(m, sb1, p->match_call_flag) &&
        (!game_mem_read8(m, p->match_call_state + MATCH_CALL_STEP_COUNTER, &mc_steps) ||
         mc_steps + 1u >= MATCH_CALL_STEPS))
        return k_next;
    /* UpdateRepelCounter (wild_encounter.c:850): the last Repel step runs the "wore off" script. */
    if (var_get(m, sb1, p->repel_var) == 1)
        return "Repel is wearing off";
    return NULL;
}

/* Fills out->wild for a grass step (factcheck F4 steps 2-11); returns NULL, or why not. Lead
 * effects follow pret: abilities, Synchronize, Cute Charm and Keen Eye/Intimidate use
 * gPlayerParty[0] unless it is an egg; the Cleanse Tag is checked on gPlayerParty[0] even then. */
static const char* read_prediction(const GameProfile* p, const GameMem* m, const NuzFacts* nf,
                                   uint32_t sb1, HuntFacts* out) {
    WildInput* w = &out->wild;
    uint8_t avatar = 0, rate_byte = 0;
    uint16_t header;
    uint8_t outbreak[4];

    if (out->in_battle)
        return "Not predicted in battle";
    if (!game_mem_read8(m, p->player_avatar_flags, &avatar))
        return "Lead Pokemon unreadable";
    if (avatar & AVATAR_SURFING)
        return "Not predicted while surfing";
    if (flag_get(m, sb1, p->safari_flag))
        return "Not predicted in the Safari Zone";
    if (!find_header(p, m, sb1, out->map_group, out->map_num, &header) ||
        !read_land(p, m, header, w, &rate_byte))
        return "Not predicted here: no grass";
    /* DoMassOutbreakEncounterTest (:481) */
    if (game_mem_copy(m, sb1 + p->outbreak_sb1_offset, outbreak, sizeof(outbreak)) &&
        (outbreak[0] | outbreak[1]) != 0 && outbreak[OUTBREAK_MAP_NUM] == out->map_num &&
        outbreak[OUTBREAK_MAP_GROUP] == out->map_group)
        return "Not predicted during an outbreak";
    /* CheckStandardWildEncounter (field_control_avatar.c:668) keeps the last step tile's behavior;
     * StandardWildEncounter checks it with MetatileBehavior_IsLandWildEncounter (u8 index). After a
     * warp or a battle the first 4 steps run no check at all, and they also refresh the tile. */
    uint16_t behavior;
    uint8_t bits, immunity;
    if (!game_mem_read16(m, p->prev_metatile_behavior, &behavior) ||
        !game_mem_read8(m, p->tile_bit_attributes + (behavior & 0xFFu), &bits) ||
        (bits & (TILE_FLAG_HAS_ENCOUNTERS | TILE_FLAG_SURFABLE)) != TILE_FLAG_HAS_ENCOUNTERS)
        return "Step into tall grass to predict";
    if (!game_mem_read8(m, p->wild_immunity_steps, &immunity) || immunity < WILD_IMMUNITY_STEPS)
        return "Walk a few steps to predict";
    const char* why = step_scripts(p, m, nf, sb1);
    if (why)
        return why;

    uint8_t raw[MON_PARTY_SIZE], info[SPECIES_INFO_SIZE];
    MonData lead;
    if (nf->party_count == 0 || !game_mem_copy(m, p->player_party, raw, sizeof(raw)) ||
        !pokemon_decode(raw, true, &lead) || lead.bad || !species_info(p, m, lead.species, info) ||
        !read_repel(m, sb1, p, nf, w))
        return "Lead Pokemon unreadable";
    w->lead_ok = !lead.is_egg;
    if (w->lead_ok) {
        /* GetAbilityBySpecies: abilityNum picks the slot, even an empty second one. */
        w->lead_ability = lead.ability_num ? info[SI_ABILITY2] : info[SI_ABILITY1];
        w->lead_level = lead.level;
        w->lead_nature = pokemon_nature(lead.personality);
        w->lead_gender = (uint8_t)pokemon_gender(info[SI_GENDER_RATIO], lead.personality);
    }
    /* Sand Veil halves the rate only in a sandstorm; not modelled. */
    if (w->lead_ok && w->lead_ability == ABILITY_SAND_VEIL)
        return "Not predicted with Sand Veil";

    /* WildEncounterCheck (:502) */
    uint32_t rate = rate_byte * 16u;
    if (avatar & (AVATAR_MACH_BIKE | AVATAR_ACRO_BIKE))
        rate = rate * 80 / 100;
    if (flag_get(m, sb1, FLAG_SYS_ENC_UP_ITEM))
        rate += rate / 2;
    else if (flag_get(m, sb1, FLAG_SYS_ENC_DOWN_ITEM))
        rate /= 2;
    if (lead.held_item == ITEM_CLEANSE_TAG)
        rate = rate * 2 / 3;
    if (w->lead_ok) {
        /* Stench is x3/4 only in the Pyramid, which has no wild header here. */
        if (w->lead_ability == ABILITY_STENCH || w->lead_ability == ABILITY_WHITE_SMOKE)
            rate /= 2;
        else if (w->lead_ability == ABILITY_ILLUMINATE || w->lead_ability == ABILITY_ARENA_TRAP)
            rate *= 2;
    }
    w->rate = (uint16_t)(rate > MAX_ENCOUNTER_RATE ? MAX_ENCOUNTER_RATE : rate);

    /* IsRoamerAt (roamer.c:186) */
    uint8_t roamer[ROAMER_SIZE], loc[2];
    if (game_mem_copy(m, sb1 + p->roamer_sb1_offset, roamer, sizeof(roamer)) &&
        game_mem_copy(m, p->roamer_location, loc, sizeof(loc)) && roamer[ROAMER_ACTIVE] &&
        loc[0] == out->map_group && loc[1] == out->map_num) {
        w->roamer_here = true;
        w->roamer_species = (uint16_t)(roamer[ROAMER_SPECIES] | (roamer[ROAMER_SPECIES + 1] << 8));
        w->roamer_level = roamer[ROAMER_LEVEL];
    }
    w->method = WILD_LAND;
    w->metatile_roll = false; /* R3: predictions assume grass-to-grass steps */
    w->otid = nf->trainer_id;
    return NULL;
}

bool hunt_read(const GameProfile* p, const GameMem* m, const NuzFacts* nf, bool predict,
               HuntFacts* out) {
    uint32_t sb1, cb2;
    memset(out, 0, sizeof(*out));
    if (!p || !nf || !nf->valid || !game_mem_read32(m, p->save_block1_ptr, &sb1) ||
        !game_mem_in_ewram(sb1, (uint32_t)p->roamer_sb1_offset + ROAMER_SIZE) ||
        !game_mem_read32(m, p->rng_value, &out->rng) ||
        !game_mem_read32(m, p->rand_count, &out->rand_count) ||
        !game_mem_read8(m, sb1 + SB1_MAP_GROUP, &out->map_group) ||
        !game_mem_read8(m, sb1 + SB1_MAP_NUM, &out->map_num))
        return false;
    out->valid = true;
    out->otid = nf->trainer_id;
    out->on_overworld = game_mem_read32(m, p->main_callback2, &cb2) && cb2 == p->cb2_overworld;
    out->in_battle = nf->in_battle;
    out->battle_kind = battle_kind(p, m, nf);
    /* nuz_read fills the enemy only when slot 0 decodes with a matching checksum. */
    out->enemy_complete = nf->in_battle && nf->enemy_species != 0;
    out->enemy_species = nf->enemy_species;
    out->enemy_pid = nf->enemy_pid;
    out->enemy_ot = nf->enemy_ot;
    out->enemy_level = nf->enemy_level;
    if (!predict)
        return true;
    const char* why = read_prediction(p, m, nf, sb1, out);
    out->predictable = why == NULL;
    if (why) {
        memset(&out->wild, 0, sizeof(out->wild));
        game_strcpy(out->why_not, sizeof(out->why_not), why);
    }
    return true;
}
