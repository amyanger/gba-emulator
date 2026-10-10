#include "ui/ui_widgets.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

static const uint32_t TYPE_COLORS[18] = {
    0xFFA8A77Au, 0xFFC22E28u, 0xFFA98FF3u, 0xFFA33EA1u, 0xFFE2BF65u, 0xFFB6A136u,
    0xFFA6B91Au, 0xFF735797u, 0xFFB7B7CEu, 0xFF68A090u, 0xFFEE8130u, 0xFF6390F0u,
    0xFF7AC74Cu, 0xFFF7D02Cu, 0xFFF95587u, 0xFF96D9D6u, 0xFF6F35FCu, 0xFF705746u,
};

uint32_t ui_type_color(uint8_t type_id) {
    return type_id < 18 ? TYPE_COLORS[type_id] : UI_DIM;
}

/* Light backgrounds need dark text to stay readable. */
static uint32_t pill_text_color(uint32_t bg) {
    if (bg == TYPE_COLORS[13] || bg == TYPE_COLORS[15] || bg == TYPE_COLORS[8] ||
        bg == TYPE_COLORS[4])
        return 0xFF1C1C1Cu;
    return 0xFFFFFFFFu;
}

float ui_pill(UiCanvas* c, float x, float y, const char* text, uint32_t bg) {
    float tw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, text);
    float w = tw + 14.0f;
    float h = UI_SIZE_PILL + 5.0f;
    ui_fill_round_rect(c, (UiRect){x, y, w, h}, h / 2, bg);
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, x + 7.0f, y + 2.5f, 0, UI_ALIGN_LEFT,
            pill_text_color(bg), text);
    return w;
}

void ui_bar(UiCanvas* c, UiRect r, float frac, uint32_t fill) {
    ui_fill_round_rect(c, r, r.h / 2, UI_TRACK);
    if (!(frac > 0.0f)) return;
    if (frac > 1.0f) frac = 1.0f;
    float fw = r.w * frac;
    if (fw < r.h) fw = r.h;
    if (fw > r.w) fw = r.w;
    ui_fill_round_rect(c, (UiRect){r.x, r.y, fw, r.h}, r.h / 2, fill);
}

/* Capsule end points are pulled in by half the thickness so the round caps
 * stay inside the size x size box. */
void ui_gender(UiCanvas* c, float x, float y, float size, bool female) {
    float t = size * 0.12f, r = size * 0.28f, half = t / 2;
    if (!female) {
        float cx = x + size * 0.4f, cy = y + size * 0.6f;
        float tx = x + size * 0.92f - half, ty = y + size * 0.08f + half;
        float d = r * 0.7071f;
        ui_ring(c, cx, cy, r, t, UI_MALE);
        ui_capsule(c, cx + d, cy - d, tx, ty, t, UI_MALE);
        ui_capsule(c, tx, ty, tx - size * 0.3f, ty, t, UI_MALE);
        ui_capsule(c, tx, ty, tx, ty + size * 0.3f, t, UI_MALE);
    } else {
        float cx = x + size * 0.5f, cy = y + size * 0.36f;
        float bottom = y + size - half, cross = y + size * 0.75f;
        ui_ring(c, cx, cy, r, t, UI_FEMALE);
        ui_capsule(c, cx, cy + r, cx, bottom, t, UI_FEMALE);
        ui_capsule(c, cx - size * 0.2f, cross, cx + size * 0.2f, cross, t, UI_FEMALE);
    }
}

void ui_check(UiCanvas* c, float x, float y, float size, uint32_t argb) {
    ui_text(c, UI_FONT_SEMIBOLD, size, x, y, 0, UI_ALIGN_LEFT, argb, "\xE2\x9C\x93");
}

float ui_tabs(UiCanvas* c, float x, float y, float w, const char* const* labels, int count,
              int active, const char* badge, const char* hint) {
    ui_hline(c, x, y + 21.0f, w, UI_DIVIDER);
    float cx = x;
    for (int i = 0; i < count; i++) {
        bool on = i == active;
        UiFont f = on ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR;
        float lw = ui_text_width(c, f, UI_SIZE_SMALL, labels[i]);
        ui_text(c, f, UI_SIZE_SMALL, cx, y, 0, UI_ALIGN_LEFT, on ? UI_TEXT : UI_DIM, labels[i]);
        if (on) ui_fill_round_rect(c, (UiRect){cx, y + 20.0f, lw, 2.0f}, 1.0f, UI_ACCENT);
        cx += lw + 18.0f;
    }
    if (badge) {
        float bw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, badge) + 10.0f;
        float bh = UI_SIZE_PILL + 5.0f;
        ui_fill_round_rect(c, (UiRect){cx, y + 0.5f, bw, bh}, bh / 2, UI_SELECTED);
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, cx + 5.0f, y + 3.0f, 0, UI_ALIGN_LEFT,
                UI_MUTED, badge);
    }
    if (hint)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x + w, y + 1.0f, 0, UI_ALIGN_RIGHT, UI_DIM, hint);
    return y + 30.0f;
}

void ui_section_label(UiCanvas* c, float x, float y, const char* upper_text) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x, y, 0, UI_ALIGN_LEFT, UI_DIM, upper_text);
}
