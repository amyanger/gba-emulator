#include "test_harness.h"
#include "frontend/xray/xray.h"
#include "frontend/overlay_draw.h"
#include "gba.h"
#include "headless/headless.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include <math.h>

/* Canvas plus a guard tail that must survive every draw untouched. */
#define GUARD 4096u
#define GUARD_WORD 0xDEADBEEFu
static uint32_t s_px[XRAY_FB_MAX_W * XRAY_FB_MAX_H + GUARD];
static XRayState s_state;

TEST(xray_fit_large_display) {
    XRayFit f = xray_fit(1920, 1050, 1.0f);
    ASSERT_TRUE(f.scale == 1.0f);
    ASSERT_EQ(f.win_w, XRAY_W_PT);
    ASSERT_EQ(f.win_h, XRAY_H_PT);
}

TEST(xray_fit_macbook_13) {
    XRayFit f = xray_fit(1440, 805, 2.0f);
    ASSERT_TRUE(f.scale < 1.0f);
    ASSERT_TRUE(f.win_h + XRAY_TITLEBAR_PT <= 805);
    ASSERT_TRUE(f.win_w <= 1440);
}

TEST(xray_fit_text_floor) {
    /* 1x display too small: stop at 10 px for the smallest text */
    XRayFit f = xray_fit(1024, 600, 1.0f);
    ASSERT_TRUE(fabsf(f.scale - 10.0f / XRAY_MIN_TEXT_PT) < 0.001f);
    ASSERT_EQ(f.win_w, (int)ceilf(XRAY_W_PT * f.scale));
}

TEST(xray_fit_unknown_display) {
    XRayFit f = xray_fit(0, 0, 2.0f);
    ASSERT_TRUE(f.scale == 1.0f);
    ASSERT_EQ(f.win_w, XRAY_W_PT);
}

TEST(xray_canvas_size_tracks_output) {
    int w, h;
    float d;
    xray_canvas_size(1200, 820, &w, &h, &d);
    ASSERT_EQ(w, 1200);
    ASSERT_EQ(h, 820);
    ASSERT_TRUE(d == 1.0f);
    xray_canvas_size(2276, 1556, &w, &h, &d);
    ASSERT_EQ(w, 2276);
    ASSERT_EQ(h, 1556);
    ASSERT_TRUE(fabsf(d - 2276.0f / 1200.0f) < 0.0001f);
}

TEST(xray_canvas_size_caps_density) {
    int w, h;
    float d;
    xray_canvas_size(3600, 2460, &w, &h, &d);
    ASSERT_EQ(w, XRAY_FB_MAX_W);
    ASSERT_EQ(h, XRAY_FB_MAX_H);
    ASSERT_TRUE(d == XRAY_MAX_DENSITY);
    xray_canvas_size(2400, 1641, &w, &h, &d);   /* window rounding: one row taller */
    ASSERT_EQ(h, XRAY_FB_MAX_H);
}

static void check_captions_fit(float density) {
    UiCanvas c;
    ui_font_init();
    ui_canvas_init(&c, s_px, 64, 64, density);
    for (int i = 0; i < XRAY_CARD_COUNT; i++) {
        float room = xray_cards[i].w - 2 * XRAY_PAD;
        ASSERT_TRUE(ui_text_width(&c, UI_FONT_REGULAR, XRAY_SIZE_CAPTION, xray_captions[i]) <= room);
        ASSERT_TRUE(ui_text_width(&c, UI_FONT_SEMIBOLD, XRAY_SIZE_TITLE, xray_titles[i]) <= room / 2);
    }
}
TEST(xray_captions_fit) {
    check_captions_fit(1.0f);
    check_captions_fit(2.0f);
}

/* Pixels in r that are neither page background nor card surface: something was drawn there. */
static int ink_in(const UiCanvas* c, UiRect r) {
    int n = 0;
    for (int y = ui_px(c, r.y); y < ui_px(c, r.y + r.h); y++)
        for (int x = ui_px(c, r.x); x < ui_px(c, r.x + r.w); x++) {
            uint32_t p = c->px[y * c->w + x];
            if (p != UI_BG && p != UI_SURFACE) n++;
        }
    return n;
}

static GBA* fresh_gba(void) {
    GBA* gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    xray_init(&s_state);
    s_state.active = true;
    return gba;
}

static void free_gba(GBA* gba) {
    gba_destroy(gba);
    free(gba);
}

/* Returns false (after printing) if a card is blank or the guard tail was overwritten. */
static int draw_and_check(GBA* gba, float density, int min_ink) {
    int w = (int)ceilf(XRAY_W_PT * density), h = (int)ceilf(XRAY_H_PT * density);
    for (uint32_t i = 0; i < GUARD; i++) s_px[(size_t)w * h + i] = GUARD_WORD;
    UiCanvas c;
    ui_canvas_init(&c, s_px, w, h, density);
    xray_capture_ppu_layers(&gba->ppu, &s_state);
    xray_capture_audio(&gba->apu, &s_state);
    xray_draw(&c, gba, &s_state);
    for (int i = 0; i < XRAY_CARD_COUNT; i++)
        if (ink_in(&c, xray_cards[i]) < min_ink) {
            printf("[card %d blank at density %.2f] ", i, (double)density);
            return 0;
        }
    for (uint32_t i = 0; i < GUARD; i++)
        if (s_px[(size_t)w * h + i] != GUARD_WORD) {
            printf("[guard overwritten at density %.2f] ", (double)density);
            return 0;
        }
    return 1;
}

TEST(xray_draw_smoke) {
    GBA* gba = fresh_gba();
    int ok = draw_and_check(gba, 1.0f, 200) && draw_and_check(gba, 1.9f, 200) &&
             draw_and_check(gba, 2.0f, 200);
    free_gba(gba);
    ASSERT_TRUE(ok);
}

/* A mode-0 frame with BG0 visible: its thumbnail shows real pixels, not the "Not used" fill. */
TEST(xray_see_card_shows_enabled_layer) {
    GBA* gba = fresh_gba();
    gba->ppu.dispcnt = 0x0100;                       /* mode 0, BG0 on */
    gba->ppu.palette_ram[0] = 0x1F;                  /* backdrop red */
    xray_capture_ppu_layers(&gba->ppu, &s_state);
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    xray_draw(&c, gba, &s_state);
    UiRect r = xray_cards[XRAY_CARD_SEE];
    /* first thumbnail image starts at (x0, top + 15) */
    uint32_t p = s_px[(int)(r.y + 56 + 15 + 20) * XRAY_W_PT + (int)(r.x + XRAY_PAD + 20)];
    free_gba(gba);
    ASSERT_EQ_HEX(p, gba_to_argb(0x001F));
}

TEST(xray_draw_bitmap_mode_and_all_off) {
    GBA* gba = fresh_gba();
    gba->ppu.dispcnt = 0x0403;                       /* mode 3, BG2 */
    int ok = draw_and_check(gba, 2.0f, 200);
    gba->ppu.dispcnt = 0x0080;                       /* forced blank, all layers off */
    ok = ok && draw_and_check(gba, 1.0f, 200);
    free_gba(gba);
    ASSERT_TRUE(ok);
}

/* BG palette entry 1 shows up in the "Background colors" grid. */
TEST(xray_tiles_card_shows_palette) {
    GBA* gba = fresh_gba();
    gba->ppu.palette_ram[2] = 0xE0;                  /* index 1 = green (0x03E0) */
    gba->ppu.palette_ram[3] = 0x03;
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    xray_draw(&c, gba, &s_state);
    UiRect r = xray_cards[XRAY_CARD_TILES];
    /* palette grid at x0 + 430, top + 16; swatch 1 is the second 5 pt cell */
    int px = (int)(r.x + XRAY_PAD + 430 + 7), py = (int)(r.y + 56 + 16 + 2);
    uint32_t p = s_px[py * XRAY_W_PT + px];
    free_gba(gba);
    ASSERT_EQ_HEX(p, gba_to_argb(0x03E0));
}

/* A loud left-channel snapshot moves the waveform away from the center line. */
TEST(xray_hear_card_draws_waveform) {
    GBA* gba = fresh_gba();
    s_state.audio_snapshot_count = XRAY_AUDIO_SNAP;
    for (int i = 0; i < XRAY_AUDIO_SNAP; i++) {
        s_state.audio_snapshot[i * 2] = (int16_t)((i / 32) % 2 ? 20000 : -20000);
        s_state.audio_snapshot[i * 2 + 1] = 0;
    }
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    ui_canvas_clear(&c, UI_BG);
    xray_render_audio(&c, &gba->apu, &s_state);
    UiRect r = xray_cards[XRAY_CARD_HEAR];
    /* left waveform box: x0, top + 14, 300 x 46. Count drawn pixels in its upper quarter. */
    int lit = 0;
    for (int y = (int)(r.y + 56 + 15); y < (int)(r.y + 56 + 14 + 11); y++)
        for (int x = (int)(r.x + XRAY_PAD); x < (int)(r.x + XRAY_PAD + 300); x++)
            if (s_px[y * XRAY_W_PT + x] != UI_BG && s_px[y * XRAY_W_PT + x] != UI_SURFACE) lit++;
    free_gba(gba);
    ASSERT_TRUE(lit > 20);
}

/* PC value text lands in the register grid (ink in the second column, last row). */
TEST(xray_cpu_card_shows_registers) {
    GBA* gba = fresh_gba();
    gba->cpu.regs[15] = 0x08006A76;
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    ui_canvas_clear(&c, UI_BG);
    xray_render_cpu(&c, &gba->cpu);
    UiRect r = xray_cards[XRAY_CARD_CPU];
    UiRect pc_cell = {r.x + XRAY_PAD + 160, r.y + 56 + 7 * 15, 140, 15};
    int ink = ink_in(&c, pc_cell);
    free_gba(gba);
    ASSERT_TRUE(ink > 30);
}

/* A timer that just overflowed gets a warm activity dot left of its row. */
TEST(xray_scenes_card_flashes_timer) {
    GBA* gba = fresh_gba();
    s_state.timer_flash[0] = XRAY_FLASH_FRAMES;
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    xray_draw(&c, gba, &s_state);
    UiRect r = xray_cards[XRAY_CARD_SCENES];
    /* dot centre: x0 - 7, first data row (top + 32) + 7 */
    uint32_t p = s_px[(int)(r.y + 56 + 32 + 7) * XRAY_W_PT + (int)(r.x + XRAY_PAD - 7)];
    free_gba(gba);
    ASSERT_EQ_HEX(p, UI_WARN);
}

/* Scroll registers only use 9 bits (GBATEK BGxHOFS/VOFS), so 0xFFFE shows the same as 510. */
TEST(xray_see_card_masks_scroll_to_9_bits) {
    static uint32_t first[XRAY_W_PT * XRAY_H_PT];
    GBA* gba = fresh_gba();
    gba->ppu.dispcnt = 0x0100;
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    gba->ppu.bg_hofs[0] = 0xFFFE;
    xray_draw(&c, gba, &s_state);
    memcpy(first, s_px, sizeof(first));
    gba->ppu.bg_hofs[0] = 0x01FE;
    xray_draw(&c, gba, &s_state);
    int same = memcmp(first, s_px, sizeof(first)) == 0;
    free_gba(gba);
    ASSERT_TRUE(same);
}

/* Pills are the smallest text (UI_SIZE_PILL); the fit floor must keep them at 10 px or more. */
TEST(xray_fit_floor_keeps_pill_text_10px) {
    XRayFit f1 = xray_fit(1024, 600, 1.0f);
    XRayFit f2 = xray_fit(800, 400, 2.0f);
    ASSERT_TRUE(UI_SIZE_PILL * f1.scale * 1.0f >= 9.999f);
    ASSERT_TRUE(UI_SIZE_PILL * f2.scale * 2.0f >= 9.999f);
}

/* PC runs ahead of the executing instruction: 8 bytes in ARM, 4 in Thumb. */
TEST(xray_cpu_exec_addr_accounts_for_pipeline) {
    GBA* gba = fresh_gba();
    gba->cpu.regs[15] = 0x08000108;
    gba->cpu.cpsr = 0x1F;                            /* ARM */
    uint32_t arm = xray_cpu_exec_addr(&gba->cpu);
    gba->cpu.cpsr = 0x1F | (1u << 5);                /* Thumb */
    uint32_t thumb = xray_cpu_exec_addr(&gba->cpu);
    free_gba(gba);
    ASSERT_EQ_HEX(arm, 0x08000100u);
    ASSERT_EQ_HEX(thumb, 0x08000104u);
}

/* Flashes fade in headless runs too, so an export only lights what fired recently. */
TEST(xray_headless_run_fades_flashes) {
    GBA* gba = fresh_gba();
    s_state.timer_flash[0] = XRAY_FLASH_FRAMES;
    s_state.dma_flash[3] = XRAY_FLASH_FRAMES;
    g_xray = &s_state;
    FILE* sink = tmpfile();
    int rc = headless_run(gba, XRAY_FLASH_FRAMES, sink, NULL);
    fclose(sink);
    g_xray = NULL;
    uint8_t t0 = s_state.timer_flash[0], d3 = s_state.dma_flash[3];
    free_gba(gba);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(t0, 0);
    ASSERT_EQ(d3, 0);
}

/* "Listening" interrupt pills must read as blue next to idle ones. */
TEST(xray_scenes_listening_pill_is_blue) {
    GBA* gba = fresh_gba();
    gba->interrupts.ie = 0x0002;                     /* HBlank enabled, nothing fired */
    UiCanvas c;
    ui_canvas_init(&c, s_px, XRAY_W_PT, XRAY_H_PT, 1.0f);
    xray_draw(&c, gba, &s_state);
    UiRect r = xray_cards[XRAY_CARD_SCENES];
    int blue = 0;
    for (int y = (int)(r.y + 56 + 16); y < (int)(r.y + r.h); y++)
        for (int x = (int)(r.x + XRAY_PAD + 840); x < (int)(r.x + r.w); x++) {
            uint32_t p = s_px[y * XRAY_W_PT + x];
            int rr = (int)((p >> 16) & 0xFF), gg = (int)((p >> 8) & 0xFF), bb = (int)(p & 0xFF);
            if (bb - (rr > gg ? rr : gg) > 0x50) blue++;
        }
    free_gba(gba);
    ASSERT_TRUE(blue > 100);
}

void run_xray_tests(void) {
    RUN_TEST(xray_fit_large_display);
    RUN_TEST(xray_fit_macbook_13);
    RUN_TEST(xray_fit_text_floor);
    RUN_TEST(xray_fit_unknown_display);
    RUN_TEST(xray_canvas_size_tracks_output);
    RUN_TEST(xray_canvas_size_caps_density);
    RUN_TEST(xray_captions_fit);
    RUN_TEST(xray_draw_smoke);
    RUN_TEST(xray_see_card_shows_enabled_layer);
    RUN_TEST(xray_see_card_masks_scroll_to_9_bits);
    RUN_TEST(xray_draw_bitmap_mode_and_all_off);
    RUN_TEST(xray_tiles_card_shows_palette);
    RUN_TEST(xray_hear_card_draws_waveform);
    RUN_TEST(xray_cpu_card_shows_registers);
    RUN_TEST(xray_scenes_card_flashes_timer);
    RUN_TEST(xray_fit_floor_keeps_pill_text_10px);
    RUN_TEST(xray_cpu_exec_addr_accounts_for_pipeline);
    RUN_TEST(xray_headless_run_fades_flashes);
    RUN_TEST(xray_scenes_listening_pill_is_blue);
}
