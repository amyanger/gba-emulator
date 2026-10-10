#include "test_harness.h"
#include "frontend/settings.h"
#include "cartridge/cartridge.h"
#include "screenshot/screenshot.h"

#include <stdio.h>
#include <stdlib.h>

#define SW 1100
#define SH 700
static uint32_t s_px[SW * SH];
static UiCanvas cv;
static UiInput in;
static Settings st;
static CheatEngine eng;
static Cartridge cart;
static SettingsContext ctx;
static int toasts;
static char last_toast[64];

static void on_toast(void* user, ToastKind kind, const char* title, const char* detail) {
    (void)user; (void)kind; (void)detail;
    toasts++;
    snprintf(last_toast, sizeof(last_toast), "%s", title);
}

static void setup(void) {
    memset(&in, 0, sizeof(in));
    memset(&cart, 0, sizeof(cart));
    cart.save_type = SAVE_NONE;
    cheat_init(&eng);
    cheat_set_game_code(&eng, 0x45455042u);
    ctx = (SettingsContext){&eng, &cart, "gba_settings_test.cht", on_toast, NULL};
    toasts = 0;
    last_toast[0] = '\0';
    remove(ctx.cht_path);
    settings_init(&st);
    settings_open(&st);
}

static void draw(void) {
    ui_canvas_init(&cv, s_px, SW, SH, 1.0f);
    ui_input_frame_begin(&in);
    settings_draw(&cv, &in, &st, &ctx);
    ui_input_frame_end(&in);
}

static void type_into(uint32_t id, const char* text) {
    in.focus = id;
    ui_input_text(&in, text);
    draw();
}

static void press_key_on(uint32_t id, UiKey k) {
    in.focus = id;
    ui_input_key(&in, k);
    draw();
}

TEST(settings_window_centered_and_shrinks) {
    UiRect r = settings_window_rect(1000, 600);
    ASSERT_EQ((int)r.w, 640);
    ASSERT_EQ((int)r.x, 180);
    ASSERT_EQ((int)r.y, 80);
    r = settings_window_rect(500, 300);
    ASSERT_TRUE(r.w <= 500 - 16 && r.h <= 300 - 16 && r.x >= 8 && r.y >= 8);
}

TEST(settings_density_keeps_window_inside_and_text_readable) {
    ASSERT_TRUE(settings_density(1440, 960, 2.0f) == 2.0f);       /* plenty of room */
    float d = settings_density(720, 480, 2.0f);                   /* 360x240 pt at 2x: too small */
    ASSERT_TRUE(d < 2.0f && d >= SETTINGS_MIN_DENSITY);
    ASSERT_TRUE(settings_density(200, 150, 1.0f) == SETTINGS_MIN_DENSITY);
}

TEST(settings_point_mapping_retina) {
    /* Window unit (300, 200) on a 2x drawable with density 2 is point (300, 200). */
    float x, y;
    settings_point_from_window(300, 200, 2.0f, 2.0f, &x, &y);
    ASSERT_TRUE(x == 300.0f && y == 200.0f);
    /* Same click with a density squeezed to 1.5 lands further right in points. */
    settings_point_from_window(300, 200, 2.0f, 1.5f, &x, &y);
    ASSERT_TRUE(x == 400.0f && y > 266.6f && y < 266.7f);
}

TEST(settings_cheats_status_for_good_bad_and_unsure) {
    setup();
    draw();
    type_into(SETTINGS_ID_PASTE, "Max Money 999999\n82025838 03E7\n");
    ASSERT_EQ(st.cheats.status_kind, SETTINGS_STATUS_OK);
    ASSERT_EQ(st.cheats.detected, CHEAT_DEV_CODEBREAKER);   /* shown in the chip */
    ASSERT_STR_EQ(st.cheats.name_buf, "Max Money 999999");   /* filled from the label */

    press_key_on(SETTINGS_ID_PASTE, UI_KEY_SELECT_ALL);
    type_into(SETTINGS_ID_PASTE, "82025838 3E7\n");
    ASSERT_EQ(st.cheats.status_kind, SETTINGS_STATUS_ERROR);
    ASSERT_TRUE(strstr(st.cheats.status, "Line 1") != NULL);

    press_key_on(SETTINGS_ID_PASTE, UI_KEY_SELECT_ALL);
    type_into(SETTINGS_ID_PASTE, "FFFFFFFF FFFFFFFF\n");
    ASSERT_EQ(st.cheats.status_kind, SETTINGS_STATUS_UNSURE);
}

TEST(settings_cheats_add_saves_and_clears) {
    setup();
    draw();
    type_into(SETTINGS_ID_PASTE, "Level 100\n32024540 0064\n");
    press_key_on(SETTINGS_ID_ADD, UI_KEY_ENTER);
    ASSERT_EQ(eng.cheat_count, 1);
    ASSERT_TRUE(eng.cheats[0].enabled);
    ASSERT_STR_EQ(eng.cheats[0].name, "Level 100");
    ASSERT_STR_EQ(st.cheats.paste_buf, "");
    ASSERT_STR_EQ(last_toast, "Cheat added");
    FILE* f = fopen(ctx.cht_path, "r");
    ASSERT_TRUE(f != NULL);
    fclose(f);
    remove(ctx.cht_path);
}

TEST(settings_cheats_add_disabled_when_unsure_or_bad) {
    setup();
    draw();
    type_into(SETTINGS_ID_PASTE, "FFFFFFFF FFFFFFFF\n");
    press_key_on(SETTINGS_ID_ADD, UI_KEY_ENTER);
    ASSERT_EQ(eng.cheat_count, 0);
}

/* The frame that receives a paste must already show Add enabled and the name. */
TEST(settings_cheats_paste_updates_same_frame) {
    setup();
    draw();
    in.focus = SETTINGS_ID_PASTE;
    ui_input_text(&in, "Infinite Rare Candy\n82025838 03E7\n");
    ui_input_key(&in, UI_KEY_TAB);   /* moves focus to the name box at frame end */
    ui_input_frame_begin(&in);
    settings_draw(&cv, &in, &st, &ctx);
    ASSERT_STR_EQ(st.cheats.name_buf, "Infinite Rare Candy");
    ASSERT_EQ(in.order_count > 0, 1);
    bool add_in_order = false;
    for (uint32_t i = 0; i < in.order_count; i++) add_in_order |= in.order[i] == SETTINGS_ID_ADD;
    ASSERT_TRUE(add_in_order);   /* enabled buttons join the Tab order */
    ui_input_frame_end(&in);
}

TEST(settings_cheats_name_edit_sticks) {
    setup();
    draw();
    type_into(SETTINGS_ID_NAME, "My name");
    type_into(SETTINGS_ID_PASTE, "Label\n82025838 03E7\n");
    ASSERT_STR_EQ(st.cheats.name_buf, "My name");
}

TEST(settings_cheats_delete_needs_two_clicks) {
    setup();
    char err[128];
    cheat_add_text(&eng, "82025838 03E7\n", "a", CHEAT_DEV_AUTO, err, sizeof(err));
    draw();
    press_key_on(settings_id_delete(0), UI_KEY_ENTER);
    ASSERT_EQ(eng.cheat_count, 1);
    ASSERT_EQ(st.cheats.confirm_delete, 0);
    press_key_on(settings_id_delete(0), UI_KEY_ENTER);
    ASSERT_EQ(eng.cheat_count, 0);
    remove(ctx.cht_path);
}

TEST(settings_cheats_toggle_and_broken_row) {
    setup();
    char err[128];
    cheat_add_text(&eng, "82025838 03E7\n", "a", CHEAT_DEV_AUTO, err, sizeof(err));
    CheatRawList raw = {0};
    raw.lines[0] = (CheatRawLine){0x88000000u, 0x0001u, 4};
    raw.count = 1;
    cheat_add_raw(&eng, "broken", &raw, CHEAT_DEV_CODEBREAKER, true, false);
    draw();
    press_key_on(settings_id_toggle(0), UI_KEY_SPACE);
    ASSERT_TRUE(!eng.cheats[0].enabled);
    press_key_on(settings_id_toggle(0), UI_KEY_SPACE);
    ASSERT_TRUE(eng.cheats[0].enabled);
    press_key_on(settings_id_toggle(1), UI_KEY_SPACE);
    ASSERT_TRUE(!eng.cheats[1].enabled);
    remove(ctx.cht_path);
}

TEST(settings_escape_and_outside_click_close) {
    setup();
    draw();
    ui_input_key(&in, UI_KEY_ESCAPE);
    draw();
    ASSERT_TRUE(st.close_requested);
    setup();
    draw();
    in.mx = 5; in.my = 5; in.down = true; in.pressed = true;
    draw();
    ASSERT_TRUE(st.close_requested);
}

TEST(settings_full_list_disables_add) {
    setup();
    char err[128];
    for (int i = 0; i < CHEAT_MAX_CHEATS; i++) cheat_add_text(&eng, "82025838 03E7\n", "x", CHEAT_DEV_AUTO, err, sizeof(err));
    draw();
    type_into(SETTINGS_ID_PASTE, "82025840 0001\n");
    ASSERT_TRUE(strstr(st.cheats.status, "full") != NULL);
    press_key_on(SETTINGS_ID_ADD, UI_KEY_ENTER);
    ASSERT_EQ(eng.cheat_count, CHEAT_MAX_CHEATS);
}

/* SETTINGS_PREVIEW=1 writes settings_preview.png for a visual check. */
TEST(settings_preview_png) {
    if (!getenv("SETTINGS_PREVIEW")) return;
    setup();
    char err[128];
    cheat_add_text(&eng, "32024540 0064\n", "Level 100 (first Pokemon)", CHEAT_DEV_AUTO, err, sizeof(err));
    cheat_add_text(&eng, "82025838 03E7\n", "Max Money 999999", CHEAT_DEV_AUTO, err, sizeof(err));
    eng.cheats[1].enabled = false;
    CheatRawList raw = {0};
    raw.lines[0] = (CheatRawLine){0x88000000u, 0x0001u, 4};
    raw.count = 1;
    cheat_add_raw(&eng, "Walk through walls", &raw, CHEAT_DEV_CODEBREAKER, true, false);
    draw();
    type_into(SETTINGS_ID_PASTE, "Infinite Rare Candy\n82025838 03E7\n");
    uint32_t* out = (uint32_t*)malloc((size_t)SW * SH * 4);
    ASSERT_TRUE(out != NULL);
    for (int i = 0; i < SW * SH; i++) {   /* over a mid gray stand-in for the game */
        uint32_t s = s_px[i], a = s >> 24, bg = 0xFF5F7F6Fu, o = 0xFF000000u;
        for (int sh = 0; sh < 24; sh += 8)
            o |= ((((s >> sh) & 0xFF) * a + ((bg >> sh) & 0xFF) * (255 - a) + 127) / 255) << sh;
        out[i] = o;
    }
    bool ok = screenshot_save_argb(out, SW, SH, "settings_preview.png");
    free(out);
    remove(ctx.cht_path);
    ASSERT_TRUE(ok);
}

void run_settings_tests(void) {
    TEST_SUITE("settings");
    RUN_TEST(settings_window_centered_and_shrinks);
    RUN_TEST(settings_density_keeps_window_inside_and_text_readable);
    RUN_TEST(settings_point_mapping_retina);
    RUN_TEST(settings_cheats_status_for_good_bad_and_unsure);
    RUN_TEST(settings_cheats_add_saves_and_clears);
    RUN_TEST(settings_cheats_add_disabled_when_unsure_or_bad);
    RUN_TEST(settings_cheats_paste_updates_same_frame);
    RUN_TEST(settings_cheats_name_edit_sticks);
    RUN_TEST(settings_cheats_delete_needs_two_clicks);
    RUN_TEST(settings_cheats_toggle_and_broken_row);
    RUN_TEST(settings_escape_and_outside_click_close);
    RUN_TEST(settings_full_list_disables_add);
    RUN_TEST(settings_preview_png);
}
