#ifndef HGSS_TALK_H
#define HGSS_TALK_H

#include <stdbool.h>
#include <stdint.h>

#define HGSS_RULE_SIZE    20
#define HGSS_RULES        100        /* 0..29 local, 30..99 global */
#define HGSS_SCRIPT_SIZE  52
#define HGSS_ANIM_SIZE    80
#define HGSS_ANIM_FRAMES  10
#define HGSS_STEPS        5
#define HGSS_SOUND_CRY    2379

/* Context vector, indexed as in HGSS ov02_0224F058. */
enum {
    HC_HAS_ITEM = 0x00, HC_ITEM_CLASS = 0x01, HC_HP = 0x02, HC_STATUS = 0x03, HC_LEVEL = 0x04,
    HC_TYPE1 = 0x05, HC_TYPE2 = 0x06, HC_FRIEND = 0x07, HC_NATURE = 0x08, HC_GENDER = 0x09,
    HC_SPECIES_CLASS = 0x0A, HC_LEAVES = 0x0B, HC_NPCS = 0x0C, HC_HIDDEN = 0x0D, HC_WEATHER = 0x11,
    HC_TILE = 0x12, HC_ENCOUNTER = 0x13, HC_TIME = 0x14, HC_MOOD = 0x15, HC_ATHLON = 0x16,
    HC_FACING = 0x17, HC_SIZE = 0x1C
};

typedef struct {
    uint8_t c[HC_SIZE];
    uint16_t map_id;          /* never matches in Emerald: 0xFFFF */
} HgssTalkCtx;

typedef uint32_t (*HgssRand)(void* ud);                       /* any 32-bit value */
typedef bool (*HgssResultOk)(uint16_t result, void* ud);      /* script filter */

typedef struct {
    uint16_t anim, msg, sound;   /* msg is 1-based (0 none); anim 1-based (0 none) */
    uint8_t emote, wait;         /* emote 1-based (0 none) */
} HgssTalkStep;

typedef struct {
    uint8_t step_count;
    HgssTalkStep step[HGSS_STEPS];
    bool yes_no;
    uint16_t next_yes, next_no;
    int8_t friend_delta, mood_delta;
    uint8_t fashion, leaf;
} HgssTalkScript;

typedef struct {
    uint8_t facing;   /* 0 keep, else HGSS facing + 1 */
    uint8_t dur;
    int8_t dx, dy, dz;
    bool sound;
} HgssAnimFrame;

uint16_t hgss_rule_result(const uint8_t* rule);
/* rules: HGSS_RULES entries of HGSS_RULE_SIZE bytes, flat. Index of the first
 * passing rule in HGSS scan order, or -1. */
int32_t hgss_rule_pick(const uint8_t* rules, const HgssTalkCtx* ctx, HgssRand rnd,
                       HgssResultOk ok, void* ud);
bool hgss_script_parse(const uint8_t* b, uint32_t len, HgssTalkScript* out);
uint8_t hgss_anim_parse(const uint8_t* b, uint32_t len, HgssAnimFrame out[HGSS_ANIM_FRAMES]);
bool hgss_mood_band(uint8_t band, int8_t mood);
bool hgss_friend_band(uint8_t band, uint8_t friendship);

#endif // HGSS_TALK_H
