#ifndef TRAINER_CARD_H
#define TRAINER_CARD_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game.h"
#include "game/nuzlocke_read.h"

typedef enum { CARD_OK, CARD_NOT_SUPPORTED, CARD_NO_TEAM, CARD_FACILITY } CardStatus;

typedef struct {
    char name[GAME_NAME_LEN];   /* decoded player name */
    const char* game_name;      /* "Pok\xC3\xA9mon FireRed"; NULL in hand-built cards = Emerald */
    uint8_t region;             /* GameRegion: picks the badge colors */
    uint16_t public_id;         /* trainer_id & 0xFFFF only */
    uint32_t play_frames;
    uint8_t badges;             /* bit n-1 = badge n */
    uint16_t dex_owned;
    uint8_t count;              /* filled party slots, same order/skips as GameSnapshot.party */
    GamePartyMon party[6];
    NuzKey keys[6];
    uint32_t sig;               /* FNV-1a over sb1, sb2, raw party bytes, trainer id: equal sigs = stable read */
} TrainerCardData;

/* Reads straight from memory; does not need game_update to have run. */
CardStatus trainer_card_read(const GameState* gs, const GameMem* m, TrainerCardData* out);
/* sel: party slot chosen on the Party page, or -1. mvp: Run MVP key or NULL.
 * Returns a non-egg, non-bad slot, or -1 when the party has none. */
int trainer_card_pick_ace(const TrainerCardData* d, int sel, const NuzKey* mvp);

#endif // TRAINER_CARD_H
