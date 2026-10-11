#include "game/nuzlocke_read.h"
#include "game/pokemon.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* SaveBlock2 (include/global.h) */
#define SB2_TRAINER_ID 0x0A
#define SB2_PLAY_TIME  0x0E  /* u16 hours, u8 minutes, u8 seconds, u8 frames */
#define SB2_MIN_SIZE   0x13
#define SB1_MIN_SIZE   0x3E08

#define MAP_HEADER_REGION_MAP_SEC 0x14
#define BOX_MON_SIZE    80
#define STORAGE_BOXES   4     /* offset of boxes[14][30] in struct PokemonStorage */
#define STORAGE_SIZE    (STORAGE_BOXES + NUZ_BOX_MAX * BOX_MON_SIZE)

/* struct SpeciesInfo growthRate, struct Evolution {method, param, target, pad} x 5 */
#define SPECIES_INFO_SIZE 28
#define SI_GROWTH_RATE    0x13
#define EVO_ENTRY_SIZE    8
#define EVOS_PER_MON      5

#define BMON_SIZE 0x58

bool nuz_key_eq(NuzKey a, NuzKey b) {
    return a.pid == b.pid && a.ot == b.ot && a.variant == b.variant;
}

bool nuz_is_shiny(uint32_t pid, uint32_t ot) {
    return ((ot & 0xFFFF) ^ (ot >> 16) ^ (pid & 0xFFFF) ^ (pid >> 16)) < 8;
}

void nuz_format_time(uint32_t frames, char* out, uint32_t out_size) {
    uint32_t s = frames / 60;
    snprintf(out, out_size, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
             (unsigned)(s % 60));
}

/* Largest level whose experience threshold the mon has reached. */
static uint8_t level_from_exp(const GameProfile* p, const GameMem* m, uint16_t species,
                              uint32_t exp) {
    uint8_t growth;
    if (species == 0 || species >= p->species_count ||
        !game_mem_read8(m, p->species_info + (uint32_t)species * SPECIES_INFO_SIZE + SI_GROWTH_RATE,
                        &growth) ||
        growth > 5)
        return 0;
    uint8_t level = 1;
    for (uint32_t l = 2; l <= 100; l++) {
        uint32_t need;
        if (!game_mem_read32(m, p->experience_tables + 4u * (growth * 101u + l), &need) || need > exp)
            break;
        level = (uint8_t)l;
    }
    return level;
}

static void fill_mon(const MonData* d, NuzMon* out) {
    memset(out, 0, sizeof(*out));
    out->key.pid = d->personality;
    out->key.ot = d->ot_id;
    out->key.variant = d->species == NUZ_SPECIES_SHEDINJA ? 1 : 0;
    out->species = d->species;
    out->level = d->level;
    out->met = d->met_location;
    out->egg = d->is_egg;
    out->hp = d->hp;
    memcpy(out->nick, d->nickname_raw, sizeof(out->nick));
}

static void read_party(const GameProfile* p, const GameMem* m, NuzFacts* f) {
    uint8_t count;
    if (!game_mem_read8(m, p->player_party_count, &count)) return;
    if (count > NUZ_PARTY_MAX) count = NUZ_PARTY_MAX;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t raw[MON_PARTY_SIZE];
        MonData d;
        /* A checksum failure (or a half-copied slot) leaves a hole the tracker skips. */
        NuzMon* out = &f->party[f->party_count++];
        memset(out, 0, sizeof(*out));
        if (!game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, sizeof(raw)) ||
            !pokemon_decode(raw, true, &d) || d.bad)
            continue;
        fill_mon(&d, out);
    }
}

static void read_boxes(const GameProfile* p, const GameMem* m, NuzFacts* f) {
    uint32_t base;
    f->has_boxes = true;
    f->box_count = 0;
    if (!game_mem_read32(m, p->pokemon_storage_ptr, &base) || !game_mem_in_ewram(base, STORAGE_SIZE))
        return;
    for (uint32_t i = 0; i < NUZ_BOX_MAX; i++) {
        uint8_t raw[BOX_MON_SIZE];
        MonData d;
        if (!game_mem_copy(m, base + STORAGE_BOXES + i * BOX_MON_SIZE, raw, sizeof(raw)) ||
            !pokemon_decode(raw, false, &d) || d.bad)
            continue;
        NuzMon* out = &f->boxes[f->box_count++];
        fill_mon(&d, out);
        out->level = level_from_exp(p, m, d.species, d.experience);
    }
}

static void read_battle(const GameProfile* p, const GameMem* m, NuzFacts* f) {
    uint8_t in_battle, count;
    f->active_party = -1;
    if (!game_mem_read8(m, p->main_in_battle, &in_battle) || !(in_battle & 0x02)) return;
    f->in_battle = true;
    game_mem_read32(m, p->battle_type_flags, &f->battle_flags);
    game_mem_read8(m, p->battle_outcome, &f->battle_outcome);

    uint8_t raw[MON_PARTY_SIZE];
    MonData d;
    if (game_mem_copy(m, p->enemy_party, raw, sizeof(raw)) && pokemon_decode(raw, true, &d) && !d.bad) {
        f->enemy_species = d.species;
        f->enemy_level = d.level;
        f->enemy_pid = d.personality;
        f->enemy_ot = d.ot_id;
    }

    if (!game_mem_read8(m, p->battlers_count, &count) || (count != 2 && count != 4)) return;
    for (uint8_t b = 0; b < count; b++) {
        uint8_t pos;
        uint16_t idx;
        if (!game_mem_read8(m, p->battler_positions + b, &pos) || (pos & 1)) continue;
        if (game_mem_read16(m, p->battler_party_indexes + 2u * b, &idx) && idx < f->party_count)
            f->active_party = (int8_t)idx;
        break;
    }
}

bool nuz_read(const GameProfile* p, const GameMem* m, bool with_boxes, NuzFacts* f) {
    uint32_t sb1 = 0, sb2 = 0;
    uint8_t pt[5];
    /* Boxes are read now and then; between reads the last ones stay for display. */
    uint16_t box_count = f->box_count;
    memset(f, 0, offsetof(NuzFacts, boxes));
    if (!with_boxes) f->box_count = box_count;
    f->active_party = -1;
    if (!p || !game_mem_read32(m, p->save_block1_ptr, &sb1) ||
        !game_mem_read32(m, p->save_block2_ptr, &sb2) || !game_mem_in_ewram(sb1, SB1_MIN_SIZE) ||
        !game_mem_in_ewram(sb2, SB2_MIN_SIZE) || !game_mem_read32(m, sb2 + SB2_TRAINER_ID, &f->trainer_id) ||
        !game_mem_copy(m, sb2 + SB2_PLAY_TIME, pt, sizeof(pt)))
        return false;
    f->valid = true;
    f->play_time = ((((uint32_t)(pt[0] | (pt[1] << 8)) * 60u + pt[2]) * 60u + pt[3]) * 60u) + pt[4];
    if (!game_mem_read8(m, p->map_header + MAP_HEADER_REGION_MAP_SEC, &f->mapsec)) f->mapsec = NUZ_SEC_NONE;
    read_party(p, m, f);
    read_battle(p, m, f);
    if (with_boxes) read_boxes(p, m, f);
    return true;
}

bool nuz_families(const GameProfile* p, const GameMem* m, uint16_t family[NUZ_SPECIES_MAX]) {
    uint16_t parent[NUZ_SPECIES_MAX];
    uint32_t count = p->species_count < NUZ_SPECIES_MAX ? p->species_count : NUZ_SPECIES_MAX;
    for (uint32_t s = 0; s < NUZ_SPECIES_MAX; s++) family[s] = parent[s] = (uint16_t)s;
    for (uint32_t s = 1; s < count; s++) {
        for (uint32_t e = 0; e < EVOS_PER_MON; e++) {
            uint8_t evo[EVO_ENTRY_SIZE];
            if (!game_mem_copy(m, p->evolution_table + (s * EVOS_PER_MON + e) * EVO_ENTRY_SIZE, evo,
                               sizeof(evo)))
                return false;
            uint16_t method = (uint16_t)(evo[0] | (evo[1] << 8));
            uint16_t target = (uint16_t)(evo[4] | (evo[5] << 8));
            if (method != 0 && target > 0 && target < count && parent[target] == target)
                parent[target] = (uint16_t)s;
        }
    }
    for (uint32_t s = 0; s < count; s++) {
        uint16_t r = (uint16_t)s;
        for (int hops = 0; hops < 4 && parent[r] != r; hops++) r = parent[r];
        family[s] = r;
    }
    return true;
}
