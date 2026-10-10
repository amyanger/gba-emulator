#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>

/* Design tokens from the approved mockups (style A). Colors are ARGB8888. */
#define UI_BG          0xFF11131Au
#define UI_SURFACE     0xFF171B25u   /* raised row/card */
#define UI_SELECTED    0xFF1C2233u
#define UI_DIVIDER     0xFF252A36u
#define UI_TRACK       0xFF252A36u
#define UI_TEXT        0xFFE7E9EEu
#define UI_MUTED       0xFF8A91A2u
#define UI_DIM         0xFF7D8494u
#define UI_FAINT       0xFF626A7Bu
#define UI_ACCENT      0xFF5B8CFFu
#define UI_GOOD        0xFF3DDC97u
#define UI_WARN        0xFFF2994Au
#define UI_BAD         0xFFEB5757u
#define UI_MID         0xFFF2C94Cu
#define UI_MALE        0xFF6FA8FFu
#define UI_FEMALE      0xFFFF7AA8u

/* Sizes and spacing in points */
#define UI_SIZE_TITLE  18.0f
#define UI_SIZE_BODY   13.0f
#define UI_SIZE_SMALL  12.0f
#define UI_SIZE_LABEL  10.5f
#define UI_SIZE_PILL   10.0f
#define UI_PAD         14.0f
#define UI_GAP         8.0f
#define UI_RADIUS      6.0f

uint32_t ui_type_color(uint8_t type_id);   /* Gen 3 ids 0..17, 9 = ???; out of range gives UI_DIM */

#endif // UI_THEME_H
