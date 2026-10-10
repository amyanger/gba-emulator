#include "ui/ui_controls.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"

#include <string.h>

#define CTRL_RADIUS   6.0f
#define FOCUS_RING    1.5f
#define TEXT_PT       12.0f
#define LINE_H        16.0f
#define TEXT_PAD      8.0f
#define WHEEL_STEP    24.0f

static bool inside(UiRect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

static bool over(const UiInput* in, UiRect r) { return !in->blocked && inside(r, in->mx, in->my); }

static void join_focus(UiInput* in, uint32_t id) {
    if (in->order_count < UI_FOCUS_MAX) in->order[in->order_count++] = id;
}

/* Press inside makes the control active and focused; release inside while
 * active is a click. */
static bool track_click(UiInput* in, uint32_t id, UiRect r) {
    bool o = over(in, r);
    if (o) in->hot = id;
    if (o && in->pressed) {
        in->active = id;
        in->focus = id;
        in->caret_ms = in->now_ms;
    }
    return o && in->released && in->active == id;
}

static bool key_activate(const UiInput* in, uint32_t id) {
    return !in->blocked && in->focus == id && (ui_input_has_key(in, UI_KEY_ENTER) || ui_input_has_key(in, UI_KEY_SPACE));
}

static void focus_ring(UiCanvas* c, UiRect r, float radius) {
    UiRect o = {r.x - FOCUS_RING, r.y - FOCUS_RING, r.w + 2 * FOCUS_RING, r.h + 2 * FOCUS_RING};
    ui_fill_round_rect(c, o, radius + FOCUS_RING, UI_ACCENT);
}

static void centered_text(UiCanvas* c, UiFont f, float pt, UiRect r, uint32_t color, const char* s) {
    float y = r.y + r.h / 2 - ui_text_cap_middle(c, f, pt);
    ui_text(c, f, pt, r.x + r.w / 2, y, r.w - 8.0f, UI_ALIGN_CENTER, color, s);
}

bool ui_button(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, const char* label, bool enabled, bool primary) {
    bool clicked = false;
    if (enabled) {
        join_focus(in, id);
        clicked = track_click(in, id, r) || key_activate(in, id);
    }
    if (enabled && in->focus == id) focus_ring(c, r, CTRL_RADIUS);
    uint32_t bg = !enabled ? UI_SURFACE : primary ? UI_ACCENT : UI_SELECTED;
    if (enabled && in->hot == id && in->down && in->active == id) bg = primary ? 0xFF4A78E0u : UI_DIVIDER;
    ui_fill_round_rect(c, r, CTRL_RADIUS, bg);
    uint32_t fg = !enabled ? UI_FAINT : primary ? 0xFFFFFFFFu : UI_TEXT;
    centered_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, r, fg, label);
    return clicked;
}

bool ui_toggle(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, bool* on, bool enabled) {
    bool flipped = false;
    if (enabled) {
        join_focus(in, id);
        if (track_click(in, id, r) || key_activate(in, id)) {
            *on = !*on;
            flipped = true;
        }
    }
    const float w = 28.0f, h = 16.0f;
    UiRect track = {r.x, r.y + (r.h - h) / 2, w, h};
    if (enabled && in->focus == id) focus_ring(c, track, h / 2);
    uint32_t bg = !enabled ? UI_DIVIDER : *on ? UI_GOOD : UI_TRACK;
    ui_fill_round_rect(c, track, h / 2, bg);
    float k = h - 4.0f;
    float kx = *on ? track.x + w - 2.0f - k : track.x + 2.0f;
    ui_fill_round_rect(c, (UiRect){kx, track.y + 2.0f, k, k}, k / 2, *on ? UI_BG : (enabled ? UI_DIM : UI_FAINT));
    return flipped;
}

bool ui_sidebar_item(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, const char* label, bool selected) {
    join_focus(in, id);
    bool clicked = track_click(in, id, r) || key_activate(in, id);
    if (in->focus == id && !selected) focus_ring(c, r, CTRL_RADIUS);
    if (selected || in->focus == id || in->hot == id)
        ui_fill_round_rect(c, r, CTRL_RADIUS, selected ? UI_SELECTED : UI_SURFACE);
    float y = r.y + r.h / 2 - ui_text_cap_middle(c, selected ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, UI_SIZE_BODY);
    ui_text(c, selected ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, UI_SIZE_BODY, r.x + 10.0f, y, r.w - 14.0f,
            UI_ALIGN_LEFT, selected ? UI_TEXT : UI_MUTED, label);
    return clicked;
}

bool ui_chip(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, const char* label, uint32_t color) {
    join_focus(in, id);
    bool clicked = track_click(in, id, r) || key_activate(in, id);
    if (in->focus == id) focus_ring(c, r, r.h / 2);
    ui_fill_round_rect(c, r, r.h / 2, in->hot == id ? UI_DIVIDER : UI_SELECTED);
    centered_text(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, r, color, label);
    return clicked;
}

/* ---- Text area ---- */

static bool is_cont(uint8_t b) { return (b & 0xC0u) == 0x80u; }

static uint32_t prev_char(const UiTextArea* t, uint32_t i) {
    if (i == 0) return 0;
    i--;
    while (i > 0 && is_cont((uint8_t)t->buf[i])) i--;
    return i;
}

static uint32_t next_char(const UiTextArea* t, uint32_t i) {
    if (i >= t->len) return t->len;
    i++;
    while (i < t->len && is_cont((uint8_t)t->buf[i])) i++;
    return i;
}

static uint32_t line_start(const UiTextArea* t, uint32_t i) {
    while (i > 0 && t->buf[i - 1] != '\n') i--;
    return i;
}

static uint32_t line_end(const UiTextArea* t, uint32_t i) {
    while (i < t->len && t->buf[i] != '\n') i++;
    return i;
}

static uint32_t chars_between(const UiTextArea* t, uint32_t a, uint32_t b) {
    uint32_t n = 0;
    while (a < b) { a = next_char(t, a); n++; }
    return n;
}

static uint32_t advance_chars(const UiTextArea* t, uint32_t i, uint32_t n, uint32_t limit) {
    while (n-- > 0 && i < limit) i = next_char(t, i);
    return i;
}

void ui_text_area_init(UiTextArea* t, char* buf, uint32_t cap) {
    memset(t, 0, sizeof(*t));
    t->buf = buf;
    t->cap = cap;
    if (cap) buf[0] = '\0';
}

void ui_text_area_set(UiTextArea* t, const char* text) {
    size_t n = strlen(text);
    if (n >= t->cap) n = t->cap - 1;
    while (n > 0 && is_cont((uint8_t)text[n])) n--;   /* never cut a character */
    memcpy(t->buf, text, n);
    t->buf[n] = '\0';
    t->len = t->caret = (uint32_t)n;
    t->all = false;
}

static void delete_range(UiTextArea* t, uint32_t a, uint32_t b) {
    memmove(t->buf + a, t->buf + b, t->len - b + 1);
    t->len -= b - a;
    t->caret = a;
}

/* Inserts UTF-8 at the caret, normalizing CR/CRLF to LF, whole characters only. */
static bool insert_text(UiTextArea* t, const char* s, uint32_t n, bool multiline) {
    bool changed = false;
    if (t->all && n > 0) {
        t->len = t->caret = 0;
        t->buf[0] = '\0';
        t->all = false;
        changed = true;
    }
    for (uint32_t i = 0; i < n;) {
        char ch = s[i];
        uint32_t w = 1;
        if (ch == '\r') {
            ch = '\n';
            if (i + 1 < n && s[i + 1] == '\n') i++;
        }
        if (ch == '\n' && !multiline) ch = ' ';
        uint8_t lead = (uint8_t)ch;
        if (lead >= 0xF0u) w = 4;
        else if (lead >= 0xE0u) w = 3;
        else if (lead >= 0xC0u) w = 2;
        if (i + w > n || t->len + w >= t->cap) break;
        memmove(t->buf + t->caret + w, t->buf + t->caret, t->len - t->caret + 1);
        t->buf[t->caret] = ch;
        for (uint32_t k = 1; k < w; k++) t->buf[t->caret + k] = s[i + k];
        t->caret += w;
        t->len += w;
        i += w;
        changed = true;
    }
    return changed;
}

static bool apply_key(UiTextArea* t, UiKey k) {
    switch (k) {
    case UI_KEY_SELECT_ALL: t->all = t->len > 0; t->caret = t->len; return false;
    case UI_KEY_BACKSPACE:
    case UI_KEY_DELETE:
        if (t->all) { t->len = t->caret = 0; t->buf[0] = '\0'; t->all = false; return true; }
        if (k == UI_KEY_BACKSPACE && t->caret > 0) { delete_range(t, prev_char(t, t->caret), t->caret); return true; }
        if (k == UI_KEY_DELETE && t->caret < t->len) { delete_range(t, t->caret, next_char(t, t->caret)); return true; }
        return false;
    default: break;
    }
    t->all = false;
    switch (k) {
    case UI_KEY_LEFT:  t->caret = prev_char(t, t->caret); break;
    case UI_KEY_RIGHT: t->caret = next_char(t, t->caret); break;
    case UI_KEY_HOME:  t->caret = line_start(t, t->caret); break;
    case UI_KEY_END:   t->caret = line_end(t, t->caret); break;
    case UI_KEY_UP:
    case UI_KEY_DOWN: {
        uint32_t ls = line_start(t, t->caret);
        uint32_t col = chars_between(t, ls, t->caret);
        if (k == UI_KEY_UP) {
            if (ls == 0) { t->caret = 0; break; }
            uint32_t ps = line_start(t, ls - 1);
            t->caret = advance_chars(t, ps, col, ls - 1);
        } else {
            uint32_t le = line_end(t, t->caret);
            if (le >= t->len) { t->caret = t->len; break; }
            t->caret = advance_chars(t, le + 1, col, line_end(t, le + 1));
        }
        break;
    }
    default: break;
    }
    return false;
}

bool ui_text_area(UiCanvas* c, UiInput* in, uint32_t id, UiRect r, UiTextArea* t, const char* placeholder,
                  bool multiline) {
    join_focus(in, id);
    if (track_click(in, id, r)) {
        /* Caret to the end of the clicked line. */
        uint32_t line = (uint32_t)((in->my - r.y - TEXT_PAD + t->scroll) / LINE_H);
        uint32_t i = 0;
        for (uint32_t k = 0; k < line && i < t->len; k++) i = line_end(t, i) + (line_end(t, i) < t->len ? 1 : 0);
        t->caret = line_end(t, i);
        t->all = false;
    }
    bool focused = in->focus == id;
    bool changed = false;
    if (focused && !in->blocked) {   /* keys belong to an open pop-up */
        for (uint32_t i = 0; i < in->key_count; i++) {
            UiKey k = in->keys[i];
            if (k == UI_KEY_SPACE) continue;   /* arrives as text */
            if (k == UI_KEY_ENTER) {
                if (multiline) changed |= insert_text(t, "\n", 1, true);
                continue;
            }
            changed |= apply_key(t, k);
        }
        if (in->text_len > 0) changed |= insert_text(t, in->text, in->text_len, multiline);
        if (changed || in->key_count > 0) in->caret_ms = in->now_ms;
    }

    /* Count lines and keep the caret's line in view. */
    uint32_t lines = 1, caret_line = 0;
    for (uint32_t i = 0; i < t->len; i++) {
        if (t->buf[i] == '\n') {
            if (i < t->caret) caret_line++;
            lines++;
        }
    }
    float view_h = r.h - 2 * TEXT_PAD;
    float content_h = (float)lines * LINE_H;
    if (focused && (changed || in->key_count > 0)) {
        float cy = (float)caret_line * LINE_H;
        if (cy < t->scroll) t->scroll = cy;
        if (cy + LINE_H > t->scroll + view_h) t->scroll = cy + LINE_H - view_h;
    }
    ui_scroll(in, r, content_h + 2 * TEXT_PAD, &t->scroll);

    if (focused) focus_ring(c, r, CTRL_RADIUS);
    ui_fill_round_rect(c, r, CTRL_RADIUS, UI_SURFACE);
    ui_canvas_clip(c, (UiRect){r.x + 2, r.y + 2, r.w - 4, r.h - 4});
    float x0 = r.x + TEXT_PAD, y0 = r.y + TEXT_PAD - t->scroll;
    if (t->len == 0 && placeholder) ui_text(c, UI_FONT_REGULAR, TEXT_PT, x0, y0, 0, UI_ALIGN_LEFT, UI_FAINT, placeholder);

    char line_buf[512];
    uint32_t start = 0, ln = 0;
    while (start <= t->len) {
        uint32_t end = line_end(t, start);
        uint32_t n = end - start < sizeof(line_buf) - 1 ? end - start : (uint32_t)sizeof(line_buf) - 1;
        memcpy(line_buf, t->buf + start, n);
        line_buf[n] = '\0';
        float y = y0 + (float)ln * LINE_H;
        if (t->all && n > 0) {
            float w = ui_text_width(c, UI_FONT_REGULAR, TEXT_PT, line_buf);
            ui_fill_rect(c, (UiRect){x0, y, w, LINE_H}, 0x665B8CFFu);
        }
        if (n > 0) ui_text(c, UI_FONT_REGULAR, TEXT_PT, x0, y, 0, UI_ALIGN_LEFT, UI_TEXT, line_buf);
        if (focused && t->caret >= start && t->caret <= end) {
            bool visible = ((in->now_ms - in->caret_ms) / UI_CARET_MS) % 2u == 0;
            if (visible) {
                uint32_t pn = t->caret - start < n ? t->caret - start : n;
                char save = line_buf[pn];
                line_buf[pn] = '\0';
                float cx = x0 + ui_text_width(c, UI_FONT_REGULAR, TEXT_PT, line_buf);
                line_buf[pn] = save;
                ui_fill_rect(c, (UiRect){cx, y + 1.0f, 1.0f, LINE_H - 2.0f}, UI_TEXT);
            }
        }
        if (end >= t->len) break;
        start = end + 1;
        ln++;
    }
    ui_canvas_unclip(c);
    return changed;
}

void ui_scroll(UiInput* in, UiRect r, float content_h, float* scroll) {
    float max = content_h - r.h;
    if (max < 0.0f) max = 0.0f;
    if (in->wheel != 0.0f && over(in, r)) {
        *scroll -= in->wheel * WHEEL_STEP;
        in->wheel = 0.0f;   /* consumed */
    }
    if (*scroll > max) *scroll = max;
    if (*scroll < 0.0f) *scroll = 0.0f;
}

int32_t ui_popup_list(UiCanvas* c, UiInput* in, UiRect anchor, const char* const* items, int32_t count,
                      int32_t selected) {
    const float row = 26.0f, w = 190.0f;
    UiRect box = {anchor.x, anchor.y + anchor.h + 4.0f, w, row * (float)count + 8.0f};
    float canvas_h = (float)c->h / c->scale;
    if (box.y + box.h > canvas_h - 4.0f) box.y = anchor.y - 4.0f - box.h;   /* no room below: open upward */
    if (box.y < 4.0f) box.y = 4.0f;
    in->popup_rect = box;
    /* Only input that arrives after the pop-up opened counts: the press and
     * release of a trackpad tap land in the frame that opened it. */
    bool live = in->blocked;
    int32_t result = UI_POPUP_NONE;
    bool in_box = inside(box, in->mx, in->my);
    if (live && in->pressed && !in_box) result = UI_POPUP_DISMISSED;
    if (live && ui_input_has_key(in, UI_KEY_ESCAPE)) result = UI_POPUP_DISMISSED;

    ui_fill_round_rect(c, (UiRect){box.x - 1, box.y - 1, box.w + 2, box.h + 2}, 9.0f, UI_DIVIDER);
    ui_fill_round_rect(c, box, 8.0f, UI_SURFACE);
    for (int32_t i = 0; i < count; i++) {
        UiRect rr = {box.x + 4.0f, box.y + 4.0f + row * (float)i, box.w - 8.0f, row};
        bool hover = inside(rr, in->mx, in->my);
        if (hover || i == selected) ui_fill_round_rect(c, rr, 5.0f, hover ? UI_SELECTED : UI_BG);
        float y = rr.y + rr.h / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, UI_SIZE_SMALL);
        ui_text(c, i == selected ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, UI_SIZE_SMALL, rr.x + 8.0f, y, rr.w - 12.0f,
                UI_ALIGN_LEFT, UI_TEXT, items[i]);
        if (live && hover && in->released) result = i;
    }
    return result;
}
