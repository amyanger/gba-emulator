#include "test_harness.h"
#include "frontend/library_view.h"

#define W 720.0f
#define H 480.0f
#define GUARD 64
#define SENTINEL 0x12345678u

/* Large: keep off the stack. */
static Library s_lib;
static uint32_t s_buf[2160 * 1440 + GUARD];

static void fill(Library* lib, uint32_t n, bool banner) {
    library_init(lib, NULL);
    lib->game_count = n;
    for (uint32_t i = 0; i < n; i++) {
        LibGame* g = &lib->games[i];
        memset(g, 0, sizeof(*g));
        snprintf(g->name, sizeof(g->name), "%c%s %03u", 'A' + (char)(i % 26), i % 7 ? "" : "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", (unsigned)i);
        snprintf(g->path, sizeof(g->path), "/roms/%03u.gba", (unsigned)i);
        if (i % 3) { memcpy(g->title, "POKEMON EMER", 13); memcpy(g->code, "BPEE", 5); }
        g->size = 1024u * (i + 1);
        g->played = i % 2 ? 1700000000 : 0;
        g->companion = i == 0;
    }
    lib->banner = banner && n ? 0 : -1;
    memset(lib->folder, 'f', 600);
    lib->folder[0] = '/';
}

TEST(library_view_keys_move_and_scroll) {
    static LibView v;
    LibLayout l;
    fill(&s_lib, 100, true);
    memset(&v, 0, sizeof(v));
    library_view_reset(&v, &s_lib, W, H);
    ASSERT_EQ(v.sel, LIB_SEL_BANNER);
    ASSERT_STR_EQ(library_view_selected_path(&v, &s_lib), "/roms/000.gba");
    ASSERT_EQ(library_view_key(&v, &s_lib, LIB_KEY_ENTER), LIB_ACT_PLAY);
    library_view_key(&v, &s_lib, LIB_KEY_UP);
    ASSERT_EQ(v.sel, LIB_SEL_BANNER);  /* can't go above the banner */
    library_view_key(&v, &s_lib, LIB_KEY_DOWN);
    ASSERT_EQ(v.sel, 0);
    library_view_key(&v, &s_lib, LIB_KEY_END);
    ASSERT_EQ(v.sel, 99);
    library_view_layout(&v, &s_lib, &l);
    ASSERT_TRUE(l.rows > 3);
    ASSERT_EQ(v.scroll, 100 - l.rows);  /* last row in view */
    library_view_key(&v, &s_lib, LIB_KEY_PAGE_UP);
    ASSERT_EQ(v.sel, 99 - l.rows);
    ASSERT_TRUE(v.sel >= v.scroll && v.sel < v.scroll + l.rows);
    library_view_key(&v, &s_lib, LIB_KEY_HOME);
    ASSERT_EQ(v.sel, LIB_SEL_BANNER);
    ASSERT_EQ(v.scroll, 0);
    ASSERT_EQ(library_view_key(&v, &s_lib, LIB_KEY_ESCAPE), LIB_ACT_QUIT);
    ASSERT_EQ(library_view_key(&v, &s_lib, LIB_KEY_OPEN), LIB_ACT_OPEN_FILE);
}

TEST(library_view_no_banner_starts_on_first_row) {
    static LibView v;
    fill(&s_lib, 3, false);
    memset(&v, 0, sizeof(v));
    library_view_reset(&v, &s_lib, W, H);
    ASSERT_EQ(v.sel, 0);
    library_view_key(&v, &s_lib, LIB_KEY_UP);
    ASSERT_EQ(v.sel, 0);
    fill(&s_lib, 0, false);
    library_view_reset(&v, &s_lib, W, H);
    ASSERT_TRUE(library_view_selected_path(&v, &s_lib) == NULL);
    ASSERT_EQ(library_view_key(&v, &s_lib, LIB_KEY_ENTER), LIB_ACT_NONE);
    ASSERT_EQ(library_view_char(&v, &s_lib, 'a'), LIB_ACT_NONE);
}

TEST(library_view_type_to_find_wraps) {
    static LibView v;
    fill(&s_lib, 60, false);
    memset(&v, 0, sizeof(v));
    library_view_reset(&v, &s_lib, W, H);
    library_view_char(&v, &s_lib, 'c');
    ASSERT_EQ(v.sel, 2);  /* "C..." */
    library_view_char(&v, &s_lib, 'C');
    ASSERT_EQ(v.sel, 28);
    library_view_char(&v, &s_lib, 'c');
    library_view_char(&v, &s_lib, 'c');
    ASSERT_EQ(v.sel, 2);  /* wrapped */
}

TEST(library_view_clicks_buttons_rows_double_click) {
    static LibView v;
    LibLayout l;
    fill(&s_lib, 30, true);
    memset(&v, 0, sizeof(v));
    library_view_reset(&v, &s_lib, W, H);
    library_view_layout(&v, &s_lib, &l);
    ASSERT_EQ(library_view_click(&v, &s_lib, l.folder_btn.x + 5, l.folder_btn.y + 5, 1000), LIB_ACT_CHOOSE_FOLDER);
    ASSERT_EQ(library_view_click(&v, &s_lib, l.open_btn.x + 5, l.open_btn.y + 5, 2000), LIB_ACT_OPEN_FILE);
    ASSERT_EQ(library_view_click(&v, &s_lib, l.play_btn.x + 5, l.play_btn.y + 5, 3000), LIB_ACT_PLAY);
    float ry = l.table_y + 2 * l.row_h + 5;
    ASSERT_EQ(library_view_click(&v, &s_lib, 100, ry, 4000), LIB_ACT_REDRAW);
    ASSERT_EQ(v.sel, 2);
    ASSERT_EQ(library_view_click(&v, &s_lib, 100, ry, 4300), LIB_ACT_PLAY);  /* double click */
    ASSERT_EQ(library_view_click(&v, &s_lib, 100, ry, 9000), LIB_ACT_REDRAW);  /* too slow */
    ASSERT_EQ(library_view_click(&v, &s_lib, 100, l.table_y + 3 * l.row_h + 5, 9100), LIB_ACT_REDRAW);
    ASSERT_EQ(v.sel, 3);  /* a different row is not a double click */
    ASSERT_EQ(library_view_click(&v, &s_lib, 2, 470, 9200), LIB_ACT_NONE);
    ASSERT_EQ(library_view_mouse_move(&v, &s_lib, 100, ry), LIB_ACT_REDRAW);
    ASSERT_EQ(library_view_mouse_move(&v, &s_lib, 101, ry), LIB_ACT_NONE);  /* same row */
}

TEST(library_view_wheel_clamps) {
    static LibView v;
    LibLayout l;
    fill(&s_lib, 40, false);
    memset(&v, 0, sizeof(v));
    library_view_reset(&v, &s_lib, W, H);
    library_view_layout(&v, &s_lib, &l);
    ASSERT_EQ(library_view_wheel(&v, &s_lib, 1.0f), LIB_ACT_NONE);  /* already at the top */
    ASSERT_EQ(library_view_wheel(&v, &s_lib, -100.0f), LIB_ACT_REDRAW);
    ASSERT_EQ(v.scroll, 40 - l.rows);
    ASSERT_EQ(v.sel, 0);  /* scrolling leaves the selection alone */
}

TEST(library_view_formats) {
    char b[32];
    int64_t now = 1800000000;
    library_format_played(0, now, b, sizeof(b));
    ASSERT_STR_EQ(b, "Never");
    library_format_played(now - 10, now, b, sizeof(b));
    ASSERT_STR_EQ(b, "Just now");
    library_format_played(now - 600, now, b, sizeof(b));
    ASSERT_STR_EQ(b, "10 min ago");
    library_format_played(now - 3700, now, b, sizeof(b));
    ASSERT_STR_EQ(b, "1 hour ago");
    library_format_played(now - 7300, now, b, sizeof(b));
    ASSERT_STR_EQ(b, "2 hours ago");
    library_format_played(now - 90000, now, b, sizeof(b));
    ASSERT_STR_EQ(b, "Yesterday");
    library_format_played(now - 86400 * 400, now, b, sizeof(b));
    ASSERT_TRUE(strchr(b, ',') != NULL);  /* another year shows the year */
    library_format_size(16u * 1024 * 1024, b, sizeof(b));
    ASSERT_STR_EQ(b, "16 MB");
    library_format_size(1000, b, sizeof(b));
    ASSERT_STR_EQ(b, "1 KB");
}

static bool draw_ok(float density, const Library* lib, float w_pt, float h_pt, const char* error) {
    static LibView v;
    int w = (int)(w_pt * density + 0.5f), h = (int)(h_pt * density + 0.5f);
    for (int i = 0; i < GUARD; i++) s_buf[w * h + i] = SENTINEL;
    memset(&v, 0, sizeof(v));
    snprintf(v.error, sizeof(v.error), "%s", error);
    library_view_reset(&v, lib, w_pt, h_pt);
    library_view_key(&v, lib, LIB_KEY_END);
    UiCanvas c;
    ui_canvas_init(&c, s_buf, w, h, density);
    library_view_draw(&c, &v, lib, 1800000000);
    for (int i = 0; i < GUARD; i++)
        if (s_buf[w * h + i] != SENTINEL) return false;
    return (s_buf[w * h - 1] >> 24) == 0xFF;
}

TEST(library_view_draws_in_bounds) {
    static const float densities[] = {0.75f, 1.0f, 2.0f, 3.0f};
    static Library empty, one, many, nofolder;
    fill(&empty, 0, false);
    fill(&one, 1, true);
    fill(&many, LIB_GAMES_MAX, true);
    library_init(&nofolder, NULL);
    for (size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); d++) {
        ASSERT_TRUE(draw_ok(densities[d], &empty, W, H, ""));
        ASSERT_TRUE(draw_ok(densities[d], &one, W, H, "Couldn't load something as a GBA ROM"));
        ASSERT_TRUE(draw_ok(densities[d], &many, W, H, ""));
        ASSERT_TRUE(draw_ok(densities[d], &nofolder, W, H, ""));
        ASSERT_TRUE(draw_ok(densities[d], &many, 480.0f, 320.0f, "error"));
    }
}

void run_library_view_tests(void) {
    RUN_TEST(library_view_keys_move_and_scroll);
    RUN_TEST(library_view_no_banner_starts_on_first_row);
    RUN_TEST(library_view_type_to_find_wraps);
    RUN_TEST(library_view_clicks_buttons_rows_double_click);
    RUN_TEST(library_view_wheel_clamps);
    RUN_TEST(library_view_formats);
    RUN_TEST(library_view_draws_in_bounds);
}
