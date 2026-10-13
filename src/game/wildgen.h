#ifndef WILDGEN_H
#define WILDGEN_H

#include <stdbool.h>
#include <stdint.h>

/* Wild-Pokemon generation from an RNG state (pokeemerald src/wild_encounter.c), pure: no memory
 * access. Draw order and the VBlank skip: shiny-hunt factcheck F4/F6/F7. */

#define WILD_SLOTS 12

/* v1 predicts LAND only (R4); WATER kept for the table shape */
typedef enum { WILD_LAND, WILD_WATER } WildMethod;
typedef enum { WG_NONE, WG_NO_BATTLE, WG_BATTLE, WG_ROAMER } WildKind;

typedef struct {
    uint16_t species;
    uint8_t min_level, max_level;
    uint8_t type1, type2;
    uint8_t gender_ratio;
    bool two_abilities;
} WildSlot;

typedef struct {
    WildMethod method;
    WildSlot slots[WILD_SLOTS]; /* raw ROM slots, index order (not merged) */
    uint16_t rate;              /* EncounterOddsCheck threshold after modifiers, 0..2880 */
    bool metatile_roll; /* step entered a new metatile behavior (R3: false in predictions) */
    bool roamer_here;
    uint16_t roamer_species;
    uint8_t roamer_level;
    bool outbreak_here; /* v1: caller marks no prediction instead (R4) */
    bool lead_ok;       /* lead readable and not an egg; false = no lead ability effects */
    uint8_t lead_ability, lead_level, lead_nature;
    uint8_t lead_gender; /* MonGender of the lead */
    bool repel_active;
    uint8_t repel_level; /* lead level when repel steps > 0 */
    uint32_t otid;       /* TID | SID << 16 */
} WildInput;

typedef struct {
    uint8_t kind; /* WildKind */
    uint8_t slot; /* slot index when BATTLE */
    uint16_t species;
    uint8_t level;
    uint8_t nature;
    uint32_t pid;
    uint8_t ivs[6]; /* HP Atk Def Spe SpA SpD as pokemon_unpack_ivs */
    bool shiny;
    bool certain; /* false: loop long enough that the VBlank skip may fall inside it (R2) */
    uint16_t loop_iters;
} WildPreview;

/* A step completes on the frame whose line-160 RNG state is s160. Runs the Random() sequence of
 * factcheck F4 from there: +1 VBlank draw, [metatile roll], rate roll, [roamer roll],
 * slot, level, lead effects, nature, PID loop with the VBlank skip after the PID, IVs. */
void wildgen_step(const WildInput* in, uint32_t s160, WildPreview* out);
/* Same generation from an explicit slot-roll state with the skip at draw index `skip`
 * (counted from the slot roll; UINT16_MAX = no skip). For tests and fixtures. */
void wildgen_from_slot_state(const WildInput* in, uint32_t slot_state, uint16_t skip,
                             WildPreview* out);
bool wildgen_shiny(uint32_t pid, uint32_t otid);

#endif // WILDGEN_H
