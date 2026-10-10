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

static int lit_rows(void) {
    int rows = 0;
    for (int r = 0; r < PANEL_CANVAS_H / 8; r++) {
        for (int x = 0; x < PANEL_CANVAS_W; x++) {
            bool hit = false;
            for (int y = r * 8; y < r * 8 + 8; y++)
                if (s_canvas[y * PANEL_CANVAS_W + x] != 0xFF0D0D36) { hit = true; break; }
            if (hit) { rows++; break; }
        }
    }
    return rows;
}

TEST(panel_battle_page_draws_enemy_and_catch_rows) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = true;
    snap.context = GAME_CTX_BATTLE_WILD;
    snap.enemy_count = 1;
    game_strcpy(snap.enemies[0].species, GAME_NAME_LEN, "ZIGZAGOON");
    snap.enemies[0].eff_count = 1;
    game_strcpy(snap.enemies[0].eff[0].move, GAME_NAME_LEN, "TACKLE");
    snap.enemies[0].eff[0].quarters = 4;
    snap.catch_count = 1;
    game_strcpy(snap.catch_rows[0].ball, GAME_NAME_LEN, "POKE BALL");
    snap.catch_rows[0].permille = 62;
    game_panel_render(s_canvas, &snap, GAME_PAGE_AUTO);
    ASSERT_TRUE(lit_rows() >= 8);
}

TEST(panel_full_party_and_encounters_stay_in_bounds) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = true;
    snap.context = GAME_CTX_OVERWORLD;
    snap.party_count = 6;
    for (int i = 0; i < 6; i++) {
        snap.party[i].present = true;
        game_strcpy(snap.party[i].species, GAME_NAME_LEN, "WWWWWWWWWW");
        game_strcpy(snap.party[i].nickname, GAME_NAME_LEN, "WWWWWWWWWW");
    }
    snap.has_encounters = true;
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        snap.enc_count[k] = 12;
        for (int i = 0; i < 12; i++) game_strcpy(snap.enc[k][i].species, GAME_NAME_LEN, "WWWWWWWWWW");
    }
    game_panel_render(s_canvas, &snap, GAME_PAGE_PARTY);
    ASSERT_TRUE(lit_rows() >= 30);
    game_panel_render(s_canvas, &snap, GAME_PAGE_ENCOUNTERS);
    ASSERT_TRUE(lit_rows() >= 30);
}

void run_game_panel_tests(void) {
    TEST_SUITE("game_panel");
    RUN_TEST(panel_logical_size);
    RUN_TEST(panel_page_cycles);
    RUN_TEST(panel_auto_page_follows_context);
    RUN_TEST(panel_renders_reason_for_invalid_snapshot);
    RUN_TEST(panel_battle_page_draws_enemy_and_catch_rows);
    RUN_TEST(panel_full_party_and_encounters_stay_in_bounds);
}
