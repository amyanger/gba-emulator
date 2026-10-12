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
    ASSERT_EQ(game_panel_next_page(GAME_PAGE_ENCOUNTERS), GAME_PAGE_NUZLOCKE);
    ASSERT_EQ(game_panel_next_page(GAME_PAGE_NUZLOCKE), GAME_PAGE_BERRIES);
    ASSERT_EQ(game_panel_next_page(GAME_PAGE_BERRIES), GAME_PAGE_AUTO);
}

TEST(panel_auto_page_follows_context) {
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_AUTO, GAME_CTX_BATTLE_WILD), GAME_PAGE_COUNT);
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_AUTO, GAME_CTX_BATTLE_TRAINER), GAME_PAGE_COUNT);
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_AUTO, GAME_CTX_OVERWORLD), GAME_PAGE_ENCOUNTERS);
    ASSERT_EQ(game_panel_resolve_page(GAME_PAGE_PARTY, GAME_CTX_BATTLE_WILD), GAME_PAGE_PARTY);
}

TEST(panel_battle_switches_to_auto_and_back) {
    PanelBattleFollow f = { 0 };
    GamePage page = game_panel_follow_battle(GAME_PAGE_PARTY, GAME_CTX_OVERWORLD, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
    page = game_panel_follow_battle(page, GAME_CTX_BATTLE_WILD, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_follow_battle(page, GAME_CTX_BATTLE_WILD, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_follow_battle(page, GAME_CTX_OVERWORLD, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
    page = game_panel_follow_battle(page, GAME_CTX_OVERWORLD, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
}

TEST(panel_battle_keeps_page_chosen_during_battle) {
    PanelBattleFollow f = { 0 };
    GamePage page = game_panel_follow_battle(GAME_PAGE_ENCOUNTERS, GAME_CTX_BATTLE_TRAINER, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_next_page(page); /* what F10 does */
    f.restore = false;
    page = game_panel_follow_battle(page, GAME_CTX_BATTLE_TRAINER, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
    page = game_panel_follow_battle(page, GAME_CTX_OVERWORLD, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
}

TEST(panel_battle_ignores_none_context) {
    PanelBattleFollow f = { 0 };
    GamePage page = game_panel_follow_battle(GAME_PAGE_PARTY, GAME_CTX_NONE, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
    page = game_panel_follow_battle(page, GAME_CTX_BATTLE_WILD, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_follow_battle(page, GAME_CTX_NONE, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_follow_battle(page, GAME_CTX_BATTLE_WILD, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_follow_battle(page, GAME_CTX_NONE, &f);
    page = game_panel_follow_battle(page, GAME_CTX_OVERWORLD, &f);
    ASSERT_EQ(page, GAME_PAGE_PARTY);
}

TEST(panel_battle_from_auto_stays_auto) {
    PanelBattleFollow f = { 0 };
    GamePage page = game_panel_follow_battle(GAME_PAGE_AUTO, GAME_CTX_BATTLE_WILD, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
    page = game_panel_follow_battle(page, GAME_CTX_OVERWORLD, &f);
    ASSERT_EQ(page, GAME_PAGE_AUTO);
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
    game_panel_title_case("TM36", out, sizeof(out));
    ASSERT_STR_EQ(out, "TM36");
    game_panel_title_case("HM05 FLASH", out, sizeof(out));
    ASSERT_STR_EQ(out, "HM05 Flash");
    game_panel_title_case("HMM", out, sizeof(out));
    ASSERT_STR_EQ(out, "Hmm");
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
        e->move_count = 4;
        for (int m = 0; m < 4; m++) {
            game_strcpy(e->moves[m].move, GAME_NAME_LEN, "SUPERSONICXXX");
            e->moves[m].type = 13;
            e->moves[m].quarters = m == 0 ? 0xFF : (uint8_t)(4u << m);
        }
        e->moves[3].status_move = true;
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

static bool draw_checked_nuz(float density, const GameSnapshot* snap, GamePage page, uint8_t sel,
                             const NuzView* nuz) {
    int w = (int)(600 * density + 0.5f), h = (int)(480 * density + 0.5f);
    for (int i = 0; i < GUARD; i++) s_buf[w * h + i] = SENTINEL;
    UiCanvas c;
    ui_canvas_init(&c, s_buf, w, h, density);
    game_panel_draw(&c, snap, page, sel, nuz, (uint8_t)(sel % NUZ_SUB_COUNT), sel, true);
    for (int i = 0; i < GUARD; i++)
        if (s_buf[w * h + i] != SENTINEL) return false;
    return (s_buf[w * h - 1] >> 24) == 0xFF;
}

static bool draw_checked(float density, const GameSnapshot* snap, GamePage page, uint8_t sel) {
    return draw_checked_nuz(density, snap, page, sel, NULL);
}

/* A busy run: more routes, team and graves than fit, long names everywhere. */
static void fill_nuz(NuzView* v) {
    memset(v, 0, sizeof(*v));
    v->state = NUZ_VIEW_RUN;
    v->alive = 30; v->dead = 12; v->routes_used = 60; v->reloads = 3; v->deadused = 2; v->rerolls = 1;
    v->wiped = true; v->reloaded_after_death = true;
    strcpy(v->reroll_place, "GRANITE CAVE");
    strcpy(v->start_time, "123:45:06");
    v->route_count = NUZ_VIEW_ROUTES;
    v->here_row = 50;
    for (int i = 0; i < NUZ_VIEW_ROUTES; i++) {
        NuzViewRoute* r = &v->routes[i];
        strcpy(r->place, "SAFARI ZONE NORTHWEST");
        strcpy(r->who, i % 5 ? "CRAWDAUNTXXXXX" : "");
        r->pill = (uint8_t)(i % 7);
        r->here = i == 50;
        r->reroll = i % 3 == 0;
    }
    v->team_total = 60;
    v->team_count = NUZ_VIEW_TEAM;
    for (int i = 0; i < NUZ_VIEW_TEAM; i++) {
        strcpy(v->team[i].nick, "WWWWWWWWWW");
        strcpy(v->team[i].species, "CRAWDAUNTXXXXX");
        v->team[i].level = 100;
        v->team[i].where = (uint8_t)(i % 4);
    }
    v->grave_total = 20;
    v->grave_count = NUZ_VIEW_GRAVES;
    for (int i = 0; i < NUZ_VIEW_GRAVES; i++) {
        NuzViewGrave* g = &v->graves[i];
        strcpy(g->nick, "WWWWWWWWWW");
        strcpy(g->species, "CRAWDAUNTXXXXX");
        g->level = 100;
        strcpy(g->caught_at, "SAFARI ZONE NORTHWEST");
        strcpy(g->died_at, "SAFARI ZONE NORTHWEST");
        strcpy(g->foe, "CRAWDAUNTXXXXX");
        strcpy(g->time, "999:59:59");
        g->reloaded_after = true;
    }
}

TEST(panel_nuzlocke_views_stay_in_bounds) {
    static const float densities[] = {0.75f, 1.0f, 2.0f, 3.0f};
    static GameSnapshot rest;
    static NuzView full, empty, none;
    fill_party_and_route(&rest);
    fill_nuz(&full);
    memset(&empty, 0, sizeof(empty));
    empty.state = NUZ_VIEW_RUN;
    empty.here_row = -1;
    memset(&none, 0, sizeof(none));
    none.state = NUZ_VIEW_NO_RUN;
    for (size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); d++)
        for (uint8_t sub = 0; sub < NUZ_SUB_COUNT; sub++) {
            ASSERT_TRUE(draw_checked_nuz(densities[d], &rest, GAME_PAGE_NUZLOCKE, sub, &full));
            ASSERT_TRUE(draw_checked_nuz(densities[d], &rest, GAME_PAGE_NUZLOCKE, sub, &empty));
            ASSERT_TRUE(draw_checked_nuz(densities[d], &rest, GAME_PAGE_NUZLOCKE, sub, &none));
            ASSERT_TRUE(draw_checked_nuz(densities[d], &rest, GAME_PAGE_NUZLOCKE, sub, NULL));
        }
}

TEST(panel_nuzlocke_sub_views_cycle) {
    ASSERT_EQ(game_panel_nuz_sub(NUZ_SUB_ROUTES, 1), NUZ_SUB_TEAM);
    ASSERT_EQ(game_panel_nuz_sub(NUZ_SUB_GRAVEYARD, 1), NUZ_SUB_ROUTES);
    ASSERT_EQ(game_panel_nuz_sub(NUZ_SUB_ROUTES, -1), NUZ_SUB_GRAVEYARD);
}

TEST(panel_nuzlocke_route_window_keeps_current_place) {
    static NuzView v;
    memset(&v, 0, sizeof(v));
    v.route_count = 40;
    v.here_row = -1;
    ASSERT_EQ(game_panel_nuz_route_start(&v, 15), 0);
    v.here_row = 30;
    uint8_t start = game_panel_nuz_route_start(&v, 15);
    ASSERT_TRUE(start <= 30 && 30 < start + 15);
    ASSERT_TRUE(start + 15 >= 33);  /* a couple of rows after the current place */
    v.here_row = 39;
    ASSERT_EQ(game_panel_nuz_route_start(&v, 15), 25);
    v.route_count = 10;
    ASSERT_EQ(game_panel_nuz_route_start(&v, 15), 0);
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
    game_panel_draw(&c, &snap, GAME_PAGE_AUTO, 0, NULL, 0, 0, true);
    /* Rows between the HP bar and the bottom of the enemy's move list; the gap between
     * the left column (ends at x 292) and the right one (starts at x 308),
     * minus a pixel either side for anti-aliasing. */
    int lit = 0;
    for (int y = 100; y < 350; y++)
        for (int x = 294; x < 306; x++)
            if (s_buf[y * 600 + x] != UI_BG) lit++;
    ASSERT_EQ(lit, 0);
}

TEST(panel_invalid_snapshot_shows_reason) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    game_strcpy(snap.reason, sizeof(snap.reason), "Waiting for save data");
    UiCanvas c;
    ui_canvas_init(&c, s_buf, 600, 480, 1.0f);
    game_panel_draw(&c, &snap, GAME_PAGE_AUTO, 0, NULL, 0, 0, true);
    int lit = 0;
    for (int y = 220; y < 240; y++)
        for (int x = 0; x < 600; x++)
            if (s_buf[y * 600 + x] != UI_BG) lit++;
    ASSERT_TRUE(lit > 0);
}

static int lit_rows(int y0, int y1) {
    int lit = 0;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < 600; x++)
            if (s_buf[y * 600 + x] != UI_BG) lit++;
    return lit;
}

TEST(panel_unsupported_rom_explains_why) {
    static GameSnapshot snap;
    const GameSupport kinds[2] = {GAME_SUPPORT_MODIFIED, GAME_SUPPORT_OTHER_GAME};
    for (int k = 0; k < 2; k++) {
        memset(&snap, 0, sizeof(snap));
        snap.support = kinds[k];
        UiCanvas c;
        ui_canvas_init(&c, s_buf, 600, 480, 1.0f);
        game_panel_draw(&c, &snap, GAME_PAGE_AUTO, 0, NULL, 0, 0, true);
        ASSERT_TRUE(lit_rows(160, 190) > 0);  /* heading */
        ASSERT_TRUE(lit_rows(200, 300) > 0);  /* explanation */
    }
}

/* Lit pixels in a 1x rect; tinted counts only clearly coloured ones (not grey text). */
static int lit_rect(int x0, int x1, int y0, int y1, bool tinted) {
    int lit = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            uint32_t p = s_buf[y * 600 + x];
            if (p == UI_BG) continue;
            int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
            int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
            if (!tinted || hi - lo > 60) lit++;
        }
    return lit;
}

static void draw_1x(const GameSnapshot* snap) {
    UiCanvas c;
    ui_canvas_init(&c, s_buf, 600, 480, 1.0f);
    game_panel_draw(&c, snap, GAME_PAGE_AUTO, 0, NULL, 0, 0, true);
}

/* The enemy's moves sit in the right column under the IVs (which end near y 230). */
TEST(panel_battle_shows_enemy_moves) {
    static GameSnapshot snap;
    fill_battle(&snap, GAME_CTX_BATTLE_TRAINER);
    snap.enemy_count = 1;
    snap.enemies[0].move_count = 0;
    draw_1x(&snap);
    ASSERT_EQ(lit_rect(308, 584, 236, 350, false), 0);
    snap.enemies[0].move_count = 4;
    draw_1x(&snap);
    ASSERT_TRUE(lit_rect(308, 584, 236, 350, false) > 0);
}

/* An unknown matchup (0xFF) is a muted "?", never a coloured "x0". */
TEST(panel_enemy_move_unknown_matchup_is_muted) {
    static GameSnapshot snap;
    fill_battle(&snap, GAME_CTX_BATTLE_TRAINER);
    snap.enemy_count = 1;
    GameEnemy* e = &snap.enemies[0];
    for (int m = 0; m < 4; m++) {
        e->moves[m].status_move = false;
        e->moves[m].quarters = 0;
    }
    draw_1x(&snap);
    ASSERT_TRUE(lit_rect(540, 584, 236, 350, true) > 0);
    for (int m = 0; m < 4; m++) e->moves[m].quarters = 0xFF;
    draw_1x(&snap);
    ASSERT_TRUE(lit_rect(540, 584, 236, 350, false) > 0);
    ASSERT_EQ(lit_rect(540, 584, 236, 350, true), 0);
}

/* A tall battle page (second enemy line, status, enemy moves) with a full bag ends the
 * catch list with "+N more": the last line drawn is that short label at the left, with
 * no ball row (count, bar, percent) beside it, and it sits above the page clip (476). */
TEST(panel_wild_battle_many_balls_fit) {
    static const float densities[] = {1.0f, 2.0f};
    static GameSnapshot snap;
    fill_battle(&snap, GAME_CTX_BATTLE_WILD);
    snap.catch_count = 16;
    for (int i = 0; i < 16; i++) {
        game_strcpy(snap.catch_rows[i].ball, GAME_NAME_LEN, "WWWWWWWWWWWW");
        snap.catch_rows[i].quantity = 999;
        snap.catch_rows[i].permille = 500;
    }
    for (int v = 0; v < 10; v++) {
        snap.enemy_count = (uint8_t)(1 + v % 2);
        snap.enemies[0].move_count = (uint8_t)(v / 2);
        for (size_t d = 0; d < 2; d++) {
            float dn = densities[d];
            int w = (int)(600 * dn + 0.5f), h = (int)(480 * dn + 0.5f);
            ASSERT_TRUE(draw_checked(dn, &snap, GAME_PAGE_AUTO, 0));
            int last = -1;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (s_buf[y * w + x] != UI_BG) last = y;
            ASSERT_TRUE(last >= (int)(400 * dn) && last < (int)(476 * dn));
            int wide = 0;
            for (int y = last - (int)(14 * dn); y <= last; y++)
                for (int x = (int)(200 * dn); x < w; x++)
                    if (s_buf[y * w + x] != UI_BG) wide++;
            ASSERT_EQ(wide, 0);
        }
    }
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
    game_panel_draw(&c, &snap, GAME_PAGE_ENCOUNTERS, 0, NULL, 0, 0, true);
}

static void fill_mirage(GameSnapshot* s, MirageWhere today) {
    s->context = GAME_CTX_OVERWORLD;
    MirageInfo* mi = &s->mirage;
    memset(mi, 0, sizeof(*mi));
    mi->valid = mi->clock_set = mi->clock_known = true;
    mi->today.where = today;
    mi->today.slot = today == MIRAGE_BOX ? 14 : 5;
    game_strcpy(mi->today.name, sizeof(mi->today.name), "CRAWDAUNTXX");
    mi->forecast_count = 3;
    const uint16_t days[3] = {7, 3650, 36000};
    for (int i = 0; i < 3; i++) {
        mi->forecast[i].where = i ? MIRAGE_BOX : MIRAGE_PARTY;
        mi->forecast[i].slot = (uint8_t)(i ? 14 : 5);
        mi->forecast[i].days = days[i];
        game_strcpy(mi->forecast[i].name, sizeof(mi->forecast[i].name), "FLETCHINDERX");
    }
}

TEST(panel_route_mirage_card_shrinks_list) {
    static GameSnapshot snap;
    static const uint8_t counts[ENC_METHOD_COUNT] = {12, 12, 12, 12, 12, 12};
    RouteLayout plain, with;
    fill_route(&snap, counts);
    game_panel_route_layout(&snap, &plain);
    fill_mirage(&snap, MIRAGE_NONE);
    game_panel_route_layout(&snap, &with);
    ASSERT_TRUE(with.avail < plain.avail);
    ASSERT_TRUE(with.height[0] <= with.avail && with.height[1] <= with.avail);
    ASSERT_EQ(layout_total(&with), 72);
}

TEST(panel_route_mirage_card_stays_in_bounds) {
    static const float densities[] = {0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
    static const MirageWhere states[] = {MIRAGE_NONE, MIRAGE_PARTY, MIRAGE_BOX};
    static GameSnapshot snap;
    static const uint8_t counts[ENC_METHOD_COUNT] = {12, 3, 0, 2, 3, 5};
    for (size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); d++)
        for (size_t k = 0; k < 3; k++) {
            fill_route(&snap, counts);
            fill_mirage(&snap, states[k]);
            snap.mirage.pending = k == 2 ? 212 : 0;
            ASSERT_TRUE(draw_checked(densities[d], &snap, GAME_PAGE_ENCOUNTERS, 0));
            snap.mirage.clock_set = false;
            ASSERT_TRUE(draw_checked(densities[d], &snap, GAME_PAGE_ENCOUNTERS, 0));
            snap.mirage.clock_set = true;
            snap.mirage.forecast_count = 0;
            ASSERT_TRUE(draw_checked(densities[d], &snap, GAME_PAGE_ENCOUNTERS, 0));
        }
}

TEST(panel_mirage_key_arms_then_jumps) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = true;
    fill_mirage(&snap, MIRAGE_BOX);
    MirageArm arm = {0, 0};
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_ARM);
    ASSERT_EQ(game_panel_mirage_key(&snap, 2500, &arm), MIRAGE_KEY_JUMP);
    /* Disarmed after the jump: the next press arms again. */
    ASSERT_EQ(game_panel_mirage_key(&snap, 2600, &arm), MIRAGE_KEY_ARM);
}

TEST(panel_mirage_key_rearms_after_timeout) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = true;
    fill_mirage(&snap, MIRAGE_NONE);
    MirageArm arm = {0, 0};
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_ARM);
    ASSERT_EQ(game_panel_mirage_key(&snap, 4001, &arm), MIRAGE_KEY_ARM);
    ASSERT_EQ(game_panel_mirage_key(&snap, 4100, &arm), MIRAGE_KEY_JUMP);
}

TEST(panel_mirage_key_rearms_when_target_changes) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = true;
    fill_mirage(&snap, MIRAGE_NONE);
    MirageArm arm = {0, 0};
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_ARM);
    snap.mirage.forecast[0].days = 6;
    ASSERT_EQ(game_panel_mirage_key(&snap, 1500, &arm), MIRAGE_KEY_ARM);
}

TEST(panel_mirage_key_refuses_when_not_useful) {
    static GameSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.valid = true;
    MirageArm arm = {0, 0};
    fill_mirage(&snap, MIRAGE_PARTY);
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_ALREADY_UP);
    fill_mirage(&snap, MIRAGE_NONE);
    snap.mirage.forecast_count = 0;
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_NO_MATCH);
    snap.mirage.clock_set = false;
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_NOT_SET);
    fill_mirage(&snap, MIRAGE_NONE);
    snap.context = GAME_CTX_BATTLE_WILD;
    ASSERT_EQ(game_panel_mirage_key(&snap, 1000, &arm), MIRAGE_KEY_IGNORE);
    ASSERT_EQ(arm.armed_ms, 0);
}


/* ---- Berries page ---- */

static BerryTree* add_tree(BerryView* v, uint8_t place, BerryStatus st, uint16_t next, bool paused) {
    BerryTree* t = &v->tree[v->count];
    memset(t, 0, sizeof(*t));
    t->id = v->count;
    t->place = place;
    t->berry = 7;
    t->stage = st == BERRY_RIPE || st == BERRY_FALLING ? 5 : 3;
    t->watered = st == BERRY_WATER ? 0x3 : 0x7;
    t->status = st;
    t->paused = paused;
    t->next_min = next;
    t->ripe_min = t->stage == 5 ? 0 : next + 240u;
    t->yield_lo = 2; t->yield_hi = 5;
    strcpy(t->name, "WWWWWW");
    v->growing[place]++;
    v->count++;
    return t;
}

static void fill_berries(GameSnapshot* s) {
    memset(s, 0, sizeof(*s));
    s->valid = true;
    s->context = GAME_CTX_OVERWORLD;
    BerryView* v = &s->berries;
    v->valid = v->clock_set = v->projected = true;
    v->place_count = 3;
    for (uint8_t i = 0; i < 3; i++) {
        snprintf(v->place[i].name, sizeof(v->place[i].name), "SAFARI ZONE NORTHWES%u", i);
        v->place[i].spots = 40;
    }
    v->here = 0;
    for (int i = 0; i < 60; i++) add_tree(v, 0, (BerryStatus)(i % 5), (uint16_t)(i * 37), i % 7 == 0);
    add_tree(v, 1, BERRY_WATER, 100, false);
    add_tree(v, 2, BERRY_GROWING, 900, false);
}

TEST(panel_berry_time_format) {
    char b[24];
    game_panel_berry_time(0, b, sizeof(b)); ASSERT_STR_EQ(b, "0m");
    game_panel_berry_time(40, b, sizeof(b)); ASSERT_STR_EQ(b, "40m");
    game_panel_berry_time(60, b, sizeof(b)); ASSERT_STR_EQ(b, "1h");
    game_panel_berry_time(310, b, sizeof(b)); ASSERT_STR_EQ(b, "5h 10m");
    game_panel_berry_time(1440, b, sizeof(b)); ASSERT_STR_EQ(b, "1d");
    game_panel_berry_time(2000, b, sizeof(b)); ASSERT_STR_EQ(b, "1d 9h");
    game_panel_berry_time(400000, b, sizeof(b)); ASSERT_STR_EQ(b, "277d 18h");
}

TEST(panel_berry_place_order_here_first) {
    static BerryView v;
    memset(&v, 0, sizeof(v));
    v.valid = true;
    v.place_count = 4;
    strcpy(v.place[0].name, "B"); strcpy(v.place[1].name, "C");
    strcpy(v.place[2].name, "A"); strcpy(v.place[3].name, "D");
    add_tree(&v, 0, BERRY_GROWING, 10, false);
    add_tree(&v, 1, BERRY_RIPE, 10, false);
    add_tree(&v, 2, BERRY_GROWING, 10, false);
    v.here = 3;   /* no trees there: not listed */
    uint8_t order[BERRY_PLACE_MAX];
    ASSERT_EQ(game_panel_berry_places(&v, order), 3);
    ASSERT_EQ(order[0], 1);   /* most urgent */
    ASSERT_EQ(order[1], 2);   /* then by name: A before B */
    ASSERT_EQ(order[2], 0);
    v.here = 0;
    ASSERT_EQ(game_panel_berry_places(&v, order), 3);
    ASSERT_EQ(order[0], 0);
    ASSERT_EQ(order[1], 1);
}

TEST(panel_berry_needs_you_skips_untouched) {
    static BerryView v;
    memset(&v, 0, sizeof(v));
    v.valid = true;
    v.place_count = 1;
    add_tree(&v, 0, BERRY_RIPE, 600, true);      /* untouched game tree */
    add_tree(&v, 0, BERRY_WATER, 50, false);
    add_tree(&v, 0, BERRY_RIPE, 300, false);
    add_tree(&v, 0, BERRY_GROWING, 5, false);
    add_tree(&v, 0, BERRY_FALLING, 20, false);
    add_tree(&v, 0, BERRY_RIPE, 100, false);
    uint8_t idx[BERRY_VIEW_MAX];
    ASSERT_EQ(game_panel_berry_needs(&v, idx), 4);
    ASSERT_EQ(idx[0], 4);   /* falling */
    ASSERT_EQ(idx[1], 5);   /* ripe, sooner drop first */
    ASSERT_EQ(idx[2], 2);
    ASSERT_EQ(idx[3], 1);   /* water */
}

TEST(panel_berries_states_stay_in_bounds) {
    static const float densities[] = {0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
    static GameSnapshot busy, empty, noclock, unread;
    fill_berries(&busy);
    memset(&empty, 0, sizeof(empty));
    empty.valid = true;
    empty.berries.valid = empty.berries.clock_set = true;
    empty.berries.place_count = 16;
    for (int i = 0; i < 16; i++) { strcpy(empty.berries.place[i].name, "ROUTE 123"); empty.berries.place[i].spots = 4; }
    noclock = busy;
    noclock.berries.clock_set = false;
    memset(&unread, 0, sizeof(unread));
    unread.valid = true;
    for (size_t d = 0; d < sizeof(densities) / sizeof(densities[0]); d++)
        for (uint8_t sel = 0; sel < 4; sel++) {
            ASSERT_TRUE(draw_checked(densities[d], &busy, GAME_PAGE_BERRIES, sel));
            ASSERT_TRUE(draw_checked(densities[d], &empty, GAME_PAGE_BERRIES, sel));
            ASSERT_TRUE(draw_checked(densities[d], &noclock, GAME_PAGE_BERRIES, sel));
            ASSERT_TRUE(draw_checked(densities[d], &unread, GAME_PAGE_BERRIES, sel));
        }
}

TEST(panel_berries_overflow_rows_fit) {
    static GameSnapshot snap;
    fill_berries(&snap);
    /* 60 trees at the selected place: the page shows whole rows that end above the footer. */
    BerryPageLayout l;
    game_panel_berry_layout(&snap.berries, 0, &l);
    ASSERT_TRUE(l.rows > 0);
    ASSERT_TRUE(l.rows < 60);
    ASSERT_EQ(l.more, 60 - l.rows);
    ASSERT_TRUE(l.list_end <= 452.0f);
}

TEST(panel_berries_more_places_line_stays_above_footer) {
    static BerryView v;
    /* Review repro: 15 trees open, 1 "Needs you" row, 8 places: "+N more places" hit the footer. */
    for (uint8_t trees = 1; trees <= 30; trees++)
        for (uint8_t places = 2; places <= 16; places++) {
            memset(&v, 0, sizeof(v));
            v.valid = v.clock_set = true;
            v.here = 0;
            v.place_count = places;
            add_tree(&v, 0, BERRY_WATER, 50, false);
            for (uint8_t i = 1; i < trees; i++) add_tree(&v, 0, BERRY_GROWING, 500, false);
            for (uint8_t p = 1; p < places; p++) add_tree(&v, p, BERRY_GROWING, 500, false);
            BerryPageLayout l;
            game_panel_berry_layout(&v, 0, &l);
            ASSERT_TRUE(l.others_end <= 452.0f);
            ASSERT_EQ(l.others + l.others_more, l.others_more ? places - 1 : l.others);
            if (!l.others_more) ASSERT_EQ(l.others, places - 1);
        }
}

void run_game_panel_tests(void) {
    RUN_TEST(panel_berries_more_places_line_stays_above_footer);
    RUN_TEST(panel_berry_time_format);
    RUN_TEST(panel_berry_place_order_here_first);
    RUN_TEST(panel_berry_needs_you_skips_untouched);
    RUN_TEST(panel_berries_states_stay_in_bounds);
    RUN_TEST(panel_berries_overflow_rows_fit);
    TEST_SUITE("game_panel");
    RUN_TEST(panel_page_cycles);
    RUN_TEST(panel_auto_page_follows_context);
    RUN_TEST(panel_battle_switches_to_auto_and_back);
    RUN_TEST(panel_battle_keeps_page_chosen_during_battle);
    RUN_TEST(panel_battle_ignores_none_context);
    RUN_TEST(panel_battle_from_auto_stays_auto);
    RUN_TEST(panel_title_case);
    RUN_TEST(panel_select_wraps);
    RUN_TEST(panel_pages_stay_in_bounds_at_all_densities);
    RUN_TEST(panel_long_names_stay_in_columns);
    RUN_TEST(panel_invalid_snapshot_shows_reason);
    RUN_TEST(panel_unsupported_rom_explains_why);
    RUN_TEST(panel_battle_shows_enemy_moves);
    RUN_TEST(panel_enemy_move_unknown_matchup_is_muted);
    RUN_TEST(panel_wild_battle_many_balls_fit);
    RUN_TEST(panel_route_busy_water_route_shows_every_row);
    RUN_TEST(panel_route_short_list_keeps_roomy_single_column);
    RUN_TEST(panel_route_overflow_keeps_whole_rows_and_counts_the_rest);
    RUN_TEST(panel_nuzlocke_views_stay_in_bounds);
    RUN_TEST(panel_nuzlocke_sub_views_cycle);
    RUN_TEST(panel_nuzlocke_route_window_keeps_current_place);
    RUN_TEST(panel_route_mirage_card_shrinks_list);
    RUN_TEST(panel_route_mirage_card_stays_in_bounds);
    RUN_TEST(panel_mirage_key_arms_then_jumps);
    RUN_TEST(panel_mirage_key_rearms_after_timeout);
    RUN_TEST(panel_mirage_key_rearms_when_target_changes);
    RUN_TEST(panel_mirage_key_refuses_when_not_useful);
}
