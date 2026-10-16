#include "test_harness.h"
#include "frontend/trainer_card_draw.h"
#include "ui/ui_theme.h"
#include <SDL2/SDL_keyboard.h>

static uint32_t s_px[CARD_W * CARD_H];
static uint32_t s_pics[6][MON_PIC_PIXELS];

TEST(card_path_suffix) {
    char buf[128];
    trainer_card_path("/r/emerald.gba", 0, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "/r/emerald.gba.19700101-000000.card.png");
}

TEST(card_style_names) {
    CardStyle s = CARD_STYLE_ACE;
    ASSERT_TRUE(card_style_from_name("grid", &s));
    ASSERT_EQ(s, CARD_STYLE_GRID);
    ASSERT_TRUE(card_style_from_name("ace", &s));
    ASSERT_EQ(s, CARD_STYLE_ACE);
    ASSERT_TRUE(!card_style_from_name("x", &s));
}

TEST(card_display_name) {
    GamePartyMon m;
    memset(&m, 0, sizeof m);
    strcpy(m.species, "PIKACHU");
    strcpy(m.nickname, "PIKA");
    ASSERT_STR_EQ(trainer_card_display_name(&m), "PIKA");
    strcpy(m.nickname, "???");
    ASSERT_STR_EQ(trainer_card_display_name(&m), "PIKACHU");
    strcpy(m.nickname, "? ?");
    ASSERT_STR_EQ(trainer_card_display_name(&m), "PIKACHU");
    m.nickname[0] = 0;
    ASSERT_STR_EQ(trainer_card_display_name(&m), "PIKACHU");
    m.egg = true;
    ASSERT_STR_EQ(trainer_card_display_name(&m), "Egg");
}

static bool has_color(uint32_t argb) {
    for (int i = 0; i < CARD_W * CARD_H; i++)
        if (s_px[i] == argb) return true;
    return false;
}

TEST(card_draw_smoke_both_styles) {
    static TrainerCardData d;
    memset(&d, 0, sizeof d);
    strcpy(d.name, "BRENDAN");
    d.public_id = 60997;
    d.play_frames = 216000u * 23;
    d.badges = 0x0F;
    d.count = 6;
    for (int i = 0; i < 6; i++) {
        GamePartyMon* m = &d.party[i];
        m->present = true;
        strcpy(m->species, "METAGROSS");
        strcpy(m->nickname, "BOB");
        strcpy(m->nature, "Adamant");
        strcpy(m->item, "Leftovers");
        strcpy(m->ability, "Clear Body");
        m->level = 70;
        m->type_ids[0] = 8;
        m->type_ids[1] = 14;
        memset(m->ivs, 31, 6);
    }
    d.party[2].egg = true;
    d.party[4].shiny = true;
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < MON_PIC_PIXELS; k++) s_pics[i][k] = 0xFFFF0000u;
    bool ok[6] = {true, true, true, true, true, true};
    UiCanvas c;
    for (int style = 0; style < 2; style++) {
        memset(s_px, 0, sizeof s_px);
        ui_canvas_init(&c, s_px, CARD_W, CARD_H, 1.0f);
        trainer_card_draw(&c, &d, (const uint32_t(*)[MON_PIC_PIXELS])s_pics, ok, (CardStyle)style, 0);
        ASSERT_EQ_HEX(s_px[5 * CARD_W + CARD_W - 5], UI_BG);
        ASSERT_TRUE(has_color(0xFFFF0000u));
    }
    d.count = 2;
    ok[1] = false;
    d.party[1].bad = true;
    for (int style = 0; style < 2; style++) {
        memset(s_px, 0, sizeof s_px);
        ui_canvas_init(&c, s_px, CARD_W, CARD_H, 1.0f);
        trainer_card_draw(&c, &d, (const uint32_t(*)[MON_PIC_PIXELS])s_pics, ok, (CardStyle)style, 0);
        trainer_card_draw(&c, &d, (const uint32_t(*)[MON_PIC_PIXELS])s_pics, ok, (CardStyle)style, -1);
    }
}

/* Badge 2 is Cascade blue in Kanto and Knuckle gold in Hoenn. */
TEST(card_badge_colors_follow_the_region) {
    static TrainerCardData d;
    memset(&d, 0, sizeof d);
    strcpy(d.name, "RED");
    d.badges = 0x02;
    bool ok[6] = {false, false, false, false, false, false};
    UiCanvas c;
    memset(s_px, 0, sizeof s_px);
    ui_canvas_init(&c, s_px, CARD_W, CARD_H, 1.0f);
    trainer_card_draw(&c, &d, (const uint32_t(*)[MON_PIC_PIXELS])s_pics, ok, CARD_STYLE_GRID, -1);
    ASSERT_TRUE(!has_color(0xFF4C9BFFu));
    d.region = GAME_REGION_KANTO;
    memset(s_px, 0, sizeof s_px);
    ui_canvas_init(&c, s_px, CARD_W, CARD_H, 1.0f);
    trainer_card_draw(&c, &d, (const uint32_t(*)[MON_PIC_PIXELS])s_pics, ok, CARD_STYLE_GRID, -1);
    ASSERT_TRUE(has_color(0xFF4C9BFFu));
}

/* R9: Cmd+C (copy), Ctrl/Alt chords and key repeat never export. */
TEST(card_key_request) {
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, 0, false), 1 + CARD_STYLE_GRID);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_LSHIFT, false), 1 + CARD_STYLE_ACE);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_RSHIFT, false), 1 + CARD_STYLE_ACE);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_NUM | KMOD_CAPS, false), 1 + CARD_STYLE_GRID);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_LGUI, false), 0);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_RGUI | KMOD_LSHIFT, false), 0);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_LCTRL, false), 0);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, KMOD_LALT, false), 0);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_C, 0, true), 0);
    ASSERT_EQ(trainer_card_key_request(SDL_SCANCODE_X, 0, false), 0);
}

void run_trainer_card_draw_tests(void) {
    RUN_TEST(card_path_suffix);
    RUN_TEST(card_style_names);
    RUN_TEST(card_display_name);
    RUN_TEST(card_draw_smoke_both_styles);
    RUN_TEST(card_badge_colors_follow_the_region);
    RUN_TEST(card_key_request);
}
