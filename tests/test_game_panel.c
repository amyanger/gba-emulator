#include "test_harness.h"
#include "frontend/frontend.h"
#include "frontend/game_panel.h"

static uint32_t s_canvas[PANEL_CANVAS_W * PANEL_CANVAS_H];

TEST(panel_logical_size) {
    int w, h;
    frontend_logical_size(false, &w, &h);
    ASSERT_EQ(w, 240); ASSERT_EQ(h, 160);
    frontend_logical_size(true, &w, &h);
    ASSERT_EQ(w, 440); ASSERT_EQ(h, 160);
}

TEST(panel_page_cycles) {
    ASSERT_EQ(game_panel_next_page(GAME_PAGE_AUTO), GAME_PAGE_PARTY);
    ASSERT_EQ(game_panel_next_page(GAME_PAGE_PARTY), GAME_PAGE_ENCOUNTERS);
    ASSERT_EQ(game_panel_next_page(GAME_PAGE_ENCOUNTERS), GAME_PAGE_AUTO);
}

TEST(panel_auto_page_follows_context) {
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_AUTO, GAME_CTX_BATTLE_WILD), GAME_PAGE_COUNT);
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_AUTO, GAME_CTX_BATTLE_TRAINER), GAME_PAGE_COUNT);
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_AUTO, GAME_CTX_OVERWORLD), GAME_PAGE_ENCOUNTERS);
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_PARTY, GAME_CTX_BATTLE_WILD), GAME_PAGE_PARTY);
}

TEST(panel_renders_reason_for_invalid_snapshot) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    game_strcpy(snap.reason, sizeof(snap.reason), "No game info for this ROM");
    memset(s_canvas, 0, sizeof(s_canvas));
    game_panel_render(s_canvas, &snap, GAME_PAGE_AUTO);
    int lit = 0;
    for (int i = 0; i < PANEL_CANVAS_W * PANEL_CANVAS_H; i++)
        if (s_canvas[i] != 0) lit++;
    ASSERT_TRUE(lit > 0);
    ASSERT_EQ(s_canvas[PANEL_CANVAS_W * PANEL_CANVAS_H - 1] >> 24, 0xFF);  /* opaque background */
}

void run_game_panel_tests(void) {
    TEST_SUITE("game_panel");
    RUN_TEST(panel_logical_size);
    RUN_TEST(panel_page_cycles);
    RUN_TEST(panel_auto_page_follows_context);
    RUN_TEST(panel_renders_reason_for_invalid_snapshot);
}
