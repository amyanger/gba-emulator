#ifndef DAYCARE_H
#define DAYCARE_H

#include <stdbool.h>
#include <stdint.h>
#include "game/pokemon.h"
#include "game/game_profile.h"

#define DAYCARE_MON_SIZE     0x8C   /* struct DaycareMon (global.h:782-787) */
#define DAYCARE_MON_STEPS    0x88
#define EGG_GROUP_DITTO        13
#define EGG_GROUP_UNDISCOVERED 15
#define ABILITY_MAGMA_ARMOR    40
#define ABILITY_FLAME_BODY     49

typedef enum {
    DAYCARE_COMPAT_OK, DAYCARE_UNDISCOVERED, DAYCARE_TWO_DITTO, DAYCARE_SAME_GENDER,
    DAYCARE_GENDERLESS, DAYCARE_NO_SHARED_GROUP
} DaycareCompatReason;

typedef struct {
    uint16_t species;
    uint32_t ot_id, personality;
    uint8_t egg_group[2];
    uint8_t gender_ratio;
} DaycareParent;

typedef struct {
    bool egg, bad;        /* bad: Bad Egg, skipped */
    uint8_t cycles;       /* friendship byte of an egg */
    bool fire;            /* non-egg with ability 40/49, or, for an egg, has it once hatched */
} DaycareSlot;

uint8_t daycare_compat(const DaycareParent* a, const DaycareParent* b, DaycareCompatReason* reason);
uint16_t daycare_next_roll(uint32_t steps1);                     /* 1..256 */
void daycare_hatch_steps(const DaycareSlot slots[6], uint8_t count, uint8_t step_counter,
                         uint32_t out[6]);                        /* 0 for non-eggs */
uint8_t daycare_level_for_exp(const uint32_t table[101], uint32_t exp); /* table = growth row */
uint32_t daycare_cost(uint8_t level_now, uint8_t level_deposit); /* 100 + 100 * gained */
/* parent[] = SpeciesIndex.parent (child -> pre-evolution, 0 = none), count = its length. */
uint16_t daycare_egg_species(const uint16_t species[2], const MonGender gender[2],
                             const uint16_t held_item[2], uint32_t pid, const uint16_t* parent,
                             uint16_t count, const GameProfile* p);

#endif
