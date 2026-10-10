#ifndef OVERWORLD_H
#define OVERWORLD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "game/game_mem.h"
#include "game/emerald_profile.h"

typedef struct GBA GBA;

#define OW_MAX_TRAINERS 16
#define OW_MAX_HIDDEN   32
#define OW_CLASS_LEN    14

/* Game directions are 1 south, 2 north, 3 west, 4 east; OwDir is that minus one. */
typedef enum { OW_DIR_DOWN, OW_DIR_UP, OW_DIR_LEFT, OW_DIR_RIGHT, OW_DIR_COUNT } OwDir;

typedef struct {
    int16_t x, y;                   /* top-left of the trainer's 16x16 tile, GBA screen px */
    uint16_t id;                    /* trainer id, valid when known */
    uint8_t facing;                 /* OwDir */
    uint8_t range;                  /* sight range from the object */
    uint8_t clear[OW_DIR_COUNT];    /* tiles seen per direction after the sight walk */
    bool buried, beaten, known;
    char class_name[OW_CLASS_LEN];  /* ROM class name, "" when unknown */
} OwTrainer;

typedef struct { int16_t x, y; } OwPoint;   /* tile top-left, GBA screen px */

/* No pointers and zeroed before filling, so memcmp detects change. */
typedef struct {
    bool visible;
    uint8_t trainer_count, hidden_count;
    OwTrainer trainers[OW_MAX_TRAINERS];
    OwPoint hidden[OW_MAX_HIDDEN];
} OverworldSnapshot;

/* next = captured at the last line 160; shown = the capture before it, which is
 * what the frame just rendered displays. */
typedef struct {
    const GameProfile* profile;     /* NULL = unsupported ROM: snapshots stay empty */
    OverworldSnapshot next, shown;
} OverworldFeed;

void overworld_read(const GameProfile* p, const GameMem* m, OverworldSnapshot* out);
void overworld_feed_init(OverworldFeed* f, const GameProfile* p);
void overworld_feed_clear(OverworldFeed* f);
void overworld_feed_capture(OverworldFeed* f, const GameMem* m);
/* GbaVblankObserver: ctx is the OverworldFeed. */
void overworld_capture(const GBA* gba, void* feed);
bool overworld_dump(const OverworldSnapshot* s, FILE* out);

#endif // OVERWORLD_H
