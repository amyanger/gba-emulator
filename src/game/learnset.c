#include "game/learnset.h"
#include <stdbool.h>
#include <string.h>

#define LEARNSET_END 0xFFFF

static bool has(const uint16_t* list, uint8_t n, uint16_t mv) {
    for (uint8_t i = 0; i < n; i++)
        if (list[i] == mv) return true;
    return false;
}

void learnset_scan(const uint16_t* e, uint8_t n, uint8_t level, const uint16_t known[4],
                   uint16_t move_count, LearnsetView* out) {
    memset(out, 0, sizeof(*out));
    if (n > LEARNSET_MAX) n = LEARNSET_MAX;
    for (uint8_t i = 0; i < n && e[i] != LEARNSET_END; i++) {
        uint16_t mv = e[i] & 0x1FF;
        uint8_t lv = (uint8_t)(e[i] >> 9);
        if (mv == 0 || mv >= move_count || has(known, 4, mv)) continue;
        if (lv <= level) {
            if (!has(out->relearn, out->relearn_count, mv)) out->relearn[out->relearn_count++] = mv;
            continue;
        }
        /* A level the mon levels into but whose moves it already knows teaches nothing. */
        if (out->next_level == 0 || lv < out->next_level) {
            out->next_level = lv;
            out->next_count = 0;
        }
        if (lv == out->next_level && out->next_count < 4 && !has(out->next, out->next_count, mv))
            out->next[out->next_count++] = mv;
    }
}
