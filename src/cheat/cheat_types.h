#ifndef CHEAT_TYPES_H
#define CHEAT_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Types shared by the cheat parser, decoder, engine and file code. */

#define CHEAT_MAX_CHEATS 64
#define CHEAT_MAX_LINES  48     /* raw code lines per cheat */
#define CHEAT_MAX_OPS    96     /* decoded ops per cheat */
#define CHEAT_NAME_LEN   48

typedef enum {
    CHEAT_DEV_CODEBREAKER,
    CHEAT_DEV_GSA,          /* GameShark / Action Replay v1/v2, encrypted */
    CHEAT_DEV_PAR3,         /* Pro Action Replay v3 / GameShark v3, encrypted */
    CHEAT_DEV_GSA_RAW,
    CHEAT_DEV_PAR3_RAW,
    CHEAT_DEV_COUNT,
    CHEAT_DEV_AUTO = CHEAT_DEV_COUNT
} CheatDevice;

/* One pasted code line: an 8-digit word and a 4- or 8-digit word. */
typedef struct {
    uint32_t a, b;
    uint8_t digits;         /* digits in b: 4 (CodeBreaker) or 8 */
} CheatRawLine;

typedef struct {
    CheatRawLine lines[CHEAT_MAX_LINES];
    uint32_t count;
    char name[CHEAT_NAME_LEN];   /* first label line, "" if none */
} CheatRawList;

typedef enum {
    CHEAT_OP_WRITE,
    CHEAT_OP_ADD,
    CHEAT_OP_OR,
    CHEAT_OP_AND,
    CHEAT_OP_SLIDE,
    CHEAT_OP_IF,
    CHEAT_OP_ELSE,
    CHEAT_OP_ENDIF,
    CHEAT_OP_MARKER,        /* game ID, hook, key: recognized, does nothing */
    CHEAT_OP_UNSUPPORTED    /* recognized but not implemented (ROM patch, buttons, ...) */
} CheatOpKind;

typedef enum {
    CHEAT_CMP_EQ,
    CHEAT_CMP_NE,
    CHEAT_CMP_LT_S,
    CHEAT_CMP_GT_S,
    CHEAT_CMP_LT_U,
    CHEAT_CMP_GT_U,
    CHEAT_CMP_LE_U,
    CHEAT_CMP_GE_U,
    CHEAT_CMP_AND,          /* true when (mem & value) != 0 */
    CHEAT_CMP_KEYS,         /* true when every button in value is held */
    CHEAT_CMP_NEVER
} CheatCmp;

#define CHEAT_SKIP_BLOCK 0xFF   /* IF false: skip to the matching ELSE/ENDIF */
#define CHEAT_OPF_INDIRECT 0x01 /* WRITE target is [addr] + value2 */

typedef struct {
    uint8_t kind, width, cmp, skip;  /* width in bytes; skip = ops skipped when IF is false */
    uint8_t flags;
    uint32_t addr, value;
    uint32_t value2;                 /* indirect offset */
    uint16_t count;                  /* SLIDE repetitions */
    int16_t value_step;              /* SLIDE */
    uint32_t addr_step;              /* SLIDE, bytes */
} CheatOp;

#endif // CHEAT_TYPES_H
