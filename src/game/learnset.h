#ifndef LEARNSET_H
#define LEARNSET_H

#include <stdint.h>

/* MAX_LEVEL_UP_MOVES: GetMoveRelearnerMoves never reads more entries than this. */
#define LEARNSET_MAX 20

typedef struct {
    uint8_t next_level;    /* lowest learnset level above the mon's, 0 when none left */
    uint8_t next_count;
    uint8_t relearn_count;
    uint16_t next[4];
    uint16_t relearn[LEARNSET_MAX];
} LearnsetView;

/* Level-up learnset entries (move = e & 0x1FF, level = e >> 9, 0xFFFF ends) to what the
 * game's relearner offers (GetMoveRelearnerMoves) and the next level-up moves. Scans at most
 * n and LEARNSET_MAX entries; ids 0 or >= move_count are skipped. */
void learnset_scan(const uint16_t* e, uint8_t n, uint8_t level, const uint16_t known[4],
                   uint16_t move_count, LearnsetView* out);

#endif // LEARNSET_H
