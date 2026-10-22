#ifndef TRAINER_CARD_DRAW_H
#define TRAINER_CARD_DRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "game/badge_pic.h"
#include "game/game.h"
#include "game/mon_pic.h"
#include "game/trainer_card.h"
#include "ui/ui_canvas.h"

#define CARD_W 1200
#define CARD_H 630

typedef enum { CARD_STYLE_GRID, CARD_STYLE_ACE } CardStyle;

bool card_style_from_name(const char* s, CardStyle* out);   /* "grid" | "ace" */
/* "<rom>.YYYYMMDD-HHMMSS.card.png"; with dir, "<dir>/<rom file name>.YYYYMMDD-HHMMSS.card.png" */
void trainer_card_path(const char* rom_path, const char* dir, time_t now, char* out, size_t out_size);
/* Nickname, or the species name when the nickname is empty or only '?' and spaces. Eggs give "Egg". */
const char* trainer_card_display_name(const GamePartyMon* mon);
/* ace < 0 (or an unusable slot) draws the Team grid whatever the style. */
/* art NULL (or not ok) draws plain badge dots. */
void trainer_card_draw(UiCanvas* c, const TrainerCardData* d, const uint32_t pics[6][MON_PIC_PIXELS],
                       const bool pic_ok[6], CardStyle style, int ace, const BadgeArt* art);
/* C with no Ctrl/Cmd/Alt and not a repeat; Shift picks the Ace style. Returns 0 for no card,
 * else 1 + CardStyle. */
uint8_t trainer_card_key_request(int32_t scancode, uint16_t mod, bool repeat);
/* Decode sprites, draw, save. Heap buffers, freed before return. */
bool trainer_card_save(const GameState* gs, const GameMem* m, const TrainerCardData* d,
                       CardStyle style, int ace, const char* path);

#endif // TRAINER_CARD_DRAW_H
