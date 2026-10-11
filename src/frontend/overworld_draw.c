#include "frontend/overworld_draw.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#define TILE        16.0f   /* GBA px */
#define CONE_INSET   2.0f   /* GBA px trimmed from each side across the cone */
#define SPRITE_HEAD 16.0f   /* GBA px from the tile top to the top of a 16x32 sprite */
#define PILL_PT     UI_SIZE_PILL
#define PILL_H      16.0f   /* points */
#define PILL_PAD     7.0f
#define PILL_DOT     5.0f
#define PILL_GAP     4.0f
#define EDGE         2.0f
#define STAR_ARM     4.0f   /* GBA px */
#define STAR_LINE    1.2f
#define GLOW_R       6.0f
#define STAR_COLOR  0xFFFFF8D0u

static const int8_t k_dx[OW_DIR_COUNT] = {0, 0, -1, 1};
static const int8_t k_dy[OW_DIR_COUNT] = {1, -1, 0, 0};

static uint32_t with_alpha(uint32_t rgb, float a) {
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return (rgb & 0x00FFFFFFu) | ((uint32_t)(a * 255.0f + 0.5f) << 24);
}

void overworld_pill_text(const OwTrainer* t, char* out, size_t out_size) {
    char cls[OW_CLASS_LEN];
    if (t->beaten) {
        snprintf(out, out_size, "Beaten");
        return;
    }
    if (t->class_name[0]) {
        bool start = true;
        size_t i;
        for (i = 0; t->class_name[i] && i + 1 < sizeof(cls); i++) {
            unsigned char ch = (unsigned char)t->class_name[i];
            cls[i] = (char)(start ? toupper(ch) : tolower(ch));
            start = !isalpha(ch);
        }
        cls[i] = '\0';
    } else {
        memcpy(cls, "Trainer", 8);
    }
    snprintf(out, out_size, "%s \xC2\xB7 %u %s", cls, (unsigned)t->range, t->range == 1 ? "tile" : "tiles");
}

UiRect overworld_pill_rect(const UiCanvas* c, const OwTrainer* t, float g) {
    char text[40];
    overworld_pill_text(t, text, sizeof(text));
    float w = 2 * PILL_PAD + ui_text_width(c, UI_FONT_SEMIBOLD, PILL_PT, text) +
              (t->beaten ? 0.0f : PILL_DOT + PILL_GAP);
    float cw = (float)c->w / c->scale, ch = (float)c->h / c->scale;
    float x = ((float)t->x + TILE / 2) * g - w / 2;
    float y = ((float)t->y - SPRITE_HEAD) * g - PILL_GAP - PILL_H;
    if (y < EDGE) y = ((float)t->y + TILE) * g + PILL_GAP;   /* no room above: under the feet */
    if (x > cw - w - EDGE) x = cw - w - EDGE;
    if (x < EDGE) x = EDGE;
    if (y > ch - PILL_H - EDGE) y = ch - PILL_H - EDGE;
    if (y < EDGE) y = EDGE;
    return (UiRect){x, y, w, PILL_H};
}

static void draw_cone(UiCanvas* c, const OwTrainer* t, int d, float g) {
    float near_a = t->beaten ? 0.30f : 0.42f, far_a = t->beaten ? 0.05f : 0.06f;
    uint32_t rgb = t->beaten ? UI_MUTED : UI_BAD;
    /* int counter: clear can be 255, where a u8 would wrap and never end. */
    for (int k = 1; k <= t->clear[d]; k++) {
        /* Fade over the full range, so a blocked cone keeps the same shading. */
        float f = t->range > 1 ? (float)(k - 1) / (float)(t->range - 1) : 0.0f;
        float x = (float)t->x + k_dx[d] * TILE * (float)k, y = (float)t->y + k_dy[d] * TILE * (float)k;
        UiRect r = k_dx[d] ? (UiRect){x * g, (y + CONE_INSET) * g, TILE * g, (TILE - 2 * CONE_INSET) * g}
                           : (UiRect){(x + CONE_INSET) * g, y * g, (TILE - 2 * CONE_INSET) * g, TILE * g};
        ui_fill_rect(c, r, with_alpha(rgb, near_a + (far_a - near_a) * f));
    }
}

static void draw_pill(UiCanvas* c, const OwTrainer* t, float g) {
    char text[40];
    overworld_pill_text(t, text, sizeof(text));
    UiRect r = overworld_pill_rect(c, t, g);
    ui_fill_round_rect(c, r, PILL_H / 2, with_alpha(UI_BG, t->beaten ? 0.60f : 0.88f));
    float tx = r.x + PILL_PAD;
    if (!t->beaten) {
        ui_fill_round_rect(c, (UiRect){tx, r.y + (PILL_H - PILL_DOT) / 2, PILL_DOT, PILL_DOT}, PILL_DOT / 2,
                           UI_BAD);
        tx += PILL_DOT + PILL_GAP;
    }
    float ty = r.y + PILL_H / 2 - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, PILL_PT);
    ui_text(c, UI_FONT_SEMIBOLD, PILL_PT, tx, ty, 0, UI_ALIGN_LEFT, t->beaten ? UI_MUTED : UI_TEXT, text);
}

static void draw_hidden(UiCanvas* c, const OwMarker* p, float g) {
    float cx = ((float)p->x + TILE / 2) * g, cy = ((float)p->y + TILE / 2) * g, r = GLOW_R * g;
    ui_fill_round_rect(c, (UiRect){cx - r, cy - r, 2 * r, 2 * r}, r, with_alpha(UI_MID, 0.35f));
    ui_capsule(c, cx - STAR_ARM * g, cy, cx + STAR_ARM * g, cy, STAR_LINE * g, STAR_COLOR);
    ui_capsule(c, cx, cy - STAR_ARM * g, cx, cy + STAR_ARM * g, STAR_LINE * g, STAR_COLOR);
}

void overworld_draw(UiCanvas* c, const OverworldSnapshot* s, float g) {
    if (!s->visible) return;
    for (uint8_t i = 0; i < s->marker_count; i++)
        if (s->markers[i].kind == OW_MARK_HIDDEN) draw_hidden(c, &s->markers[i], g);
    for (uint8_t i = 0; i < s->trainer_count; i++)
        for (int d = 0; d < OW_DIR_COUNT; d++)
            if (s->trainers[i].clear[d]) draw_cone(c, &s->trainers[i], d, g);
    /* Pills last, so no cone covers a label. */
    for (uint8_t i = 0; i < s->trainer_count; i++) draw_pill(c, &s->trainers[i], g);
}
