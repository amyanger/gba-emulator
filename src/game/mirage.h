#ifndef MIRAGE_H
#define MIRAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/game_profile.h"

#define MIRAGE_FORECAST_MAX 3
#define MIRAGE_BOX_MONS     420  /* 14 boxes x 30 */
#define MIRAGE_OWNED_MAX    (6 + MIRAGE_BOX_MONS)

typedef enum { MIRAGE_NONE, MIRAGE_PARTY, MIRAGE_BOX } MirageWhere;

typedef struct {
    MirageWhere where;
    uint8_t slot;   /* party slot 0-5, or box number 1-14 */
    uint16_t days;  /* days from today; 0 = today */
    char name[16];  /* ROM species name (upper case), or "EGG"; GAME_NAME_LEN */
} MirageHit;

typedef struct {
    bool valid;          /* Mirage vars readable */
    bool clock_set;      /* FLAG_SYS_CLOCK_SET: the game steps the value only after this */
    bool clock_known;    /* RTC date and local time offset readable, so days can be forecast */
    uint16_t pending;    /* days the next map load will apply */
    uint32_t value;      /* the value the next map load will use */
    MirageHit today;     /* party match, else a box match, else where == MIRAGE_NONE */
    uint8_t forecast_count;
    MirageHit forecast[MIRAGE_FORECAST_MAX]; /* days >= 1, earliest first, one per day */
} MirageInfo;

/* The RTC's calendar time as the game reads it; year 0-99 is 2000-2099. */
typedef struct {
    bool valid;
    uint8_t year, month, day, hour, minute, second;
} MirageClock;

/* pokeemerald struct Time (SaveBlock2.localTimeOffset). */
typedef struct {
    int16_t days;
    int8_t hours, minutes, seconds;
} MirageTime;

typedef struct {
    uint16_t pid_lo;
    uint16_t species;
    bool egg;
    MirageWhere where;
    uint8_t slot;
} MirageMon;

typedef struct {
    uint32_t k;      /* LCG steps from the stored value */
    uint16_t index;  /* into the owned list */
} MirageMatch;

/* Owned Pokemon and the last forecast; lives in GameState across snapshots. */
typedef struct {
    MirageMon boxes[MIRAGE_BOX_MONS];
    uint16_t box_count;
    uint8_t box_age;       /* boxes are re-read when this wraps to 0 */
    MirageMon owned[MIRAGE_OWNED_MAX];
    uint16_t owned_count;
    bool have;             /* cached matches below are valid for `key` */
    uint32_t key;
    uint8_t today_n, forecast_n;
    MirageMatch today_m, forecast_m[MIRAGE_FORECAST_MAX];
} MirageCache;

/* ISO_RANDOMIZE2, applied once per elapsed day (pokeemerald UpdateMirageRnd). */
uint32_t mirage_step(uint32_t v);
/* pokeemerald ConvertDateToDayCount: days since the start of 2000, 2000-01-01 = 1. */
uint16_t mirage_day_count(uint8_t year, uint8_t month, uint8_t day);
/* The game's gLocalTime.days: RTC day count minus the offset, with RtcCalcTimeDifference's borrows. */
int32_t mirage_local_days(const MirageClock* c, const MirageTime* off);
/* Up to `max` step counts k in [k_from, k_to] (earliest first, one per k) where the value's high
 * half equals an owned PID's low half. Ties go to the earliest owned entry (party before boxes). */
uint8_t mirage_search(uint32_t value, const MirageMon* owned, uint16_t count, uint32_t k_from,
                      uint32_t k_to, MirageMatch* out, uint8_t max);
/* Fill `out` from save blocks at sb1/sb2. Pure reader. */
void mirage_read(MirageCache* mc, const GameProfile* p, const GameMem* m, const MirageClock* clk,
                 uint32_t sb1, uint32_t sb2, MirageInfo* out);

#endif // MIRAGE_H
