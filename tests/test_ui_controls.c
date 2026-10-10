#include "test_harness.h"
#include "ui/ui_controls.h"

#define CW 400
#define CH 300
static uint32_t s_px[CW * CH];
static UiCanvas cv;
static UiInput in;

static void frame_begin(void) {
    ui_canvas_init(&cv, s_px, CW, CH, 1.0f);
    cv.keep_alpha = true;
    ui_canvas_clear(&cv, 0);
    ui_input_frame_begin(&in);
}

static void frame_end(void) { ui_input_frame_end(&in); }

static void mouse(float x, float y, bool down) {
    in.mx = x;
    in.my = y;
    if (down && !in.down) in.pressed = true;
    if (!down && in.down) in.released = true;
    in.down = down;
}

static const UiRect R = {10, 10, 80, 24};

static bool run_button(void) {
    frame_begin();
    bool c = ui_button(&cv, &in, ui_id("b", 0), R, "Add", true, true);
    frame_end();
    return c;
}

TEST(ui_button_click_needs_press_and_release_inside) {
    memset(&in, 0, sizeof(in));
    mouse(20, 20, true);
    ASSERT_TRUE(!run_button());      /* press */
    mouse(20, 20, false);
    ASSERT_TRUE(run_button());       /* release inside -> click */
    mouse(20, 20, true);
    run_button();
    mouse(200, 200, false);
    ASSERT_TRUE(!run_button());      /* released outside -> no click */
}

TEST(ui_button_disabled_never_clicks) {
    memset(&in, 0, sizeof(in));
    for (int k = 0; k < 2; k++) {
        mouse(20, 20, k == 0);
        frame_begin();
        ASSERT_TRUE(!ui_button(&cv, &in, ui_id("b", 0), R, "Add", false, true));
        frame_end();
    }
}

TEST(ui_toggle_flips_on_click_and_space) {
    memset(&in, 0, sizeof(in));
    bool on = false;
    mouse(20, 20, true);
    frame_begin(); ui_toggle(&cv, &in, ui_id("t", 0), R, &on, true); frame_end();
    mouse(20, 20, false);
    frame_begin(); ASSERT_TRUE(ui_toggle(&cv, &in, ui_id("t", 0), R, &on, true)); frame_end();
    ASSERT_TRUE(on);
    ASSERT_EQ(in.focus, ui_id("t", 0));
    ui_input_key(&in, UI_KEY_SPACE);
    frame_begin(); ASSERT_TRUE(ui_toggle(&cv, &in, ui_id("t", 0), R, &on, true)); frame_end();
    ASSERT_TRUE(!on);
}

TEST(ui_tab_moves_focus_in_call_order) {
    memset(&in, 0, sizeof(in));
    bool a = false, b = false;
    for (int k = 0; k < 4; k++) {
        if (k > 0) ui_input_key(&in, UI_KEY_TAB);
        frame_begin();
        ui_toggle(&cv, &in, ui_id("a", 0), (UiRect){0, 0, 30, 20}, &a, true);
        ui_toggle(&cv, &in, ui_id("b", 0), (UiRect){0, 30, 30, 20}, &b, true);
        frame_end();
    }
    /* Tab x3 from nothing: a, b, a */
    ASSERT_EQ(in.focus, ui_id("a", 0));
    ui_input_key(&in, UI_KEY_SHIFT_TAB);
    frame_begin();
    ui_toggle(&cv, &in, ui_id("a", 0), (UiRect){0, 0, 30, 20}, &a, true);
    ui_toggle(&cv, &in, ui_id("b", 0), (UiRect){0, 30, 30, 20}, &b, true);
    frame_end();
    ASSERT_EQ(in.focus, ui_id("b", 0));
}

static char buf[256];
static UiTextArea ta;

static void run_text(bool multiline) {
    frame_begin();
    ui_text_area(&cv, &in, ui_id("ta", 0), (UiRect){10, 10, 300, 100}, &ta, "Paste here", multiline);
    frame_end();
}

static void text_setup(void) {
    memset(&in, 0, sizeof(in));
    ui_text_area_init(&ta, buf, sizeof(buf));
    in.focus = ui_id("ta", 0);
}

TEST(ui_text_area_typing_backspace_and_arrows) {
    text_setup();
    ui_input_text(&in, "abc");
    run_text(true);
    ASSERT_STR_EQ(buf, "abc");
    ui_input_key(&in, UI_KEY_LEFT);
    ui_input_key(&in, UI_KEY_BACKSPACE);
    run_text(true);
    ASSERT_STR_EQ(buf, "ac");
    ui_input_text(&in, "X");
    run_text(true);
    ASSERT_STR_EQ(buf, "aXc");
}

TEST(ui_text_area_paste_normalizes_newlines) {
    text_setup();
    ui_input_text(&in, "82025838 03E7\r\n82025840 0001\r");
    run_text(true);
    ASSERT_STR_EQ(buf, "82025838 03E7\n82025840 0001\n");
}

TEST(ui_text_area_single_line_turns_newlines_into_spaces) {
    text_setup();
    ui_input_text(&in, "Max\nMoney");
    run_text(false);
    ASSERT_STR_EQ(buf, "Max Money");
}

TEST(ui_text_area_select_all_then_type_replaces) {
    text_setup();
    ui_input_text(&in, "old text");
    run_text(true);
    ui_input_key(&in, UI_KEY_SELECT_ALL);
    run_text(true);
    ui_input_text(&in, "new");
    run_text(true);
    ASSERT_STR_EQ(buf, "new");
}

TEST(ui_text_area_utf8_caret_steps_whole_characters) {
    text_setup();
    ui_input_text(&in, "Pok\xC3\xA9mon");   /* é is 2 bytes */
    run_text(true);
    for (int i = 0; i < 3; i++) ui_input_key(&in, UI_KEY_LEFT);
    ui_input_key(&in, UI_KEY_BACKSPACE);
    run_text(true);
    ASSERT_STR_EQ(buf, "Pokmon");
}

TEST(ui_text_area_respects_capacity) {
    text_setup();
    static char big[600];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    ui_input_text(&in, big);
    run_text(true);
    ASSERT_EQ(strlen(buf), sizeof(buf) - 1);
}

TEST(ui_scroll_clamps) {
    memset(&in, 0, sizeof(in));
    float scroll = 0;
    in.mx = 20; in.my = 20; in.wheel = -100;   /* scroll down a lot */
    ui_scroll(&in, (UiRect){0, 0, 100, 100}, 250, &scroll);
    ASSERT_TRUE(scroll > 149.9f && scroll < 150.1f);   /* content 250 - view 100 */
    in.wheel = 100;
    ui_scroll(&in, (UiRect){0, 0, 100, 100}, 250, &scroll);
    ASSERT_TRUE(scroll == 0.0f);
}

TEST(ui_popup_blocks_clicks_underneath) {
    memset(&in, 0, sizeof(in));
    in.popup_open = true;
    in.popup_rect = (UiRect){0, 0, 200, 200};
    mouse(20, 20, true);
    run_button();
    mouse(20, 20, false);
    ASSERT_TRUE(!run_button());
}

/* Trackpad tap: press and release arrive in the same frame as the chip click
 * that opens the pop-up; the pop-up must not treat that as an outside click. */
TEST(ui_popup_survives_tap_that_opened_it) {
    static const char* const items[] = {"one", "two", "three"};
    memset(&in, 0, sizeof(in));
    in.mx = 20; in.my = 20; in.down = false; in.pressed = true; in.released = true;
    frame_begin();
    if (ui_chip(&cv, &in, ui_id("chip", 0), R, "Pick", 0xFFFFFFFFu)) in.popup_open = true;
    int32_t r = in.popup_open ? ui_popup_list(&cv, &in, R, items, 3, -1) : UI_POPUP_NONE;
    frame_end();
    ASSERT_TRUE(in.popup_open);
    ASSERT_EQ(r, UI_POPUP_NONE);
}

/* Near the bottom of the canvas the list opens upward instead of off-screen. */
TEST(ui_popup_stays_on_canvas) {
    static const char* const items[] = {"a", "b", "c", "d", "e"};
    memset(&in, 0, sizeof(in));
    in.popup_open = true;
    frame_begin();
    ui_popup_list(&cv, &in, (UiRect){10, CH - 30, 80, 20}, items, 5, -1);
    frame_end();
    ASSERT_TRUE(in.popup_rect.y >= 0.0f);
    ASSERT_TRUE(in.popup_rect.y + in.popup_rect.h <= (float)CH);
}

/* While a pop-up is open, keys must not activate widgets underneath it. */
TEST(ui_keys_blocked_while_popup_open) {
    memset(&in, 0, sizeof(in));
    in.popup_open = true;
    in.focus = ui_id("b", 0);
    ui_input_key(&in, UI_KEY_ENTER);
    ASSERT_TRUE(!run_button());
}

void run_ui_controls_tests(void) {
    TEST_SUITE("ui_controls");
    RUN_TEST(ui_button_click_needs_press_and_release_inside);
    RUN_TEST(ui_button_disabled_never_clicks);
    RUN_TEST(ui_toggle_flips_on_click_and_space);
    RUN_TEST(ui_tab_moves_focus_in_call_order);
    RUN_TEST(ui_text_area_typing_backspace_and_arrows);
    RUN_TEST(ui_text_area_paste_normalizes_newlines);
    RUN_TEST(ui_text_area_single_line_turns_newlines_into_spaces);
    RUN_TEST(ui_text_area_select_all_then_type_replaces);
    RUN_TEST(ui_text_area_utf8_caret_steps_whole_characters);
    RUN_TEST(ui_text_area_respects_capacity);
    RUN_TEST(ui_scroll_clamps);
    RUN_TEST(ui_popup_blocks_clicks_underneath);
    RUN_TEST(ui_popup_survives_tap_that_opened_it);
    RUN_TEST(ui_popup_stays_on_canvas);
    RUN_TEST(ui_keys_blocked_while_popup_open);
}
