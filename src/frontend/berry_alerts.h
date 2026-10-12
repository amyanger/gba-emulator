#ifndef BERRY_ALERTS_H
#define BERRY_ALERTS_H

#include <stdbool.h>
#include <stdint.h>
#include "game/berry.h"

#define BERRY_TOAST_MAX 3
#define BERRY_TOAST_LEN 48   /* TOAST_TITLE_LEN / TOAST_DETAIL_LEN */

typedef struct {
    char key[BERRY_TOAST_LEN];     /* merge key: "berry:<place>" */
    char title[BERRY_TOAST_LEN];
    char detail[BERRY_TOAST_LEN];
} BerryToast;

/* Turns successive BerryViews into toasts. Zero-initialised or reset = no baseline yet. */
typedef struct {
    bool baselined;
    bool quiet;                              /* the next baseline shows no summary */
    uint8_t here;                            /* place at the last update */
    uint8_t stage[BERRY_TREE_COUNT];         /* last seen stage, 0 = no tree */
    uint8_t sent[BERRY_TREE_COUNT];          /* events already toasted for that stage */
    uint8_t ripe[BERRY_PLACE_MAX], water[BERRY_PLACE_MAX], fall[BERRY_PLACE_MAX];  /* waiting to show */
    char one[BERRY_PLACE_MAX][BERRY_NAME_LEN]; /* berry name when a place has a single event */
    char fall_in[BERRY_PLACE_MAX][16];         /* "40m" for a single falling tree */
} BerryAlerts;

/* Call when a game starts: the first update that sees a loaded save (clock set, trees
 * present) is the baseline, plus one summary toast when something needs the player. */
void berry_alerts_reset(BerryAlerts* a);
/* Savestate load or rewind: the next update re-baselines without a summary toast. */
void berry_alerts_rebase(BerryAlerts* a);
/* Returns the number of toasts written to out. An invalid view changes nothing. */
uint8_t berry_alerts_update(BerryAlerts* a, const BerryView* v, BerryToast* out, uint8_t max);

#endif // BERRY_ALERTS_H
