#include "frontend/library_view.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PAD        20.0f
#define BAR_H      50.0f
#define BTN_H      26.0f
#define BTN_Y      12.0f
#define OPEN_W     92.0f
#define FOLDER_W   118.0f
#define BANNER_H   76.0f
#define PLAY_W     84.0f
#define ROW_H      26.0f
#define DOUBLE_MS  400u

#define MDOT  " \xC2\xB7 "    /* " · " */
#define ELLIP "\xE2\x80\xA6"  /* … */
#define NDASH "\xE2\x80\x93"  /* – */

static bool inside(UiRect r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

void library_view_layout(const LibView* v, const Library* lib, LibLayout* l) {
    memset(l, 0, sizeof(*l));
    l->open_btn = (UiRect){v->w - PAD - OPEN_W, BTN_Y, OPEN_W, BTN_H};
    l->folder_btn = (UiRect){l->open_btn.x - 8.0f - FOLDER_W, BTN_Y, FOLDER_W, BTN_H};
    float y = BAR_H;
    if (v->error[0]) {
        l->error_y = y + 8.0f;
        y += 24.0f;
    }
    l->banner = lib->banner >= 0;
    if (l->banner) {
        l->banner_rect = (UiRect){PAD, y + 12.0f, v->w - 2 * PAD, BANNER_H};
        l->play_btn = (UiRect){l->banner_rect.x + l->banner_rect.w - 16.0f - PLAY_W,
                               l->banner_rect.y + (BANNER_H - 30.0f) / 2, PLAY_W, 30.0f};
        y = l->banner_rect.y + BANNER_H;
    }
    l->section_y = y + 14.0f;
    l->table_y = l->section_y + 40.0f;
    l->row_h = ROW_H;
    l->foot_y = v->h - 24.0f;
    l->rows = (int32_t)((l->foot_y - 6.0f - l->table_y) / ROW_H);
    if (l->rows < 1) l->rows = 1;
}

static int32_t first_sel(const Library* lib) {
    return lib->banner >= 0 ? LIB_SEL_BANNER : 0;
}

static void keep_visible(LibView* v, const Library* lib) {
    LibLayout l;
    library_view_layout(v, lib, &l);
    int32_t count = (int32_t)lib->game_count;
    if (v->sel >= 0) {
        if (v->sel < v->scroll) v->scroll = v->sel;
        if (v->sel >= v->scroll + l.rows) v->scroll = v->sel - l.rows + 1;
    }
    int32_t max_scroll = count > l.rows ? count - l.rows : 0;
    if (v->scroll > max_scroll) v->scroll = max_scroll;
    if (v->scroll < 0) v->scroll = 0;
}

void library_view_reset(LibView* v, const Library* lib, float w, float h) {
    char error[sizeof(v->error)];
    memcpy(error, v->error, sizeof(error));
    memset(v, 0, sizeof(*v));
    memcpy(v->error, error, sizeof(error));
    v->w = w;
    v->h = h;
    v->sel = first_sel(lib);
    v->hover_row = -1;
    v->last_click_row = -2;
}

const char* library_view_selected_path(const LibView* v, const Library* lib) {
    if (v->sel == LIB_SEL_BANNER) return lib->banner >= 0 ? lib->games[lib->banner].path : NULL;
    if (v->sel >= 0 && (uint32_t)v->sel < lib->game_count) return lib->games[v->sel].path;
    return NULL;
}

static LibAction move_to(LibView* v, const Library* lib, int32_t sel) {
    int32_t count = (int32_t)lib->game_count;
    int32_t lo = lib->banner >= 0 ? LIB_SEL_BANNER : 0;
    if (sel > count - 1) sel = count - 1;
    if (sel < lo) sel = lo;
    if (count == 0 && lib->banner < 0) sel = 0;
    v->sel = sel;
    keep_visible(v, lib);
    if (sel == LIB_SEL_BANNER) v->scroll = 0;
    return LIB_ACT_REDRAW;
}

LibAction library_view_key(LibView* v, const Library* lib, LibKey key) {
    LibLayout l;
    library_view_layout(v, lib, &l);
    switch (key) {
    case LIB_KEY_UP:        return move_to(v, lib, v->sel - 1);
    case LIB_KEY_DOWN:      return move_to(v, lib, v->sel + 1);
    case LIB_KEY_PAGE_UP:   return move_to(v, lib, v->sel - l.rows);
    case LIB_KEY_PAGE_DOWN: return move_to(v, lib, v->sel < 0 ? l.rows - 1 : v->sel + l.rows);
    case LIB_KEY_HOME:      return move_to(v, lib, first_sel(lib));
    case LIB_KEY_END:       return move_to(v, lib, (int32_t)lib->game_count - 1);
    case LIB_KEY_ENTER:     return library_view_selected_path(v, lib) ? LIB_ACT_PLAY : LIB_ACT_NONE;
    case LIB_KEY_ESCAPE:    return LIB_ACT_QUIT;
    case LIB_KEY_OPEN:      return LIB_ACT_OPEN_FILE;
    }
    return LIB_ACT_NONE;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

LibAction library_view_char(LibView* v, const Library* lib, char c) {
    int32_t count = (int32_t)lib->game_count;
    if (count == 0) return LIB_ACT_NONE;
    c = lower(c);
    for (int32_t i = 1; i <= count; i++) {
        int32_t idx = ((v->sel < 0 ? -1 : v->sel) + i) % count;
        if (lower(lib->games[idx].name[0]) == c) return move_to(v, lib, idx);
    }
    return LIB_ACT_NONE;
}

static LibHit hit_test(const LibView* v, const Library* lib, float x, float y, int32_t* row) {
    LibLayout l;
    library_view_layout(v, lib, &l);
    *row = -1;
    if (inside(l.folder_btn, x, y)) return LIB_HIT_FOLDER;
    if (inside(l.open_btn, x, y)) return LIB_HIT_OPEN;
    if (l.banner && inside(l.play_btn, x, y)) return LIB_HIT_PLAY;
    if (l.banner && inside(l.banner_rect, x, y)) return LIB_HIT_BANNER;
    if (x >= PAD && x < v->w - PAD && y >= l.table_y && y < l.table_y + l.rows * ROW_H) {
        int32_t r = v->scroll + (int32_t)((y - l.table_y) / ROW_H);
        if (r < (int32_t)lib->game_count) {
            *row = r;
            return LIB_HIT_ROW;
        }
    }
    return LIB_HIT_NONE;
}

LibAction library_view_mouse_move(LibView* v, const Library* lib, float x, float y) {
    int32_t row;
    LibHit h = hit_test(v, lib, x, y, &row);
    if (h == v->hover && row == v->hover_row) return LIB_ACT_NONE;
    v->hover = (uint8_t)h;
    v->hover_row = row;
    return LIB_ACT_REDRAW;
}

LibAction library_view_click(LibView* v, const Library* lib, float x, float y, uint32_t now_ms) {
    int32_t row;
    LibHit h = hit_test(v, lib, x, y, &row);
    int32_t clicked = h == LIB_HIT_BANNER ? LIB_SEL_BANNER : row;
    bool dbl = (h == LIB_HIT_ROW || h == LIB_HIT_BANNER) && clicked == v->last_click_row &&
               now_ms - v->last_click_ms <= DOUBLE_MS;
    v->last_click_row = (h == LIB_HIT_ROW || h == LIB_HIT_BANNER) ? clicked : -2;
    v->last_click_ms = now_ms;
    switch (h) {
    case LIB_HIT_FOLDER: return LIB_ACT_CHOOSE_FOLDER;
    case LIB_HIT_OPEN:   return LIB_ACT_OPEN_FILE;
    case LIB_HIT_PLAY:
        v->sel = LIB_SEL_BANNER;
        return LIB_ACT_PLAY;
    case LIB_HIT_BANNER:
    case LIB_HIT_ROW:
        v->sel = clicked;
        if (dbl) return LIB_ACT_PLAY;
        keep_visible(v, lib);
        return LIB_ACT_REDRAW;
    default:
        return LIB_ACT_NONE;
    }
}

LibAction library_view_wheel(LibView* v, const Library* lib, float notches) {
    int32_t before = v->scroll;
    v->scroll -= (int32_t)(notches * 3.0f + (notches > 0 ? 0.5f : -0.5f));
    int32_t sel = v->sel;
    v->sel = -1000;  /* scroll freely; the selection may leave the view */
    keep_visible(v, lib);
    v->sel = sel;
    return v->scroll != before ? LIB_ACT_REDRAW : LIB_ACT_NONE;
}

/* ---- Formatting ---- */

void library_format_played(int64_t played, int64_t now, char* out, size_t out_size) {
    static const char* const months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int64_t ago = now - played;
    if (played <= 0) { snprintf(out, out_size, "Never"); return; }
    if (ago < 60) { snprintf(out, out_size, "Just now"); return; }
    if (ago < 3600) { snprintf(out, out_size, "%d min ago", (int)(ago / 60)); return; }
    if (ago < 86400) {
        int h = (int)(ago / 3600);
        snprintf(out, out_size, h == 1 ? "1 hour ago" : "%d hours ago", h);
        return;
    }
    if (ago < 2 * 86400) { snprintf(out, out_size, "Yesterday"); return; }
    time_t t = (time_t)played, n = (time_t)now;
    struct tm then = *localtime(&t);
    int this_year = localtime(&n)->tm_year;
    if (then.tm_year == this_year)
        snprintf(out, out_size, "%s %d", months[then.tm_mon], then.tm_mday);
    else
        snprintf(out, out_size, "%s %d, %d", months[then.tm_mon], then.tm_mday, then.tm_year + 1900);
}

void library_format_size(uint32_t bytes, char* out, size_t out_size) {
    if (bytes >= 1024u * 1024u)
        snprintf(out, out_size, "%u MB", (unsigned)((bytes + 512u * 1024u) / (1024u * 1024u)));
    else
        snprintf(out, out_size, "%u KB", (unsigned)((bytes + 1023u) / 1024u));
}

/* ---- Drawing ---- */

static uint32_t tile_color(const LibGame* g) {
    static const uint32_t palette[8] = {0xFF2E9E6Bu, 0xFFD9603Bu, 0xFF7B5BD6u, 0xFF3A7BD5u,
                                        0xFFC9A227u, 0xFFD6487Eu, 0xFF2A9BB0u, 0xFF5A6475u};
    const char* s = g->code[0] ? g->code : g->name;
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return palette[h % 8];
}

static void draw_tile(UiCanvas* c, float x, float y, float w, float h, const LibGame* g, bool label) {
    ui_fill_round_rect(c, (UiRect){x, y, w, h}, w * 0.12f, tile_color(g));
    ui_fill_round_rect(c, (UiRect){x + w * 0.15f, y + h * 0.1f, w * 0.7f, h * 0.38f}, 2.0f, 0x38FFFFFFu);
    if (label && g->code[0])
        ui_text(c, UI_FONT_SEMIBOLD, 9.0f, x + w / 2, y + h - 14.0f, w, UI_ALIGN_CENTER, 0xDDFFFFFFu, g->code);
}

static void button(UiCanvas* c, UiRect r, const char* text, bool hot, bool primary) {
    uint32_t bg = primary ? UI_ACCENT : (hot ? UI_DIVIDER : UI_SELECTED);
    ui_fill_round_rect(c, r, UI_RADIUS, bg);
    float ty = r.y + r.h / 2 - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL);
    ui_text(c, primary ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, UI_SIZE_SMALL, r.x + r.w / 2, ty, r.w,
            UI_ALIGN_CENTER, primary ? 0xFFFFFFFFu : UI_TEXT, text);
}

/* Draws text in max_w, cutting the middle out of long paths: "/Users/…/roms". */
static void middle_ellipsis(UiCanvas* c, float x, float y, float max_w, const char* s) {
    char buf[LIB_PATH_LEN + 4];
    size_t len = strlen(s);
    if (max_w <= 0) return;
    if (ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_LABEL, s) <= max_w) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x, y, max_w, UI_ALIGN_LEFT, UI_FAINT, s);
        return;
    }
    for (size_t keep = len; keep > 4; keep--) {
        size_t head = keep / 3, tail = keep - head;
        while (head > 0 && ((unsigned char)s[head] & 0xC0) == 0x80) head--;
        size_t from = len - tail;
        while (from < len && ((unsigned char)s[from] & 0xC0) == 0x80) from++;
        snprintf(buf, sizeof(buf), "%.*s" ELLIP "%s", (int)head, s, s + from);
        if (ui_text_width(c, UI_FONT_REGULAR, UI_SIZE_LABEL, buf) <= max_w) break;
    }
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x, y, max_w, UI_ALIGN_LEFT, UI_FAINT, buf);
}

static void header_text(const LibGame* g, char* out, size_t out_size) {
    if (g->title[0] && g->code[0]) snprintf(out, out_size, "%s" MDOT "%s", g->title, g->code);
    else if (g->title[0]) snprintf(out, out_size, "%s", g->title);
    else if (g->code[0]) snprintf(out, out_size, "%s", g->code);
    else snprintf(out, out_size, "no title");
}

static float pill_w(UiCanvas* c, const char* t) {
    return ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, t) + 14.0f;
}

static void draw_banner(UiCanvas* c, const LibView* v, const LibLayout* l, const Library* lib, int64_t now) {
    const LibGame* g = &lib->games[lib->banner];
    UiRect r = l->banner_rect;
    bool sel = v->sel == LIB_SEL_BANNER;
    if (sel) ui_fill_round_rect(c, (UiRect){r.x - 1.5f, r.y - 1.5f, r.w + 3.0f, r.h + 3.0f}, 11.5f, UI_ACCENT);
    ui_fill_round_rect(c, r, 10.0f, v->hover == LIB_HIT_BANNER && !sel ? UI_SELECTED : UI_SURFACE);
    draw_tile(c, r.x + 16.0f, r.y + 12.0f, 44.0f, 52.0f, g, true);
    float tx = r.x + 76.0f, tw = l->play_btn.x - 14.0f - tx;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, tx, r.y + 12.0f, tw, UI_ALIGN_LEFT, UI_DIM, "CONTINUE PLAYING");
    float nw = ui_text(c, UI_FONT_SEMIBOLD, 17.0f, tx, r.y + 27.0f, tw, UI_ALIGN_LEFT, UI_TEXT, g->name);
    if (g->companion && tx + nw + 10.0f + pill_w(c, "Companion") < tx + tw)
        ui_pill(c, tx + nw + 10.0f, r.y + 31.0f, "Companion", UI_GOOD);
    char hdr[48], when[32], line[96];
    header_text(g, hdr, sizeof(hdr));
    library_format_played(g->played, now, when, sizeof(when));
    snprintf(line, sizeof(line), "%s" MDOT "last played %s", hdr, when);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL - 0.5f, tx, r.y + 52.0f, tw, UI_ALIGN_LEFT, UI_MUTED, line);
    button(c, l->play_btn, "Play", v->hover == LIB_HIT_PLAY, true);
}

static void draw_empty(UiCanvas* c, const LibView* v, const Library* lib, float y) {
    char line[LIB_PATH_LEN + 32];
    const char* title;
    const char* body;
    if (!lib->folder[0]) {
        title = "Choose the folder where you keep your GBA games";
        body = "Every .gba file in it shows up here. You can also open a single file.";
    } else {
        snprintf(line, sizeof(line), lib->folder_missing ? "Can't open %s" : "No .gba files in %s", lib->folder);
        title = line;
        body = "Choose another folder, or open a single file.";
    }
    float cx = v->w / 2, w = v->w - 2 * PAD;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY + 1.0f, cx, y, w, UI_ALIGN_CENTER, UI_TEXT, title);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, cx, y + 24.0f, w, UI_ALIGN_CENTER, UI_MUTED, body);
}

void library_view_draw(UiCanvas* c, const LibView* v, const Library* lib, int64_t now) {
    LibLayout l;
    library_view_layout(v, lib, &l);
    ui_canvas_unclip(c);
    ui_canvas_clear(c, UI_BG);

    /* Top bar */
    float tw = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, PAD, 14.0f, 0, UI_ALIGN_LEFT, UI_TEXT, "Library");
    if (lib->folder[0])
        middle_ellipsis(c, PAD + tw + 12.0f, 21.0f, l.folder_btn.x - 16.0f - (PAD + tw + 12.0f), lib->folder);
    button(c, l.folder_btn, "Choose folder" ELLIP, v->hover == LIB_HIT_FOLDER, false);
    button(c, l.open_btn, "Open file" ELLIP, v->hover == LIB_HIT_OPEN, false);
    ui_hline(c, 0, BAR_H, v->w, UI_DIVIDER);
    if (v->error[0])
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, PAD, l.error_y, v->w - 2 * PAD, UI_ALIGN_LEFT, UI_BAD, v->error);

    if (l.banner) draw_banner(c, v, &l, lib, now);

    char buf[96];
    snprintf(buf, sizeof(buf), "ALL GAMES" MDOT "%u", (unsigned)lib->game_count);
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, PAD, l.section_y, 0, UI_ALIGN_LEFT, UI_DIM, buf);
    if (lib->game_count == 0) {
        draw_empty(c, v, lib, l.section_y + 70.0f);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, PAD, l.foot_y, v->w - 2 * PAD, UI_ALIGN_LEFT, UI_FAINT,
                "Drop a .gba file here to open it" MDOT "Esc quits");
        return;
    }

    /* Columns */
    float x_name = PAD + 28.0f, inner = v->w - PAD - x_name;
    float x_hdr = x_name + inner * 0.36f, x_size = x_name + inner * 0.68f, x_played = x_name + inner * 0.80f;
    float hy = l.section_y + 20.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x_name, hy, 0, UI_ALIGN_LEFT, UI_DIM, "NAME");
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x_hdr, hy, 0, UI_ALIGN_LEFT, UI_DIM, "HEADER");
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x_size, hy, 0, UI_ALIGN_LEFT, UI_DIM, "SIZE");
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x_played, hy, 0, UI_ALIGN_LEFT, UI_DIM, "PLAYED");
    ui_hline(c, PAD, l.table_y - 4.0f, v->w - 2 * PAD, UI_DIVIDER);

    ui_canvas_clip(c, (UiRect){0, l.table_y - 2.0f, v->w, l.rows * ROW_H + 2.0f});
    for (int32_t i = 0; i < l.rows; i++) {
        int32_t idx = v->scroll + i;
        if (idx >= (int32_t)lib->game_count) break;
        const LibGame* g = &lib->games[idx];
        float y = l.table_y + i * ROW_H;
        bool sel = idx == v->sel, hot = v->hover == LIB_HIT_ROW && v->hover_row == idx;
        if (sel || hot) ui_fill_round_rect(c, (UiRect){PAD - 6.0f, y, v->w - 2 * PAD + 12.0f, ROW_H - 2.0f},
                                           UI_RADIUS, sel ? UI_SELECTED : UI_SURFACE);
        if (sel) ui_fill_rect(c, (UiRect){PAD - 6.0f, y + 4.0f, 2.0f, ROW_H - 10.0f}, UI_ACCENT);
        draw_tile(c, PAD + 2.0f, y + 3.5f, 15.0f, 18.0f, g, false);
        float ty = y + (ROW_H - 2.0f) / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, 12.5f);
        ui_text(c, sel ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, 12.5f, x_name, ty, x_hdr - x_name - 10.0f,
                UI_ALIGN_LEFT, UI_TEXT, g->name);
        header_text(g, buf, sizeof(buf));
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x_hdr, ty, x_size - x_hdr - 10.0f, UI_ALIGN_LEFT,
                g->title[0] || g->code[0] ? UI_MUTED : UI_FAINT, buf);
        library_format_size(g->size, buf, sizeof(buf));
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x_size, ty, x_played - x_size - 10.0f, UI_ALIGN_LEFT, UI_MUTED, buf);
        library_format_played(g->played, now, buf, sizeof(buf));
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x_played, ty, v->w - PAD - x_played, UI_ALIGN_LEFT,
                g->played ? UI_MUTED : UI_FAINT, buf);
    }
    ui_canvas_unclip(c);

    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, PAD, l.foot_y, v->w * 0.65f, UI_ALIGN_LEFT, UI_FAINT,
            "Arrows move" MDOT "Enter plays" MDOT "drop a .gba file here to open it");
    if ((int32_t)lib->game_count > l.rows) {
        int32_t last = v->scroll + l.rows;
        if (last > (int32_t)lib->game_count) last = (int32_t)lib->game_count;
        snprintf(buf, sizeof(buf), "%d" NDASH "%d of %u", v->scroll + 1, last, (unsigned)lib->game_count);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, v->w - PAD, l.foot_y, 0, UI_ALIGN_RIGHT, UI_FAINT, buf);
    }
}
