#ifndef OVERWORLD_H
#define OVERWORLD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "game/game_mem.h"
#include "game/emerald_profile.h"
#include "game/script_walk.h"

typedef struct GBA GBA;

#define OW_MAX_TRAINERS 16
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

#define OW_MAX_MARKERS   32
#define OW_ITEM_NAME_LEN 14

typedef enum { OW_MARK_HIDDEN, OW_MARK_ITEM_BALL, OW_MARK_GIFT, OW_MARK_FEEBAS, OW_MARK_FEEBAS_BLOCKED } OwMarkKind;

typedef struct {
    uint8_t kind;                      /* OwMarkKind */
    uint8_t name_count;                /* 1, or 2 for a choice gift */
    int16_t x, y;                      /* tile top-left, GBA px */
    char name[2][OW_ITEM_NAME_LEN];    /* raw ROM item names, decoded (Feebas: species name) */
    uint16_t spot;                     /* Feebas kinds: the game's fishing spot id */
    bool bridge;                       /* Feebas kinds: a bridge tile */
} OwMarker;

/* Points the way to the nearest fishable Feebas tile when none is on screen.
 * Screen px of the player's tile and of the target tile (off screen). */
typedef struct {
    bool show;
    int16_t px, py, tx, ty;
    char name[OW_ITEM_NAME_LEN];
} OwFeebasHint;

/* No pointers and zeroed before filling, so memcmp detects change. */
typedef struct {
    bool visible;          /* trainers, items and gifts */
    bool feebas_visible;   /* Feebas tiles; stays up while fishing locks the controls */
    uint8_t trainer_count, marker_count;
    OwTrainer trainers[OW_MAX_TRAINERS];
    OwMarker markers[OW_MAX_MARKERS];
    OwFeebasHint hint;
} OverworldSnapshot;

/* next = captured at the last line 160; shown = the capture before it, which is
 * what the frame just rendered displays. */
/* Gift walks per script, for the map the player is on. Not emulated state. */
#define OW_GIFT_CACHE 64
typedef struct { uint32_t script; ScriptGifts gifts; } OwGiftEntry;
typedef struct {
    bool valid;
    uint8_t group, num;             /* map the entries belong to */
    uint8_t count;
    OwGiftEntry entry[OW_GIFT_CACHE];
} OwGiftCache;

typedef struct {
    const GameProfile* profile;     /* NULL = unsupported ROM: snapshots stay empty */
    OverworldSnapshot next, shown;
    OwGiftCache gifts;
} OverworldFeed;

void overworld_read(const GameProfile* p, const GameMem* m, OverworldSnapshot* out);
/* Like overworld_read, plus NPC gift tags; cache may be NULL (no gift tags). */
void overworld_read_cached(const GameProfile* p, const GameMem* m, OwGiftCache* cache,
                           OverworldSnapshot* out);
void overworld_feed_init(OverworldFeed* f, const GameProfile* p);
void overworld_feed_clear(OverworldFeed* f);
void overworld_feed_capture(OverworldFeed* f, const GameMem* m);
/* GbaVblankObserver: ctx is the OverworldFeed. */
void overworld_capture(const GBA* gba, void* feed);
bool overworld_dump(const OverworldSnapshot* s, FILE* out);
/* Walks every NPC template in the ROM and writes one line per gift tag,
 * "<group>.<num> lid=<id> <item>:<flag>[,...]" (hex, pairs sorted). For checking
 * the walker against a known list; not used while playing. */
bool overworld_gift_audit(const GameProfile* p, const GameMem* m, FILE* out);

typedef struct { int16_t pos_x, pos_y, cam_dx, cam_dy; } OwScreenOrigin;
/* Top-left map coords and sub-tile camera offset used by tile_to_screen.
 * Screen px of VMap tile (vx, vy): ((vx - pos_x) * 16 + cam_dx, (vy - pos_y) * 16 + cam_dy). */
bool overworld_screen_origin(const GameProfile* p, const GameMem* m, OwScreenOrigin* out);
/* Map grid word at VMap tile: metatile behavior (attr & 0xFF) and elevation (bits 12-15). */
bool overworld_tile_info(const GameProfile* p, const GameMem* m, int16_t vx, int16_t vy, uint8_t* behavior,
                         uint8_t* elevation);

#endif // OVERWORLD_H
