#ifndef BERRY_H
#define BERRY_H

#include <stdbool.h>
#include <stdint.h>
#include "game/game_mem.h"
#include "game/game_profile.h"
#include "game/mirage.h"

#define BERRY_TREE_COUNT     128   /* SaveBlock1.berryTrees */
#define BERRY_PLACE_MAX      24
#define BERRY_PLACE_NONE     0xFF
#define BERRY_VIEW_MAX       96    /* the retail game has 88 trees */
#define BERRY_NAME_LEN       8     /* gBerries name: 6 chars */
#define BERRY_PLACE_NAME_LEN 24    /* GAME_MAP_NAME_LEN */
#define BERRY_FALLING_MIN    60    /* ripe berries this close to dropping are "falling" */

/* Most urgent first; the page and the alerts sort by this. */
typedef enum { BERRY_FALLING, BERRY_RIPE, BERRY_WATER, BERRY_GROWING, BERRY_PAUSED } BerryStatus;

typedef struct {
    uint8_t sec;     /* region map section id */
    uint8_t spots;   /* tree spots in this place */
    char name[BERRY_PLACE_NAME_LEN];
} BerryPlace;

/* Where each tree id is, from the ROM's map events. Built once per ROM. */
typedef struct {
    uint8_t place_of[BERRY_TREE_COUNT];  /* index into place[], or BERRY_PLACE_NONE */
    uint8_t count;
    BerryPlace place[BERRY_PLACE_MAX];
} BerryPlaces;

typedef struct {
    uint8_t id, place, berry, stage;  /* stage 1 planted .. 5 berries */
    uint8_t watered;                  /* bit i set: watered during stage i+1 */
    uint8_t regrowth;
    bool paused;                      /* stopGrowth: the game hasn't seen the tree yet */
    uint16_t next_min;                /* minutes to the next stage; at stage 5 until the berries drop */
    uint32_t ripe_min;                /* minutes until stage 5; 0 when ripe */
    uint8_t yield_lo, yield_hi;       /* equal once the game has rolled the yield */
    BerryStatus status;
    char name[BERRY_NAME_LEN];        /* ROM berry name, e.g. "ORAN" */
} BerryTree;

typedef struct {
    bool valid;      /* save data readable */
    bool clock_set;  /* FLAG_SYS_CLOCK_SET: berries only grow after this */
    bool projected;  /* timers moved forward by the RTC since the game's last update */
    bool in_battle;
    uint8_t here;    /* place of the current map, or BERRY_PLACE_NONE */
    uint8_t count;
    BerryTree tree[BERRY_VIEW_MAX];  /* trees with a berry, by id */
    uint8_t growing[BERRY_PLACE_MAX]; /* trees with a berry per place */
    uint8_t place_count;              /* copy of BerryPlaces, so the view stands alone */
    BerryPlace place[BERRY_PLACE_MAX];
} BerryView;

void berry_places_build(BerryPlaces* bp, const GameProfile* p, const GameMem* m);
/* Reads every tree with a berry and projects it to the clock's time. clk may be invalid. */
void berry_read(const BerryPlaces* bp, const GameProfile* p, const GameMem* m, const MirageClock* clk,
                BerryView* out);

/* Exposed for tests. Range of CalcBerryYieldInternal for `water` watered stages. */
void berry_yield_band(uint8_t min, uint8_t max, uint8_t water, uint8_t* lo, uint8_t* hi);
/* The game's BerryTreeTimeUpdate on one 8-byte tree, without the random yield roll. */
void berry_project(uint8_t raw[8], uint16_t stage_min, uint32_t minutes);
/* gLocalTime (RTC minus offset, with RtcCalcTimeDifference's borrows) in minutes. */
int32_t berry_local_minutes(const MirageClock* c, const MirageTime* off);

#endif // BERRY_H
