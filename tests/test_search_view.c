#include "test_harness.h"
#include "frontend/search_view.h"
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_scancode.h>

static SearchView fresh(void) {
    SearchView sv;
    search_view_init(&sv);
    return sv;
}

TEST(search_view_init_state) {
    SearchView sv = fresh();
    ASSERT_STR_EQ(sv.query, "");
    ASSERT_TRUE(!sv.focused);
    ASSERT_EQ(sv.sel, 0);
    ASSERT_EQ(sv.list_top, 0);
    ASSERT_EQ(sv.detail_top, 0);
    ASSERT_EQ(sv.result_count, 0);
}

TEST(search_view_layout_fits_panel) {
    SearchLayout l;
    search_view_layout(&l);
    ASSERT_TRUE(l.box_x >= 16.0f && l.box_x + l.box_w < l.detail_x);
    ASSERT_TRUE(l.list_y >= l.box_y + l.box_h);
    ASSERT_TRUE(l.list_rows > 5);
    ASSERT_TRUE(l.list_y + l.list_rows * l.row_h <= 476.0f);
    ASSERT_TRUE(l.detail_x + l.detail_w <= 584.0f);
    ASSERT_TRUE(l.detail_y + l.detail_h <= 480.0f);
}

TEST(search_view_text_filters_characters) {
    SearchView sv = fresh();
    ASSERT_EQ(search_view_text(&sv, "Mr. Mime!"), SV_CHANGED);
    ASSERT_STR_EQ(sv.query, "Mr. Mime");
    ASSERT_EQ(search_view_text(&sv, "!?@#"), SV_NONE);
    ASSERT_STR_EQ(sv.query, "Mr. Mime");
    SearchView b = fresh();
    search_view_text(&b, "Farfetch'd-0 9");
    ASSERT_STR_EQ(b.query, "Farfetch'd-0 9");
}

TEST(search_view_text_caps_at_15) {
    SearchView sv = fresh();
    search_view_text(&sv, "abcdefghijklmnopqrst");
    ASSERT_STR_EQ(sv.query, "abcdefghijklmno");
    uint32_t v = sv.version;
    ASSERT_EQ(search_view_text(&sv, "z"), SV_NONE);
    ASSERT_EQ(sv.version, v);
}

TEST(search_view_text_resets_selection) {
    SearchView sv = fresh();
    search_view_set_results(&sv, 40);
    search_view_set_detail_lines(&sv, 100, 20);
    for (int i = 0; i < 30; i++) search_view_key(&sv, SV_KEY_DOWN);
    search_view_key(&sv, SV_KEY_PAGE_DOWN);
    ASSERT_TRUE(sv.sel == 30 && sv.list_top > 0 && sv.detail_top == 5);
    search_view_text(&sv, "a");
    ASSERT_EQ(sv.sel, 0);
    ASSERT_EQ(sv.list_top, 0);
    ASSERT_EQ(sv.detail_top, 0);
}

TEST(search_view_backspace_clear_escape_enter) {
    SearchView sv = fresh();
    search_view_set_focus(&sv, true);
    search_view_text(&sv, "abc");
    ASSERT_EQ(search_view_key(&sv, SV_KEY_BACKSPACE), SV_CHANGED);
    ASSERT_STR_EQ(sv.query, "ab");
    ASSERT_EQ(search_view_key(&sv, SV_KEY_ESCAPE), SV_CHANGED);
    ASSERT_STR_EQ(sv.query, "");
    ASSERT_EQ(search_view_key(&sv, SV_KEY_BACKSPACE), SV_NONE);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_ESCAPE), SV_UNFOCUS);
    search_view_text(&sv, "xy");
    ASSERT_EQ(search_view_key(&sv, SV_KEY_CLEAR), SV_CHANGED);
    ASSERT_STR_EQ(sv.query, "");
    ASSERT_EQ(search_view_key(&sv, SV_KEY_CLEAR), SV_NONE);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_ENTER), SV_UNFOCUS);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_OTHER), SV_NONE);
}

TEST(search_view_selection_clamps_and_stays_visible) {
    SearchView sv = fresh();
    SearchLayout l;
    search_view_layout(&l);
    search_view_set_results(&sv, 20);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_UP), SV_NONE);
    ASSERT_EQ(sv.sel, 0);
    for (int i = 0; i < 40; i++) {
        search_view_key(&sv, SV_KEY_DOWN);
        ASSERT_TRUE(sv.sel >= sv.list_top && sv.sel < sv.list_top + l.list_rows);
    }
    ASSERT_EQ(sv.sel, 19);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_DOWN), SV_NONE);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_HOME), SV_CHANGED);
    ASSERT_TRUE(sv.sel == 0 && sv.list_top == 0);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_END), SV_CHANGED);
    ASSERT_EQ(sv.sel, 19);
    ASSERT_TRUE(sv.sel < sv.list_top + l.list_rows);
    for (int i = 0; i < 40; i++) search_view_key(&sv, SV_KEY_UP);
    ASSERT_TRUE(sv.sel == 0 && sv.list_top == 0);
}

TEST(search_view_set_results_clamps) {
    SearchView sv = fresh();
    search_view_set_results(&sv, 100);
    search_view_key(&sv, SV_KEY_END);
    ASSERT_EQ(sv.sel, 99);
    search_view_set_results(&sv, 10);
    ASSERT_EQ(sv.result_count, 10);
    ASSERT_EQ(sv.sel, 9);
    ASSERT_EQ(sv.list_top, 0);
    search_view_set_results(&sv, 0);
    ASSERT_TRUE(sv.sel == 0 && sv.list_top == 0 && sv.result_count == 0);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_DOWN), SV_NONE);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_END), SV_NONE);
}

TEST(search_view_detail_paging) {
    SearchView sv = fresh();
    search_view_set_detail_lines(&sv, 100, 20);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_PAGE_UP), SV_NONE);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_PAGE_DOWN), SV_CHANGED);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_PAGE_DOWN), SV_CHANGED);
    ASSERT_EQ(sv.detail_top, 10);
    search_view_key(&sv, SV_KEY_PAGE_UP);
    ASSERT_EQ(sv.detail_top, 5);
    search_view_key(&sv, SV_KEY_PAGE_UP);
    search_view_key(&sv, SV_KEY_PAGE_UP);
    ASSERT_EQ(sv.detail_top, 0);
}

TEST(search_view_selection_change_resets_detail) {
    SearchView sv = fresh();
    search_view_set_results(&sv, 5);
    search_view_set_detail_lines(&sv, 100, 20);
    search_view_key(&sv, SV_KEY_PAGE_DOWN);
    search_view_key(&sv, SV_KEY_DOWN);
    ASSERT_EQ(sv.detail_top, 0);
}

TEST(search_view_click_hit_testing) {
    SearchView sv = fresh();
    SearchLayout l;
    search_view_layout(&l);
    search_view_set_results(&sv, 30);

    ASSERT_EQ(search_view_click(&sv, &l, l.box_x + 5, l.box_y + 5), SV_CHANGED);
    ASSERT_TRUE(sv.focused);
    ASSERT_EQ(search_view_click(&sv, &l, l.box_x + 5, l.box_y + 5), SV_NONE);

    /* Row click selects, keeps focus. */
    ASSERT_EQ(search_view_click(&sv, &l, l.list_x + 5, l.list_y + 2.5f * l.row_h), SV_CHANGED);
    ASSERT_EQ(sv.sel, 2);
    ASSERT_TRUE(sv.focused);
    ASSERT_EQ(search_view_click(&sv, &l, l.list_x + 5, l.list_y + 2.5f * l.row_h), SV_NONE);

    /* Below the last visible row and elsewhere unfocus. */
    ASSERT_EQ(search_view_click(&sv, &l, l.detail_x + 5, l.detail_y + 5), SV_UNFOCUS);
    ASSERT_TRUE(!sv.focused);
    ASSERT_EQ(search_view_click(&sv, &l, l.detail_x + 5, l.detail_y + 5), SV_NONE);
}

TEST(search_view_click_row_respects_scroll_and_count) {
    SearchView sv = fresh();
    SearchLayout l;
    search_view_layout(&l);
    search_view_set_results(&sv, 3);
    /* Empty row area past the results does nothing to selection. */
    search_view_click(&sv, &l, l.list_x + 5, l.list_y + 5.5f * l.row_h);
    ASSERT_EQ(sv.sel, 0);

    search_view_set_results(&sv, 50);
    search_view_key(&sv, SV_KEY_END);
    uint16_t top = sv.list_top;
    ASSERT_TRUE(top > 0);
    search_view_click(&sv, &l, l.list_x + 5, l.list_y + 0.5f * l.row_h);
    ASSERT_EQ(sv.sel, top);
}

TEST(search_wheel_accumulates_fractions) {
    float acc = 0.0f;
    ASSERT_EQ(search_wheel_notches(&acc, 0.3f), 0);
    ASSERT_EQ(search_wheel_notches(&acc, 0.3f), 0);
    ASSERT_EQ(search_wheel_notches(&acc, 0.3f), 0);
    ASSERT_EQ(search_wheel_notches(&acc, 0.3f), -1);   /* up 1.2 total: one notch up */
    ASSERT_EQ(search_wheel_notches(&acc, -2.5f), 2);   /* down, keeping the remainder */
    ASSERT_EQ(search_wheel_notches(&acc, -0.8f), 1);
}

TEST(search_view_wheel_region_ids) {
    SearchLayout l;
    search_view_layout(&l);
    ASSERT_EQ(search_view_wheel_region(&l, l.list_x + 5, l.list_y + 5), 1);
    ASSERT_EQ(search_view_wheel_region(&l, l.detail_x + 5, l.detail_y + 5), 2);
    ASSERT_EQ(search_view_wheel_region(&l, -5.0f, -5.0f), 0);
}

TEST(search_view_wheel_regions) {
    SearchView sv = fresh();
    SearchLayout l;
    search_view_layout(&l);
    search_view_set_results(&sv, 50);
    search_view_set_detail_lines(&sv, 100, 20);
    ASSERT_EQ(search_view_wheel(&sv, &l, l.list_x + 5, l.list_y + 5, -1), SV_NONE);
    ASSERT_EQ(search_view_wheel(&sv, &l, l.list_x + 5, l.list_y + 5, 1), SV_CHANGED);
    ASSERT_TRUE(sv.list_top > 0);
    ASSERT_EQ(sv.sel, 0);
    search_view_wheel(&sv, &l, l.list_x + 5, l.list_y + 5, 1000);
    ASSERT_EQ(sv.list_top, 50 - l.list_rows);
    search_view_wheel(&sv, &l, l.list_x + 5, l.list_y + 5, -1000);
    ASSERT_EQ(sv.list_top, 0);

    ASSERT_EQ(search_view_wheel(&sv, &l, l.detail_x + 5, l.detail_y + 5, -1), SV_NONE);
    ASSERT_EQ(search_view_wheel(&sv, &l, l.detail_x + 5, l.detail_y + 5, 2), SV_CHANGED);
    ASSERT_TRUE(sv.detail_top > 0);
    search_view_wheel(&sv, &l, l.detail_x + 5, l.detail_y + 5, -1000);
    ASSERT_EQ(sv.detail_top, 0);
    ASSERT_EQ(search_view_wheel(&sv, &l, l.box_x + 5, l.box_y + 5, 1), SV_NONE);
}

TEST(search_view_version_bumps_only_on_change) {
    SearchView sv = fresh();
    uint32_t v = sv.version;
    search_view_set_focus(&sv, false);
    search_view_key(&sv, SV_KEY_OTHER);
    search_view_key(&sv, SV_KEY_UP);
    search_view_key(&sv, SV_KEY_BACKSPACE);
    search_view_text(&sv, "!");
    search_view_set_results(&sv, 0);
    ASSERT_EQ(sv.version, v);
    search_view_set_focus(&sv, true);
    ASSERT_NEQ(sv.version, v);
    v = sv.version;
    search_view_set_focus(&sv, true);
    ASSERT_EQ(sv.version, v);
    search_view_text(&sv, "a");
    ASSERT_NEQ(sv.version, v);
    v = sv.version;
    search_view_set_results(&sv, 4);
    ASSERT_NEQ(sv.version, v);
    v = sv.version;
    search_view_set_results(&sv, 4);
    ASSERT_EQ(sv.version, v);
    search_view_key(&sv, SV_KEY_DOWN);
    ASSERT_NEQ(sv.version, v);
}

TEST(search_view_detail_clamped_to_range) {
    SearchView sv = fresh();
    search_view_set_detail_lines(&sv, 30, 20);
    for (int i = 0; i < 50; i++) search_view_key(&sv, SV_KEY_PAGE_DOWN);
    ASSERT_EQ(sv.detail_top, 10);
    uint32_t v = sv.version;
    ASSERT_EQ(search_view_key(&sv, SV_KEY_PAGE_DOWN), SV_NONE);
    ASSERT_EQ(sv.version, v);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_PAGE_UP), SV_CHANGED);
    ASSERT_EQ(sv.detail_top, 5);
    search_view_set_detail_lines(&sv, 22, 20);
    ASSERT_EQ(sv.detail_top, 2);
    search_view_set_detail_lines(&sv, 10, 20);
    ASSERT_EQ(sv.detail_top, 0);
    ASSERT_EQ(search_view_key(&sv, SV_KEY_PAGE_DOWN), SV_NONE);
}

TEST(search_view_detail_wheel_clamped) {
    SearchView sv = fresh();
    SearchLayout l;
    search_view_layout(&l);
    search_view_set_detail_lines(&sv, 30, 20);
    search_view_wheel(&sv, &l, l.detail_x + 5, l.detail_y + 5, 1000);
    ASSERT_EQ(sv.detail_top, 10);
    ASSERT_EQ(search_view_wheel(&sv, &l, l.detail_x + 5, l.detail_y + 5, 1), SV_NONE);
}

TEST(search_view_set_results_keeps_detail_when_sel_same) {
    SearchView sv = fresh();
    search_view_set_results(&sv, 5);
    search_view_set_detail_lines(&sv, 100, 20);
    search_view_key(&sv, SV_KEY_PAGE_DOWN);
    search_view_set_results(&sv, 6);
    ASSERT_EQ(sv.detail_top, 5);
    search_view_key(&sv, SV_KEY_END);
    search_view_set_results(&sv, 2);
    ASSERT_EQ(sv.detail_top, 0);
}

TEST(search_guard_blocks_left_key_until_keyup) {
    SearchFocusGuard g = {0};
    ASSERT_TRUE(!search_guard_blocks_keydown(&g, SDL_SCANCODE_RETURN));
    search_guard_leave(&g, SDL_SCANCODE_RETURN);
    ASSERT_TRUE(search_guard_blocks_keydown(&g, SDL_SCANCODE_RETURN));
    ASSERT_TRUE(search_guard_blocks_keydown(&g, SDL_SCANCODE_RETURN));
    ASSERT_TRUE(!search_guard_blocks_keydown(&g, SDL_SCANCODE_X));
    search_guard_keyup(&g, SDL_SCANCODE_X);   /* another key's release leaves it held */
    ASSERT_TRUE(search_guard_blocks_keydown(&g, SDL_SCANCODE_RETURN));
    search_guard_keyup(&g, SDL_SCANCODE_RETURN);
    ASSERT_TRUE(!search_guard_blocks_keydown(&g, SDL_SCANCODE_RETURN));
}

TEST(search_guard_escape) {
    SearchFocusGuard g = {0};
    search_guard_leave(&g, SDL_SCANCODE_ESCAPE);
    ASSERT_TRUE(search_guard_blocks_keydown(&g, SDL_SCANCODE_ESCAPE));
    ASSERT_TRUE(!search_guard_blocks_keydown(&g, SDL_SCANCODE_RETURN));
    search_guard_keyup(&g, SDL_SCANCODE_ESCAPE);
    ASSERT_TRUE(!search_guard_blocks_keydown(&g, SDL_SCANCODE_ESCAPE));
    /* A zeroed guard blocks nothing, not even the unknown scancode 0. */
    SearchFocusGuard z = {0};
    ASSERT_TRUE(!search_guard_blocks_keydown(&z, 0));
}

static SvRoute route(int32_t sc, uint16_t mod, bool repeat, SvKey* key) {
    *key = SV_KEY_OTHER;
    return search_route_keydown(sc, mod, repeat, key);
}

TEST(search_route_function_keys) {
    SvKey k;
    int32_t pass[] = {SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
                      SDL_SCANCODE_F11, SDL_SCANCODE_F12};
    for (size_t i = 0; i < sizeof(pass) / sizeof(pass[0]); i++) {
        ASSERT_EQ(route(pass[i], 0, false, &k), SV_ROUTE_PASS);
        ASSERT_EQ(route(pass[i], 0, true, &k), SV_ROUTE_PASS);
    }
    int32_t leave[] = {SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F9, SDL_SCANCODE_F10};
    for (size_t i = 0; i < sizeof(leave) / sizeof(leave[0]); i++) {
        ASSERT_EQ(route(leave[i], 0, false, &k), SV_ROUTE_UNFOCUS_PASS);
        ASSERT_EQ(route(leave[i], 0, true, &k), SV_ROUTE_SWALLOW);
    }
    /* Save and load state stay off while typing. */
    ASSERT_EQ(route(SDL_SCANCODE_F5, 0, false, &k), SV_ROUTE_SWALLOW);
    ASSERT_EQ(route(SDL_SCANCODE_F8, 0, false, &k), SV_ROUTE_SWALLOW);
}

TEST(search_route_swallows_hotkeys) {
    SvKey k;
    int32_t eaten[] = {SDL_SCANCODE_SPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_9,
                       SDL_SCANCODE_M, SDL_SCANCODE_H, SDL_SCANCODE_N, SDL_SCANCODE_J, SDL_SCANCODE_B,
                       SDL_SCANCODE_GRAVE, SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_LEFTBRACKET,
                       SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_LEFT,
                       SDL_SCANCODE_RIGHT, SDL_SCANCODE_SLASH};
    for (size_t i = 0; i < sizeof(eaten) / sizeof(eaten[0]); i++) {
        ASSERT_EQ(route(eaten[i], 0, false, &k), SV_ROUTE_SWALLOW);
        ASSERT_EQ(route(eaten[i], 0, true, &k), SV_ROUTE_SWALLOW);
    }
}

TEST(search_route_editing_keys_and_repeat) {
    SvKey k;
    struct { int32_t sc; SvKey key; } rep[] = {
        {SDL_SCANCODE_UP, SV_KEY_UP}, {SDL_SCANCODE_DOWN, SV_KEY_DOWN}, {SDL_SCANCODE_HOME, SV_KEY_HOME},
        {SDL_SCANCODE_END, SV_KEY_END}, {SDL_SCANCODE_PAGEUP, SV_KEY_PAGE_UP},
        {SDL_SCANCODE_PAGEDOWN, SV_KEY_PAGE_DOWN}, {SDL_SCANCODE_BACKSPACE, SV_KEY_BACKSPACE}};
    for (size_t i = 0; i < sizeof(rep) / sizeof(rep[0]); i++) {
        ASSERT_EQ(route(rep[i].sc, 0, false, &k), SV_ROUTE_KEY);
        ASSERT_EQ(k, rep[i].key);
        ASSERT_EQ(route(rep[i].sc, 0, true, &k), SV_ROUTE_KEY);
        ASSERT_EQ(k, rep[i].key);
    }
    /* Enter and Esc act once per press: a held key never mashes clear, unfocus, and more. */
    ASSERT_EQ(route(SDL_SCANCODE_RETURN, 0, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_ENTER);
    ASSERT_EQ(route(SDL_SCANCODE_KP_ENTER, 0, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_ENTER);
    ASSERT_EQ(route(SDL_SCANCODE_RETURN, 0, true, &k), SV_ROUTE_SWALLOW);
    ASSERT_EQ(route(SDL_SCANCODE_ESCAPE, 0, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_ESCAPE);
    ASSERT_EQ(route(SDL_SCANCODE_ESCAPE, 0, true, &k), SV_ROUTE_SWALLOW);
}

TEST(search_route_shortcuts) {
    SvKey k;
    ASSERT_EQ(route(SDL_SCANCODE_BACKSPACE, KMOD_LCTRL, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_CLEAR);
    ASSERT_EQ(route(SDL_SCANCODE_BACKSPACE, KMOD_LGUI, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_CLEAR);
    ASSERT_EQ(route(SDL_SCANCODE_V, KMOD_LCTRL, false, &k), SV_ROUTE_PASTE);
    ASSERT_EQ(route(SDL_SCANCODE_V, KMOD_RGUI, false, &k), SV_ROUTE_PASTE);
    ASSERT_EQ(route(SDL_SCANCODE_V, KMOD_LCTRL, true, &k), SV_ROUTE_SWALLOW);
    ASSERT_EQ(route(SDL_SCANCODE_V, 0, false, &k), SV_ROUTE_SWALLOW);
    /* Ctrl+Alt is AltGr on Windows: a typed character, not a shortcut. */
    ASSERT_EQ(route(SDL_SCANCODE_V, KMOD_LCTRL | KMOD_RALT, false, &k), SV_ROUTE_SWALLOW);
    ASSERT_EQ(route(SDL_SCANCODE_BACKSPACE, KMOD_LCTRL | KMOD_RALT, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_BACKSPACE);
    /* Shift does not change the editing keys. */
    ASSERT_EQ(route(SDL_SCANCODE_DOWN, KMOD_LSHIFT, false, &k), SV_ROUTE_KEY);
    ASSERT_EQ(k, SV_KEY_DOWN);
}

TEST(search_focus_key_rules) {
    ASSERT_TRUE(search_focus_key(SDLK_SLASH, false, true, false));
    ASSERT_TRUE(!search_focus_key(SDLK_SLASH, true, true, false));    /* repeat */
    ASSERT_TRUE(!search_focus_key(SDLK_SLASH, false, false, false));  /* page not shown */
    ASSERT_TRUE(!search_focus_key(SDLK_SLASH, false, true, true));    /* a game key */
    ASSERT_TRUE(!search_focus_key(SDLK_MINUS, false, true, false));
}

void run_search_view_tests(void) {
    RUN_TEST(search_wheel_accumulates_fractions);
    RUN_TEST(search_view_wheel_region_ids);
    RUN_TEST(search_view_init_state);
    RUN_TEST(search_view_layout_fits_panel);
    RUN_TEST(search_view_text_filters_characters);
    RUN_TEST(search_view_text_caps_at_15);
    RUN_TEST(search_view_text_resets_selection);
    RUN_TEST(search_view_backspace_clear_escape_enter);
    RUN_TEST(search_view_selection_clamps_and_stays_visible);
    RUN_TEST(search_view_set_results_clamps);
    RUN_TEST(search_view_detail_paging);
    RUN_TEST(search_view_selection_change_resets_detail);
    RUN_TEST(search_view_detail_clamped_to_range);
    RUN_TEST(search_view_detail_wheel_clamped);
    RUN_TEST(search_view_set_results_keeps_detail_when_sel_same);
    RUN_TEST(search_view_click_hit_testing);
    RUN_TEST(search_view_click_row_respects_scroll_and_count);
    RUN_TEST(search_view_wheel_regions);
    RUN_TEST(search_view_version_bumps_only_on_change);
    RUN_TEST(search_guard_blocks_left_key_until_keyup);
    RUN_TEST(search_guard_escape);
    RUN_TEST(search_route_function_keys);
    RUN_TEST(search_route_swallows_hotkeys);
    RUN_TEST(search_route_editing_keys_and_repeat);
    RUN_TEST(search_route_shortcuts);
    RUN_TEST(search_focus_key_rules);
}
