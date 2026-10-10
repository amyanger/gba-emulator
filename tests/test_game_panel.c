#include "test_harness.h"
#include "frontend/game_panel.h"
#include "ui/ui_canvas.h"
#include "ui/ui_theme.h"

#define BUF_W 1800
#define BUF_H 1440
#define GUARD 64
#define SENTINEL 0x12345678u

static uint32_t s_buf[BUF_W * BUF_H + GUARD];

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

TEST(panel_title_case) {
    char out[GAME_NAME_LEN];
    game_panel_title_case("POK\xC3\xA9 BALL", out, sizeof(out));
    ASSERT_STR_EQ(out, "Pok\xC3\xA9 Ball");
    game_panel_title_case("WATER GUN", out, sizeof(out));
    ASSERT_STR_EQ(out, "Water Gun");
    game_panel_title_case("MR. MIME", out, sizeof(out));
    ASSERT_STR_EQ(out, "Mr. Mime");
    game_panel_title_case("HO-OH", out, sizeof(out));
    ASSERT_STR_EQ(out, "Ho-Oh");
    game_panel_title_case("", out, sizeof(out));
    ASSERT_STR_EQ(out, "");
    /* Truncation never splits a UTF-8 sequence. */
    char small[5];
    game_panel_title_case("POK\xC3\xA9", small, sizeof(small));
    ASSERT_STR_EQ(small, "Pok");
    /* The ROM's "POKe BALL" decodes as "POKE BALL"; whole-word POKE, HP and PP
     * are fixed up, other words containing them are not. */
    game_panel_title_case("POKE BALL", out, sizeof(out));
    ASSERT_STR_EQ(out, "Pok\xC3\xA9 Ball");
    game_panel_title_case("HP UP", out, sizeof(out));
    ASSERT_STR_EQ(out, "HP Up");
    game_panel_title_case("PP MAX", out, sizeof(out));
    ASSERT_STR_EQ(out, "PP Max");
    game_panel_title_case("POKEMON", out, sizeof(out));
    ASSERT_STR_EQ(out, "Pokemon");
    game_panel_title_case("POKEBLOCK", out, sizeof(out));
    ASSERT_STR_EQ(out, "Pokeblock");
    game_panel_title_case("SHARP BEAK", out, sizeof(out));
    ASSERT_STR_EQ(out, "Sharp Beak");
    /* No room for the extra UTF-8 byte: keep the plain spelling. */
    game_panel_title_case("POKE", small, sizeof(small));
    ASSERT_STR_EQ(small, "Poke");
}

TEST(panel_select_wraps) {
    ASSERT_EQ(game_panel_select(0, -1, 6), 5);
    ASSERT_EQ(game_panel_select(5, 1, 6), 0);
    ASSERT_EQ(game_panel_select(2, 1, 6), 3);
    ASSERT_EQ(game_panel_select(3, 1, 0), 0);
    ASSERT_EQ(game_panel_select(9, 0, 3), 2);
}

static void fill_battle(GameSnapshot* s, GameContext ctx) {
    memset(s, 0, sizeof(*s));
    s->valid = true;
    s->context = ctx;
    game_strcpy(s->active_name, GAME_NAME_LEN, "WWWWWWWWWW");
    s->enemy_count = 2;
    for (int i = 0; i < 2; i++) {
        GameEnemy* e = &s->enemies[i];
        game_strcpy(e->species, GAME_NAME_LEN, "WWWWWWWWWW");
        game_strcpy(e->nature, GAME_NAME_LEN, "HASTY");
        e->nature_up = 2; e->nature_down = 1;
        game_strcpy(e->ability, GAME_NAME_LEN, "WWWWWWWWWWWW");
        game_strcpy(e->item, GAME_NAME_LEN, "BRIGHTPOWDER");
        e->level = 100; e->hp = 3; e->max_hp = 300;
        e->gender = MON_GENDER_FEMALE;
        e->status1 = 0x80;
        e->type_ids[0] = 13; e->type_ids[1] = 16;
        for (int k = 0; k < 6; k++) e->ivs[k] = (uint8_t)(k * 6);
        e->eff_count = 4;
        for (int m = 0; m < 4; m++) {
            game_strcpy(e->eff[m].move, GAME_NAME_LEN, "SUPERSONICXXX");
            e->eff[m].type = 13;
            e->eff[m].quarters = (uint8_t)(1u << m);
        }
        e->eff[3].status_move = true;
    }
    s->catch_count = 3;
    for (int i = 0; i < 3; i++) {
        game_strcpy(s->catch_rows[i].ball, GAME_NAME_LEN, "WWWWWWWWWWWW");
        s->catch_rows[i].quantity = 999;
        s->catch_rows[i].permille = (int16_t)(i == 2 ? -1 : 1000);
    }
}

static void fill_party_and_route(GameSnapshot* s) {
    memset(s, 0, sizeof(*s));
    s->valid = true;
    s->context = GAME_CTX_OVERWORLD;
    s->party_count = 6;
    for (int i = 0; i < 6; i++) {
        GamePartyMon* m = &s->party[i];
        m->present = true;
        game_strcpy(m->species, GAME_NAME_LEN, "WWWWWWWWWW");
        game_strcpy(m->nickname, GAME_NAME_LEN, "WWWWWWWWWW");
        game_strcpy(m->nature, GAME_NAME_LEN, "WWWWWWW");
        game_strcpy(m->ability, GAME_NAME_LEN, "WWWWWWWWWWWW");
        game_strcpy(m->item, GAME_NAME_LEN, "WWWWWWWWWWWW");
        game_strcpy(m->hp_type, GAME_NAME_LEN, "PSYCHIC");
        m->level = 100; m->hp = 10; m->max_hp = 300;
        m->type_ids[0] = 1; m->type_ids[1] = 13;
        for (int k = 0; k < 6; k++) { m->ivs[k] = 31; m->evs[k] = 255; }
    }
    s->party[4].egg = true;
    s->party[5].bad = true;
    s->has_encounters = true;
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        s->enc_count[k] = 12;
        for (int i = 0; i < 12; i++) {
            game_strcpy(s->enc[k][i].species, GAME_NAME_LEN, "WWWWWWWWWW");
            s->enc[k][i].min_level = 10; s->enc[k][i].max_level = 100;
            s->enc[k][i].percent = 100;
            s->enc[k][i].caught = (i & 1) != 0;
        }
    }
}

static bool draw_checked(float density, const GameSnapshot* snap, GamePage page, uint8_t sel) {
    int w = (int)(600 * density + 0.5f), h = (int)(480 * density + 0.5f);
    for (int i = 0; i < GUARD; i++) s_buf[w * h + i] = SENTINEL;
    UiCanvas c;
    ui_canvas_init(&c, s_buf, w, h, density);
    game_panel_draw(&c, snap, page, sel);
    for (int i = 0; i < GUARD; i++)
        if (s_buf[w * h + i] != SENTINEL) return false;
    return (s_buf[w * h - 1] >> 24) == 0xFF;
}

TEST(panel_pages_stay_in_bounds_at_all_densities) {
    static const float densities[] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f};
    static GameSnapshot battle, rest;
    fill_battle(&battle, GAME_CTX_BATTLE_WILD);
    fill_party_and_route(&rest);
    for (size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); d++) {
        ASSERT_TRUE(draw_checked(densities[d], &battle, GAME_PAGE_AUTO, 0));
        for (uint8_t sel = 0; sel < 6; sel++)
            ASSERT_TRUE(draw_checked(densities[d], &rest, GAME_PAGE_PARTY, sel));
        ASSERT_TRUE(draw_checked(densities[d], &rest, GAME_PAGE_ENCOUNTERS, 0));
        ASSERT_TRUE(draw_checked(densities[d], &rest, GAME_PAGE_AUTO, 0));
    }
}

TEST(panel_long_names_stay_in_columns) {
    static GameSnapshot snap;
    fill_battle(&snap, GAME_CTX_BATTLE_TRAINER);
    snap.enemy_count = 1;
    UiCanvas c;
    ui_canvas_init(&c, s_buf, 600, 480, 1.0f);
    game_panel_draw(&c, &snap, GAME_PAGE_AUTO, 0);
    /* Rows between the HP bar and the bottom of the move list; the gap between
     * the left column (ends at x 292) and the right one (starts at x 308),
     * minus a pixel either side for anti-aliasing. */
    int lit = 0;
    for (int y = 100; y < 300; y++)
        for (int x = 294; x < 306; x++)
            if (s_buf[y * 600 + x] != UI_BG) lit++;
    ASSERT_EQ(lit, 0);
}

TEST(panel_invalid_snapshot_shows_reason) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    game_strcpy(snap.reason, sizeof(snap.reason), "No game info for this ROM");
    UiCanvas c;
    ui_canvas_init(&c, s_buf, 600, 480, 1.0f);
    game_panel_draw(&c, &snap, GAME_PAGE_AUTO, 0);
    int lit = 0;
    for (int y = 220; y < 240; y++)
        for (int x = 0; x < 600; x++)
            if (s_buf[y * 600 + x] != UI_BG) lit++;
    ASSERT_TRUE(lit > 0);
}

static void fill_route(GameSnapshot* s, const uint8_t counts[ENC_METHOD_COUNT]) {
    memset(s, 0, sizeof(*s));
    s->valid = true;
    s->has_encounters = true;
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        s->enc_count[k] = counts[k];
        for (int i = 0; i < counts[k]; i++) {
            game_strcpy(s->enc[k][i].species, GAME_NAME_LEN, "ZIGZAGOON");
            s->enc[k][i].min_level = 5; s->enc[k][i].max_level = 10;
            s->enc[k][i].percent = 20;
        }
    }
}

static int layout_total(const RouteLayout* l) {
    int n = l->more[0] + l->more[1];
    for (int k = 0; k < ENC_METHOD_COUNT; k++) n += l->shown[k];
    return n;
}

TEST(panel_route_busy_water_route_shows_every_row) {
    static GameSnapshot snap;
    static const uint8_t counts[ENC_METHOD_COUNT] = {6, 3, 0, 2, 3, 5};
    fill_route(&snap, counts);
    RouteLayout l;
    game_panel_route_layout(&snap, &l);
    for (int k = 0; k < ENC_METHOD_COUNT; k++) ASSERT_EQ(l.shown[k], counts[k]);
    ASSERT_EQ(l.more[0] + l.more[1], 0);
    ASSERT_TRUE(l.height[0] <= l.avail && l.height[1] <= l.avail);
}

TEST(panel_route_short_list_keeps_roomy_single_column) {
    static GameSnapshot snap;
    static const uint8_t counts[ENC_METHOD_COUNT] = {3, 0, 0, 2, 0, 0};
    fill_route(&snap, counts);
    RouteLayout l;
    game_panel_route_layout(&snap, &l);
    ASSERT_TRUE(!l.two_columns);
    ASSERT_TRUE(l.row_h == 19.0f);
    ASSERT_EQ(layout_total(&l), 5);
    ASSERT_EQ(l.more[0] + l.more[1], 0);
}

TEST(panel_route_overflow_keeps_whole_rows_and_counts_the_rest) {
    static GameSnapshot snap;
    static const uint8_t counts[ENC_METHOD_COUNT] = {12, 12, 12, 12, 12, 12};
    fill_route(&snap, counts);
    RouteLayout l;
    game_panel_route_layout(&snap, &l);
    ASSERT_TRUE(l.more[0] + l.more[1] > 0);
    ASSERT_EQ(layout_total(&l), 72);
    ASSERT_TRUE(l.height[0] <= l.avail && l.height[1] <= l.avail);
    UiCanvas c;
    ui_canvas_init(&c, s_buf, 600, 480, 1.0f);
    game_panel_draw(&c, &snap, GAME_PAGE_ENCOUNTERS, 0);
}

void run_game_panel_tests(void) {
    TEST_SUITE("game_panel");
    RUN_TEST(panel_page_cycles);
    RUN_TEST(panel_auto_page_follows_context);
    RUN_TEST(panel_title_case);
    RUN_TEST(panel_select_wraps);
    RUN_TEST(panel_pages_stay_in_bounds_at_all_densities);
    RUN_TEST(panel_long_names_stay_in_columns);
    RUN_TEST(panel_invalid_snapshot_shows_reason);
    RUN_TEST(panel_route_busy_water_route_shows_every_row);
    RUN_TEST(panel_route_short_list_keeps_roomy_single_column);
    RUN_TEST(panel_route_overflow_keeps_whole_rows_and_counts_the_rest);
}
