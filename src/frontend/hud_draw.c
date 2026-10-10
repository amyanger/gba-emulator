#include "frontend/hud_draw.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#define HUD_INSET     10.0f
#define HUD_GAP        6.0f
#define BADGE_H       18.0f
#define BADGE_PAD_X    8.0f
#define CARD_H        34.0f
#define TOAST_RADIUS   8.0f
#define STRIPE_W       3.0f
#define PAD_L         11.0f
#define ICON          16.0f
#define ICON_GAP       9.0f
#define PAD_R         14.0f
#define DETAIL_GAP     8.0f
#define MIN_DETAIL    24.0f
#define SLIDE          8.0f
#define TITLE_PT      UI_SIZE_BODY
#define DETAIL_PT     UI_SIZE_SMALL
#define BADGE_PT      UI_SIZE_LABEL
#define ICON_PT       11.0f

static const char* const k_badge_label[BADGE_COUNT] = {"PAUSED", "FAST", "REWIND", "MUTED"};
static const uint32_t k_badge_color[BADGE_COUNT] = {UI_MID, UI_ACCENT, UI_ACCENT, UI_MUTED};
static const uint32_t k_kind_color[] = {UI_GOOD, UI_ACCENT, UI_WARN, UI_BAD};
static const char* const k_kind_glyph[] = {"\xE2\x9C\x93", "i", "!", "!"};

#define BADGE_BG ((UI_BG & 0x00FFFFFFu) | 0xD1000000u)      /* 82% */
#define CARD_BG  ((UI_SURFACE & 0x00FFFFFFu) | 0xF0000000u) /* 94% */

static uint32_t fade(uint32_t argb, float a) {
    uint32_t al = (uint32_t)((float)(argb >> 24) * a + 0.5f);
    return (argb & 0x00FFFFFFu) | (al << 24);
}

float hud_strip_height_pt(void) {
    return HUD_INSET + BADGE_H + HUD_GAP + TOAST_CAP * CARD_H + (TOAST_CAP - 1) * HUD_GAP +
           HUD_INSET;
}

bool hud_visible(const ToastQueue* q) {
    if (q->count > 0) return true;
    for (int i = 0; i < BADGE_COUNT; i++)
        if (q->badges[i]) return true;
    return false;
}

void hud_layout(const UiCanvas* c, const ToastQueue* q, uint32_t now, float area_w, HudLayout* out) {
    float x = HUD_INSET, badges_right = 0.0f;
    bool any_badge = false;
    for (int i = 0; i < BADGE_COUNT; i++) {
        out->badges[i] = (HudBox){0, 0, 0, 0};
        if (!q->badges[i]) continue;
        float w = ui_text_width(c, UI_FONT_SEMIBOLD, BADGE_PT, k_badge_label[i]) + 2 * BADGE_PAD_X;
        out->badges[i] = (HudBox){x, HUD_INSET, w, BADGE_H};
        badges_right = x + w;
        x += w + HUD_GAP;
        any_badge = true;
    }

    /* Titles keep priority; the detail shrinks, then disappears when too narrow. */
    const float fixed = STRIPE_W + PAD_L + ICON + ICON_GAP + PAD_R;
    float avail = area_w - 2 * HUD_INSET - fixed;
    if (avail < 0.0f) avail = 0.0f;
    /* Fit as many of the newest toasts as the canvas height allows; older ones
     * give way so the toast just triggered is always on screen. */
    float area_h = (float)c->h / c->scale;
    float base_hi = HUD_INSET + BADGE_H + HUD_GAP;
    uint32_t fit = (uint32_t)((area_h - base_hi - HUD_INSET + HUD_GAP) / (CARD_H + HUD_GAP));
    if (fit < 1) fit = 1;
    uint32_t first = q->count > fit ? q->count - fit : 0;

    bool overlap = false;
    for (uint32_t i = 0; i < q->count; i++) {
        if (i < first) {
            out->toasts[i] = (HudBox){0, 0, 0, 0};
            out->title_w[i] = out->detail_w[i] = 0.0f;
            continue;
        }
        const Toast* t = &q->items[i];
        float tw = ui_text_width(c, UI_FONT_REGULAR, TITLE_PT, t->title);
        float dw = t->detail[0] ? ui_text_width(c, UI_FONT_REGULAR, DETAIL_PT, t->detail) : 0.0f;
        float title_w = tw < avail ? tw : avail;
        float rest = avail - title_w - DETAIL_GAP;
        float detail_w = (dw > 0.0f && rest >= MIN_DETAIL) ? (dw < rest ? dw : rest) : 0.0f;
        float w = fixed + title_w + (detail_w > 0.0f ? DETAIL_GAP + detail_w : 0.0f);
        out->title_w[i] = title_w;
        out->detail_w[i] = detail_w;
        out->toasts[i] = (HudBox){(area_w - w) / 2, 0, w, CARD_H};
        if (any_badge && out->toasts[i].x < badges_right + HUD_GAP) overlap = true;
    }

    out->below_badges = overlap;
    float y = overlap ? HUD_INSET + BADGE_H + HUD_GAP : HUD_INSET;
    for (uint32_t i = first; i < q->count; i++) {
        out->toasts[i].y = y - SLIDE * (1.0f - toast_slide(&q->items[i], now));
        y += CARD_H + HUD_GAP;
    }
}

void hud_draw(UiCanvas* c, const ToastQueue* q, uint32_t now) {
    HudLayout l;
    c->keep_alpha = true;
    ui_canvas_unclip(c);
    ui_canvas_clear(c, 0x00000000u);
    hud_layout(c, q, now, (float)c->w / c->scale, &l);

    for (int i = 0; i < BADGE_COUNT; i++) {
        HudBox b = l.badges[i];
        if (b.w <= 0.0f) continue;
        ui_fill_round_rect(c, (UiRect){b.x, b.y, b.w, b.h}, b.h / 2, BADGE_BG);
        float ty = b.y + b.h / 2 - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, BADGE_PT);
        ui_text(c, UI_FONT_SEMIBOLD, BADGE_PT, b.x + BADGE_PAD_X, ty, 0, UI_ALIGN_LEFT,
                k_badge_color[i], k_badge_label[i]);
    }

    for (uint32_t i = 0; i < q->count; i++) {
        const Toast* t = &q->items[i];
        float a = toast_alpha(t, now);
        if (a <= 0.0f) continue;
        HudBox b = l.toasts[i];
        if (b.w <= 0.0f) continue;
        UiRect card = {b.x, b.y, b.w, b.h};
        uint32_t kind = k_kind_color[t->kind];
        ui_fill_round_rect(c, card, TOAST_RADIUS, fade(CARD_BG, a));
        /* Left stripe: the card shape in the kind color, clipped to the stripe. */
        ui_canvas_clip(c, (UiRect){b.x, b.y, STRIPE_W, b.h});
        ui_fill_round_rect(c, card, TOAST_RADIUS, fade(kind, a));
        ui_canvas_unclip(c);

        float ix = b.x + STRIPE_W + PAD_L, iy = b.y + (b.h - ICON) / 2;
        ui_fill_round_rect(c, (UiRect){ix, iy, ICON, ICON}, ICON / 2, fade(kind, a));
        ui_text(c, UI_FONT_SEMIBOLD, ICON_PT, ix + ICON / 2,
                iy + ICON / 2 - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, ICON_PT), 0,
                UI_ALIGN_CENTER, fade(UI_BG, a), k_kind_glyph[t->kind]);

        float tx = ix + ICON + ICON_GAP;
        float ty = b.y + b.h / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, TITLE_PT);
        ui_text(c, UI_FONT_REGULAR, TITLE_PT, tx, ty, l.title_w[i] + 0.01f, UI_ALIGN_LEFT,
                fade(UI_TEXT, a), t->title);
        if (l.detail_w[i] > 0.0f) {
            float dy = b.y + b.h / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, DETAIL_PT);
            ui_text(c, UI_FONT_REGULAR, DETAIL_PT, tx + l.title_w[i] + DETAIL_GAP, dy,
                    l.detail_w[i] + 0.01f, UI_ALIGN_LEFT, fade(UI_MUTED, a), t->detail);
        }
    }
}
