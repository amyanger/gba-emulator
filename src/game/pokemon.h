#ifndef POKEMON_H
#define POKEMON_H

#include <stdbool.h>
#include <stdint.h>

#define MON_PARTY_SIZE 100

typedef enum { MON_GENDER_MALE, MON_GENDER_FEMALE, MON_GENDER_NONE } MonGender;

/* Decrypted copy of a struct Pokemon (pokeemerald include/pokemon.h). */
typedef struct MonData {
    bool present;      /* hasSpecies flag set */
    bool bad;          /* checksum mismatch or isBadEgg: fields below unset */
    bool is_egg;
    uint32_t personality, ot_id;
    uint8_t nickname_raw[10];
    uint16_t species, held_item;
    uint32_t experience;
    uint8_t friendship;
    uint16_t moves[4];
    uint8_t pp[4];
    uint8_t evs[6];      /* HP, Atk, Def, Spe, SpA, SpD */
    uint32_t iv_word;
    uint8_t ivs[6];      /* HP, Atk, Def, Spe, SpA, SpD */
    uint8_t ability_num;
    /* Party-only fields (party == true) */
    uint32_t status;
    uint8_t level;
    uint16_t hp, max_hp;
} MonData;

/* Decrypts into `out`; never writes to `raw` (unlike the game's GetBoxMonData,
 * which marks a mon as a Bad Egg on checksum failure). Returns out->present. */
bool pokemon_decode(const uint8_t* raw, bool party, MonData* out);

void pokemon_unpack_ivs(uint32_t iv_word, uint8_t ivs[6]);
uint8_t pokemon_nature(uint32_t personality);
/* up/down index {Atk, Def, Spe, SpA, SpD}; both -1 for a neutral nature. */
void pokemon_nature_effect(uint8_t nature, int8_t* up, int8_t* down);
MonGender pokemon_gender(uint8_t gender_ratio, uint32_t personality);
uint8_t pokemon_hidden_power_type(const uint8_t ivs[6]);
uint8_t pokemon_hidden_power_power(const uint8_t ivs[6]);

#endif // POKEMON_H
