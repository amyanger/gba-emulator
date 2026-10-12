#include "frontend/overworld_draw.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "frontend/game_panel.h"

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
#define ITEM_PT      9.0f
#define MIN_TEXT_PX 10.0f   /* CLAUDE.md: text never below 10 px */
#define ITEM_H      14.0f   /* points */
#define ITEM_PAD     6.0f
#define ITEM_BG     0x3A2F08u
#define ITEM_TEXT   0xFFFFE9A8u
#define GIFT_BG     0x0F3D2Au
#define GIFT_TEXT   0xFFB6F5D6u
#define FEEBAS_RGB  0x2EC4E8u
#define FEEBAS_BG   0x0B3442u
#define FEEBAS_TEXT 0xFFA8ECFFu
#define FEEBAS_LINE  1.5f   /* GBA px */
#define CHEVRON_W    8.0f   /* points */
#define STACK_ROWS   3
#define MAX_CONES   (OW_MAX_TRAINERS * OW_DIR_COUNT)
#define MAX_AVOID   (MAX_CONES + OW_MAX_MARKERS)
#define LABEL_TEXT  48

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

static void draw_hidden(UiCanvas* c, const OwMarker* p, float g) {
    float cx = ((float)p->x + TILE / 2) * g, cy = ((float)p->y + TILE / 2) * g, r = GLOW_R * g;
    ui_fill_round_rect(c, (UiRect){cx - r, cy - r, 2 * r, 2 * r}, r, with_alpha(UI_MID, 0.35f));
    ui_capsule(c, cx - STAR_ARM * g, cy, cx + STAR_ARM * g, cy, STAR_LINE * g, STAR_COLOR);
    ui_capsule(c, cx, cy - STAR_ARM * g, cx, cy + STAR_ARM * g, STAR_LINE * g, STAR_COLOR);
}

static bool is_feebas(const OwMarker* mk) {
    return mk->kind == OW_MARK_FEEBAS || mk->kind == OW_MARK_FEEBAS_BLOCKED;
}

/* Bridge, blocked or plain water: one label per spot and kind. */
static int feebas_key(const OwMarker* mk) {
    return mk->kind == OW_MARK_FEEBAS_BLOCKED ? 2 : mk->bridge ? 1 : 0;
}

static void draw_feebas_tile(UiCanvas* c, const OwMarker* mk, float g) {
    bool blocked = mk->kind == OW_MARK_FEEBAS_BLOCKED;
    uint32_t rgb = blocked ? UI_MUTED : FEEBAS_RGB;
    float x0 = ((float)mk->x + 1) * g, y0 = ((float)mk->y + 1) * g, s = (TILE - 2) * g, t = FEEBAS_LINE * g;
    ui_fill_round_rect(c, (UiRect){x0, y0, s, s}, 3.0f * g, with_alpha(rgb, blocked ? 0.18f : 0.30f));
    uint32_t line = with_alpha(rgb, blocked ? 0.55f : 0.85f);
    float a = x0 + t / 2, b = y0 + t / 2, e = x0 + s - t / 2, f = y0 + s - t / 2;
    ui_capsule(c, a, b, e, b, t, line);
    ui_capsule(c, a, f, e, f, t, line);
    ui_capsule(c, a, b, a, f, t, line);
    ui_capsule(c, e, b, e, f, t, line);
}

void overworld_marker_text(const OwMarker* mk, char* out, size_t out_size) {
    char a[OW_ITEM_NAME_LEN * 2], b[OW_ITEM_NAME_LEN * 2];
    game_panel_title_case(mk->name[0], a, sizeof(a));
    if (is_feebas(mk)) {
        const char* note = mk->kind == OW_MARK_FEEBAS_BLOCKED ? " \xC2\xB7 can't fish" : mk->bridge ? " \xC2\xB7 bridge" : "";
        snprintf(out, out_size, "%s%s", a, note);
        return;
    }
    if (mk->kind != OW_MARK_GIFT) {
        snprintf(out, out_size, "%s", a);
        return;
    }
    if (mk->name_count > 1) {
        game_panel_title_case(mk->name[1], b, sizeof(b));
        snprintf(out, out_size, "Gives %s / %s", a, b);
    } else {
        snprintf(out, out_size, "Gives %s", a);
    }
}

typedef struct { float pt, h, pad; bool dot; } PillStyle;

static PillStyle label_style(const UiCanvas* c, const OverworldSnapshot* s, const OwLabel* l) {
    if (l->source == OW_LABEL_TRAINER) return (PillStyle){PILL_PT, PILL_H, PILL_PAD, !s->trainers[l->index].beaten};
    if (l->source == OW_LABEL_HINT || s->markers[l->index].kind == OW_MARK_GIFT || is_feebas(&s->markers[l->index]))
        return (PillStyle){PILL_PT, PILL_H, PILL_PAD, false};
    /* Smaller than the other pills, but never below 10 px on low-density screens. */
    float pt = ITEM_PT * c->scale < MIN_TEXT_PX ? MIN_TEXT_PX / c->scale : ITEM_PT;
    return (PillStyle){pt, ITEM_H, ITEM_PAD, false};
}

static void label_text(const OverworldSnapshot* s, const OwLabel* l, char* out, size_t n) {
    if (l->source == OW_LABEL_HINT) game_panel_title_case(s->hint.name, out, n);
    else if (l->source == OW_LABEL_TRAINER) overworld_pill_text(&s->trainers[l->index], out, n);
    else overworld_marker_text(&s->markers[l->index], out, n);
}

/* The object a label belongs to, in points: people are a 16x32 sprite standing on
 * their tile, items just the tile. */
static UiRect label_anchor(const OverworldSnapshot* s, const OwLabel* l, float g) {
    int16_t x, y;
    bool person = l->source == OW_LABEL_TRAINER || s->markers[l->index].kind == OW_MARK_GIFT;
    if (l->source == OW_LABEL_TRAINER) {
        x = s->trainers[l->index].x;
        y = s->trainers[l->index].y;
    } else {
        x = s->markers[l->index].x;
        y = s->markers[l->index].y;
    }
    float top = person ? (float)y - SPRITE_HEAD : (float)y;
    return (UiRect){(float)x * g, top * g, TILE * g, (person ? 2 * TILE : TILE) * g};
}

static bool overlaps(UiRect a, UiRect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static UiRect clamp_rect(const UiCanvas* c, UiRect r) {
    float cw = (float)c->w / c->scale, ch = (float)c->h / c->scale;
    if (r.x > cw - r.w - EDGE) r.x = cw - r.w - EDGE;
    if (r.x < EDGE) r.x = EDGE;
    if (r.y > ch - r.h - EDGE) r.y = ch - r.h - EDGE;
    if (r.y < EDGE) r.y = EDGE;
    return r;
}

/* Every cone as one rect per direction, in points. */
static uint32_t cone_rects(const OverworldSnapshot* s, float g, UiRect* out) {
    uint32_t n = 0;
    for (uint8_t i = 0; i < s->trainer_count; i++) {
        const OwTrainer* t = &s->trainers[i];
        for (int d = 0; d < OW_DIR_COUNT; d++) {
            float len = TILE * (float)t->clear[d];
            if (len <= 0.0f) continue;
            float x = (float)t->x, y = (float)t->y;
            UiRect r = d == OW_DIR_DOWN ? (UiRect){x, y + TILE, TILE, len}
                     : d == OW_DIR_UP   ? (UiRect){x, y - len, TILE, len}
                     : d == OW_DIR_LEFT ? (UiRect){x - len, y, len, TILE}
                                        : (UiRect){x + TILE, y, len, TILE};
            out[n++] = (UiRect){r.x * g, r.y * g, r.w * g, r.h * g};
        }
    }
    return n;
}

/* On-screen Feebas tiles, in points, so other labels keep off them. */
static uint32_t feebas_rects(const OverworldSnapshot* s, float g, UiRect* out) {
    uint32_t n = 0;
    for (uint8_t i = 0; i < s->marker_count; i++)
        if (is_feebas(&s->markers[i]))
            out[n++] = (UiRect){(float)s->markers[i].x * g, (float)s->markers[i].y * g, TILE * g, TILE * g};
    return n;
}

static float label_width(const UiCanvas* c, const OverworldSnapshot* s, const OwLabel* l, PillStyle st) {
    char text[LABEL_TEXT];
    label_text(s, l, text, sizeof(text));
    float w = 2 * st.pad + ui_text_width(c, UI_FONT_SEMIBOLD, st.pt, text) + (st.dot ? PILL_DOT + PILL_GAP : 0.0f);
    return l->source == OW_LABEL_HINT ? w + PILL_GAP + CHEVRON_W : w;
}

/* Player and target tile centres in points; the hint goes where that ray leaves the screen. */
static UiRect hint_rect(const UiCanvas* c, const OwFeebasHint* h, float g, float w, float hh) {
    float cw = (float)c->w / c->scale, ch = (float)c->h / c->scale;
    float px = ((float)h->px + TILE / 2) * g, py = ((float)h->py + TILE / 2) * g;
    float dx = ((float)h->tx + TILE / 2) * g - px, dy = ((float)h->ty + TILE / 2) * g - py;
    float mx = w / 2 + EDGE, my = hh / 2 + EDGE, t = 1.0f;
    if (dx > 0.0f) t = fminf(t, (cw - mx - px) / dx);
    if (dx < 0.0f) t = fminf(t, (mx - px) / dx);
    if (dy > 0.0f) t = fminf(t, (ch - my - py) / dy);
    if (dy < 0.0f) t = fminf(t, (my - py) / dy);
    if (t < 0.0f) t = 0.0f;
    return clamp_rect(c, (UiRect){px + dx * t - w / 2, py + dy * t - hh / 2, w, hh});
}

static bool blocked(UiRect r, const OwLabel* placed, uint32_t n, const UiRect* cones, uint32_t nc) {
    for (uint32_t i = 0; i < n; i++)
        if (overlaps(r, placed[i].rect)) return true;
    for (uint32_t i = 0; i < nc; i++)
        if (overlaps(r, cones[i])) return true;
    return false;
}

/* Above, left, right, below; then stacked above; then above anyway. */
static UiRect place(const UiCanvas* c, UiRect anchor, float w, float h, const OwLabel* placed, uint32_t n,
                    const UiRect* cones, uint32_t nc) {
    float cx = anchor.x + anchor.w / 2, cy = anchor.y + anchor.h / 2;
    const UiRect cand[4] = {
        {cx - w / 2, anchor.y - PILL_GAP - h, w, h},
        {anchor.x - PILL_GAP - w, cy - h / 2, w, h},
        {anchor.x + anchor.w + PILL_GAP, cy - h / 2, w, h},
        {cx - w / 2, anchor.y + anchor.h + PILL_GAP, w, h},
    };
    for (int i = 0; i < 4; i++) {
        UiRect r = clamp_rect(c, cand[i]);
        if (!blocked(r, placed, n, cones, nc)) return r;
    }
    UiRect up = cand[0];
    for (int k = 0; k < STACK_ROWS; k++) {
        up.y -= h + 2.0f;
        UiRect r = clamp_rect(c, up);
        if (!blocked(r, placed, n, cones, nc)) return r;
    }
    return clamp_rect(c, cand[0]);
}

static bool feebas_label_taken(const OverworldSnapshot* s, uint32_t i) {
    for (uint32_t j = 0; j < i; j++)
        if (is_feebas(&s->markers[j]) && s->markers[j].spot == s->markers[i].spot &&
            feebas_key(&s->markers[j]) == feebas_key(&s->markers[i]))
            return true;
    return false;
}

uint32_t overworld_label_rects(const UiCanvas* c, const OverworldSnapshot* s, float g, OwLabel* out) {
    UiRect avoid[MAX_AVOID];
    uint32_t na = cone_rects(s, g, avoid), n = 0;
    na += feebas_rects(s, g, avoid + na);
    if (s->feebas_visible && s->hint.show) {
        OwLabel l = {OW_LABEL_HINT, 0, {0, 0, 0, 0}};
        PillStyle st = label_style(c, s, &l);
        l.rect = hint_rect(c, &s->hint, g, label_width(c, s, &l, st), st.h);
        out[n++] = l;
    }
    /* Priority order: who can see you, Feebas, gifts, then items. */
    for (int pass = 0; pass < 4; pass++) {
        uint32_t count = pass == 0 ? s->trainer_count : s->marker_count;
        for (uint32_t i = 0; i < count && n < OW_MAX_LABELS; i++) {
            if (pass > 0) {
                const OwMarker* mk = &s->markers[i];
                int kind = is_feebas(mk) ? 1 : mk->kind == OW_MARK_GIFT ? 2 : 3;
                if (kind != pass || (kind == 1 && feebas_label_taken(s, i))) continue;
            }
            OwLabel l = {pass == 0 ? OW_LABEL_TRAINER : OW_LABEL_MARKER, (uint8_t)i, {0, 0, 0, 0}};
            PillStyle st = label_style(c, s, &l);
            l.rect = place(c, label_anchor(s, &l, g), label_width(c, s, &l, st), st.h, out, n, avoid, na);
            out[n++] = l;
        }
    }
    return n;
}

/* An arrowhead pointing from the player toward the hint's target. */
static void draw_chevron(UiCanvas* c, const OwFeebasHint* h, float cx, float cy, uint32_t argb) {
    float dx = (float)(h->tx - h->px), dy = (float)(h->ty - h->py), len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    float ux = dx / len, uy = dy / len, tipx = cx + ux * 3.0f, tipy = cy + uy * 3.0f;
    float bx = cx - ux * 2.0f, by = cy - uy * 2.0f;
    ui_capsule(c, tipx, tipy, bx - uy * 3.0f, by + ux * 3.0f, 1.5f, argb);
    ui_capsule(c, tipx, tipy, bx + uy * 3.0f, by - ux * 3.0f, 1.5f, argb);
}

static void draw_label(UiCanvas* c, const OverworldSnapshot* s, const OwLabel* l) {
    char text[LABEL_TEXT];
    PillStyle st = label_style(c, s, l);
    uint32_t bg, fg;
    label_text(s, l, text, sizeof(text));
    if (l->source == OW_LABEL_HINT) {
        bg = with_alpha(FEEBAS_BG, 0.92f);
        fg = FEEBAS_TEXT;
    } else if (l->source == OW_LABEL_TRAINER) {
        bool beaten = s->trainers[l->index].beaten;
        bg = with_alpha(UI_BG, beaten ? 0.60f : 0.88f);
        fg = beaten ? UI_MUTED : UI_TEXT;
    } else if (s->markers[l->index].kind == OW_MARK_FEEBAS) {
        bg = with_alpha(FEEBAS_BG, 0.90f);
        fg = FEEBAS_TEXT;
    } else if (s->markers[l->index].kind == OW_MARK_FEEBAS_BLOCKED) {
        bg = with_alpha(UI_BG, 0.60f);
        fg = UI_MUTED;
    } else if (s->markers[l->index].kind == OW_MARK_GIFT) {
        bg = with_alpha(GIFT_BG, 0.90f);
        fg = GIFT_TEXT;
    } else {
        bg = with_alpha(ITEM_BG, 0.80f);
        fg = ITEM_TEXT;
    }
    UiRect r = l->rect;
    ui_fill_round_rect(c, r, st.h / 2, bg);
    float tx = r.x + st.pad;
    if (st.dot) {
        ui_fill_round_rect(c, (UiRect){tx, r.y + (st.h - PILL_DOT) / 2, PILL_DOT, PILL_DOT}, PILL_DOT / 2, UI_BAD);
        tx += PILL_DOT + PILL_GAP;
    }
    float ty = r.y + st.h / 2 - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, st.pt);
    ui_text(c, UI_FONT_SEMIBOLD, st.pt, tx, ty, 0, UI_ALIGN_LEFT, fg, text);
    if (l->source == OW_LABEL_HINT)
        draw_chevron(c, &s->hint, r.x + r.w - st.pad - CHEVRON_W / 2, r.y + st.h / 2, fg);
}

void overworld_draw(UiCanvas* c, const OverworldSnapshot* s, float g) {
    OwLabel labels[OW_MAX_LABELS];
    if (!s->visible && !s->feebas_visible) return;
    /* Feebas tiles first, under everything else. While fishing only they are in the snapshot. */
    for (uint8_t i = 0; i < s->marker_count; i++)
        if (is_feebas(&s->markers[i])) draw_feebas_tile(c, &s->markers[i], g);
    for (uint8_t i = 0; i < s->marker_count; i++)
        if (s->markers[i].kind == OW_MARK_HIDDEN) draw_hidden(c, &s->markers[i], g);
    for (uint8_t i = 0; i < s->trainer_count; i++)
        for (int d = 0; d < OW_DIR_COUNT; d++)
            if (s->trainers[i].clear[d]) draw_cone(c, &s->trainers[i], d, g);
    /* Labels last, so no cone covers one. None while fishing: the message box is up. */
    if (!s->visible) return;
    uint32_t n = overworld_label_rects(c, s, g, labels);
    for (uint32_t i = 0; i < n; i++) draw_label(c, s, &labels[i]);
}
