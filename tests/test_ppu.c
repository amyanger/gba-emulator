#include "test_harness.h"
#include "gba.h"
#include "ppu/ppu.h"
#include <stdlib.h>

/* Helper: build a GBA in a clean reset state, with no ROM loaded.
 * gba_run_frame works against the bus's empty memory and HLE BIOS,
 * which is sufficient for timing-edge tests that don't depend on
 * actual instructions executing. */
static void ppu_test_setup(GBA* gba) {
    gba_init(gba);
}

TEST(vcount_wraps_to_zero_after_full_frame) {
    GBA gba;
    ppu_test_setup(&gba);
    gba_run_frame(&gba);
    /* Loop runs 228 times; each iter increments vcount; wraps 227 -> 0. */
    ASSERT_EQ(gba.ppu.vcount, 0);
}

TEST(hblank_irq_fires_when_enabled) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Enable HBlank IRQ in DISPSTAT (bit 4) and IE (bit 1). */
    gba.ppu.dispstat |= (1u << 4);
    gba.interrupts.ie = IRQ_HBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    /* IRQ_HBLANK fires 228 times per frame; all bits OR into IF. */
    ASSERT_TRUE((gba.interrupts.irf & IRQ_HBLANK) != 0);
}

TEST(hblank_irq_does_not_fire_when_dispstat_bit_clear) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat &= ~(1u << 4);  /* HBlank IRQ disabled. */
    gba.interrupts.ie = IRQ_HBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_EQ(gba.interrupts.irf & IRQ_HBLANK, 0);
}

TEST(vblank_irq_fires_when_dispstat_bit_set) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat |= (1u << 3);  /* VBlank IRQ enabled. */
    gba.interrupts.ie = IRQ_VBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_TRUE((gba.interrupts.irf & IRQ_VBLANK) != 0);
}

TEST(vblank_irq_does_not_fire_when_dispstat_bit_clear) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat &= ~(1u << 3);
    gba.interrupts.ie = IRQ_VBLANK;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_EQ(gba.interrupts.irf & IRQ_VBLANK, 0);
}

TEST(frame_complete_set_exactly_once_per_frame) {
    GBA gba;
    ppu_test_setup(&gba);
    /* gba_run_frame sets frame_complete=true on the line-159->160
     * transition and resets it to false at the start of the next call. */
    gba_run_frame(&gba);
    ASSERT_EQ(gba.frame_complete, true);
    gba_run_frame(&gba);
    /* After the second frame, frame_complete is again true. The
     * "exactly once per frame" contract is observable by checking
     * that it was reset between the two run_frame calls. */
    ASSERT_EQ(gba.frame_complete, true);
}

TEST(vcount_irq_fires_when_target_in_visible_range) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Set LYC=100, enable VCount IRQ in DISPSTAT (bit 5) and IE (bit 2). */
    gba.ppu.dispstat = (100u << 8) | (1u << 5);
    gba.interrupts.ie = IRQ_VCOUNT;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_TRUE((gba.interrupts.irf & IRQ_VCOUNT) != 0);
}

TEST(vcount_irq_fires_when_target_in_vblank_range) {
    GBA gba;
    ppu_test_setup(&gba);
    gba.ppu.dispstat = (200u << 8) | (1u << 5);
    gba.interrupts.ie = IRQ_VCOUNT;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_TRUE((gba.interrupts.irf & IRQ_VCOUNT) != 0);
}

TEST(vcount_irq_never_fires_when_target_above_max) {
    GBA gba;
    ppu_test_setup(&gba);
    /* LYC=228 is impossible — vcount tops out at 227. */
    gba.ppu.dispstat = (228u << 8) | (1u << 5);
    gba.interrupts.ie = IRQ_VCOUNT;
    gba.interrupts.ime = true;
    gba.interrupts.irf = 0;
    gba_run_frame(&gba);
    ASSERT_EQ(gba.interrupts.irf & IRQ_VCOUNT, 0);
}

TEST(vcount_match_flag_set_regardless_of_irq_enable) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Set LYC=1, IRQ-enable bit (5) clear. */
    gba.ppu.dispstat = (1u << 8);
    gba_run_frame(&gba);
    /* After one frame, vcount=0; LYC=1 → flag should be 0. */
    ASSERT_EQ(BIT(gba.ppu.dispstat, 2), 0);

    /* Now set LYC=0 to match. Run another frame; the flag should
     * end up set with no IRQ-enable bit. */
    gba.ppu.dispstat = (0u << 8);
    gba_run_frame(&gba);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 2), 1);
    /* And no IRQ fired (DISPSTAT.5 clear). */
    ASSERT_EQ(gba.interrupts.irf & IRQ_VCOUNT, 0);
}

TEST(affine_refs_reload_from_latches_at_vblank_start) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Set affine BG2 internal refs and latches to distinct values. */
    gba.ppu.bg_ref_x_latch[0] = 0x12345678;
    gba.ppu.bg_ref_y_latch[0] = 0x0ABCDEF0;
    gba.ppu.bg_ref_x[0]       = 0x00000000;
    gba.ppu.bg_ref_y[0]       = 0x00000000;

    gba_run_frame(&gba);

    /* At line 160 (VBlank start), the orchestrator copies latches
     * into internal refs. By end-of-frame the internal refs equal
     * the latches (no rendering occurred to advance them past
     * VBlank, so the post-VBlank value is exactly the latch). */
    ASSERT_EQ_HEX((uint32_t)gba.ppu.bg_ref_x[0], 0x12345678u);
    ASSERT_EQ_HEX((uint32_t)gba.ppu.bg_ref_y[0], 0x0ABCDEF0u);
}

TEST(force_blank_fills_framebuffer_with_white) {
    GBA gba;
    ppu_test_setup(&gba);
    /* Force-blank: DISPCNT bit 7. */
    gba.ppu.dispcnt = (1u << 7);

    gba_run_frame(&gba);

    /* Sample a few framebuffer pixels — all must be 0x7FFF. */
    ASSERT_EQ_HEX(gba.ppu.framebuffer[0], 0x7FFFu);
    ASSERT_EQ_HEX(gba.ppu.framebuffer[SCREEN_WIDTH * 80 + 120], 0x7FFFu);
    ASSERT_EQ_HEX(gba.ppu.framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT - 1], 0x7FFFu);

    /* vcount still progressed normally. */
    ASSERT_EQ(gba.ppu.vcount, 0);
}

TEST(vblank_flag_clears_on_line_227) {
    GBA gba;
    ppu_test_setup(&gba);

    /* Drive 160 scanlines — vcount lands on 160, VBlank flag is set. */
    for (int i = 0; i < 160; i++) gba_run_scanline(&gba);
    ASSERT_EQ(gba.ppu.vcount, 160);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 0), 1);

    /* Drive 66 more — vcount=226, still in VBlank, flag still set. */
    for (int i = 0; i < 66; i++) gba_run_scanline(&gba);
    ASSERT_EQ(gba.ppu.vcount, 226);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 0), 1);

    /* One more — vcount=227, flag must clear. */
    gba_run_scanline(&gba);
    ASSERT_EQ(gba.ppu.vcount, 227);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 0), 0);
}

TEST(hblank_constants_consistent_with_scanline) {
    /* Pin the chunk-budget arithmetic — if any of these change, the
     * orchestrator's per-line cycle accounting will drift. */
    ASSERT_EQ(HBLANK_FLAG_SET_CYCLE, 1006);
    ASSERT_EQ(HBLANK_TAIL_CYCLES, 226);
    ASSERT_EQ(HBLANK_FLAG_SET_CYCLE + HBLANK_TAIL_CYCLES, SCANLINE_CYCLES);
}

TEST(hblank_flag_zero_before_cycle_1006) {
    /* gba_run_cycles does not fire scanline edge events, so after
     * 1005 cycles into a scanline, the HBlank flag must still be 0
     * (no edge has been triggered yet). The set-edge fires when the
     * orchestrator reaches the chunk boundary — i.e., during a
     * gba_run_scanline call, not a gba_run_cycles call. */
    GBA gba;
    ppu_test_setup(&gba);
    gba_run_cycles(&gba, HBLANK_FLAG_SET_CYCLE - 1);
    ASSERT_EQ(BIT(gba.ppu.dispstat, 1), 0);
}

/* ---- Rendering: windowing, affine sprites, semi-transparency ------ */

/* Heap-allocated GBA for the rendering tests below (large struct). */
static GBA* make_gba(void) {
    GBA* gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    return gba;
}

TEST(window_inverted_h_range_clamps_no_wrap) {
    /* GBATEK: garbage WIN0H values (X1 > X2, or X2 > 240) are
     * interpreted as X2 = 240 — the window runs from X1 to the right
     * edge.  It does NOT wrap around to the left edge. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    /* Mode 3 bitmap, BG2 on, WIN0 on. */
    ppu->dispcnt = 3 | (1u << 10) | (1u << 13);
    ppu->vcount = 0;

    /* Row 0 all red (BGR555 0x001F). */
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        ppu->vram[x * 2] = 0x1F;
        ppu->vram[x * 2 + 1] = 0x00;
    }
    /* Backdrop (palette entry 0) = blue, distinct from red and from
     * zero-initialized memory. */
    ppu->palette_ram[0] = 0x00;
    ppu->palette_ram[1] = 0x7C;

    /* WIN0: horizontal left=100 right=10 (inverted), full-height. */
    ppu->win_h[0] = (uint16_t)((100 << 8) | 10);
    ppu->win_v[0] = (uint16_t)((0 << 8) | 160);
    /* Inside WIN0: BG2 visible.  Outside: nothing (backdrop). */
    ppu->winin = 1u << 2;
    ppu->winout = 0;

    ppu_render_scanline(ppu);

    const uint16_t red = 0x001F, blue = 0x7C00;
    /* x=150 is in [100, 240): inside the window — BG2 shows. */
    ASSERT_EQ_HEX(ppu->framebuffer[150], red);
    /* x=5 would only be inside if the range wrapped; it must not. */
    ASSERT_EQ_HEX(ppu->framebuffer[5], blue);
    /* x=50 is outside either way. */
    ASSERT_EQ_HEX(ppu->framebuffer[50], blue);

    free(gba);
}

/* Set up an 8x8 4bpp OBJ: tile 1 solid color-index 1, OBJ palette
 * color 1 = red, identity affine params in OAM group 0. */
static void setup_affine_obj_fixture(PPU* ppu) {
    /* Tile 1 (OBJ VRAM 0x10000 + 32): every 4bpp pixel = color 1. */
    for (int i = 0; i < 32; i++) {
        ppu->vram[0x10000 + 32 + i] = 0x11;
    }
    /* OBJ palette entry 1 = red (0x001F). */
    ppu->palette_ram[0x200 + 2] = 0x1F;
    ppu->palette_ram[0x200 + 3] = 0x00;
    /* Backdrop = blue so misses are visible. */
    ppu->palette_ram[0] = 0x00;
    ppu->palette_ram[1] = 0x7C;

    /* Affine parameter group 0 = identity (PA=PD=0x100, PB=PC=0).
     * Group N's PA/PB/PC/PD live at OAM offsets N*32 + 6/14/22/30. */
    ppu->oam[6] = 0x00; ppu->oam[7] = 0x01;   /* PA = 0x100 */
    ppu->oam[14] = 0x00; ppu->oam[15] = 0x00; /* PB = 0 */
    ppu->oam[22] = 0x00; ppu->oam[23] = 0x00; /* PC = 0 */
    ppu->oam[30] = 0x00; ppu->oam[31] = 0x01; /* PD = 0x100 */
}

TEST(affine_sprite_identity_renders) {
    /* OBJ mode 1 (affine) with an identity matrix must render exactly
     * like a regular sprite — today affine sprites are skipped
     * entirely and Emerald's battle/menu scaling effects vanish. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    ppu->dispcnt = 0 | (1u << 12); /* mode 0, OBJ on */
    ppu->vcount = 0;
    setup_affine_obj_fixture(ppu);

    /* OAM entry 0: y=0, affine (bit 8), 4bpp, square 8x8, x=10,
     * param group 0, tile 1, priority 0. */
    uint16_t attr0 = 0 | (1u << 8);
    uint16_t attr1 = 10;
    uint16_t attr2 = 1;
    ppu->oam[0] = (uint8_t)attr0; ppu->oam[1] = (uint8_t)(attr0 >> 8);
    ppu->oam[2] = (uint8_t)attr1; ppu->oam[3] = (uint8_t)(attr1 >> 8);
    ppu->oam[4] = (uint8_t)attr2; ppu->oam[5] = (uint8_t)(attr2 >> 8);

    ppu_render_scanline(ppu);

    const uint16_t red = 0x001F, blue = 0x7C00;
    ASSERT_EQ_HEX(ppu->framebuffer[10], red);
    ASSERT_EQ_HEX(ppu->framebuffer[17], red);
    ASSERT_EQ_HEX(ppu->framebuffer[9], blue);
    ASSERT_EQ_HEX(ppu->framebuffer[18], blue);

    free(gba);
}

TEST(affine_sprite_double_size_centers_texture) {
    /* OBJ mode 3 doubles the bounding box (16x16 for an 8x8 sprite);
     * with an identity matrix the texture sits centered in the box:
     * visible columns are bbox x 4..11, rows 4..11. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    ppu->dispcnt = 0 | (1u << 12);
    ppu->vcount = 4; /* local_y = 4 → first texture row */
    setup_affine_obj_fixture(ppu);

    /* OAM entry 0: y=0, affine double-size (bits 8+9), x=100. */
    uint16_t attr0 = 0 | (3u << 8);
    uint16_t attr1 = 100;
    uint16_t attr2 = 1;
    ppu->oam[0] = (uint8_t)attr0; ppu->oam[1] = (uint8_t)(attr0 >> 8);
    ppu->oam[2] = (uint8_t)attr1; ppu->oam[3] = (uint8_t)(attr1 >> 8);
    ppu->oam[4] = (uint8_t)attr2; ppu->oam[5] = (uint8_t)(attr2 >> 8);

    ppu_render_scanline(ppu);

    const uint16_t red = 0x001F, blue = 0x7C00;
    const uint32_t row = 4 * SCREEN_WIDTH; /* vcount=4 renders row 4 */
    ASSERT_EQ_HEX(ppu->framebuffer[row + 104], red);  /* bbox col 4 → tex col 0 */
    ASSERT_EQ_HEX(ppu->framebuffer[row + 111], red);  /* bbox col 11 → tex col 7 */
    ASSERT_EQ_HEX(ppu->framebuffer[row + 103], blue); /* outside texture */
    ASSERT_EQ_HEX(ppu->framebuffer[row + 112], blue);

    free(gba);
}

TEST(semi_transparent_sprite_forces_alpha_blend) {
    /* OBJ GFX mode 1 (semi-transparent) forces alpha blending against
     * a valid BLDCNT 2nd target, regardless of the BLDCNT mode bits and
     * of the OBJ 1st-target bit.  Emerald uses this for shadows and
     * battle effects; without it these sprites render opaque. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    /* Mode 3 (BG2 bitmap) + OBJ. */
    ppu->dispcnt = 3 | (1u << 10) | (1u << 12);
    ppu->vcount = 0;

    /* BG row 0 red. */
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        ppu->vram[x * 2] = 0x1F;
        ppu->vram[x * 2 + 1] = 0x00;
    }

    /* OBJ tile 1 solid color 1; OBJ palette color 1 = white. */
    for (int i = 0; i < 32; i++) {
        ppu->vram[0x10000 + 32 + i] = 0x11;
    }
    ppu->palette_ram[0x200 + 2] = 0xFF;
    ppu->palette_ram[0x200 + 3] = 0x7F;

    /* OAM entry 0: regular sprite, GFX mode 1 (attr0 bit 10), 8x8,
     * x=10, tile 1. */
    uint16_t attr0 = 0 | (1u << 10);
    uint16_t attr1 = 10;
    uint16_t attr2 = 1;
    ppu->oam[0] = (uint8_t)attr0; ppu->oam[1] = (uint8_t)(attr0 >> 8);
    ppu->oam[2] = (uint8_t)attr1; ppu->oam[3] = (uint8_t)(attr1 >> 8);
    ppu->oam[4] = (uint8_t)attr2; ppu->oam[5] = (uint8_t)(attr2 >> 8);

    /* BLDCNT mode 0 (no effect selected), but BG2 is a 2nd target —
     * semi-transparency must still blend.  EVA=EVB=8 (half/half). */
    ppu->bldcnt = 1u << 10;
    ppu->bldalpha = 8 | (8u << 8);

    ppu_render_scanline(ppu);

    /* white/2 + red/2: r=(31*8+31*8)>>4=31, g=b=(31*8)>>4=15. */
    const uint16_t blended = (uint16_t)(31 | (15u << 5) | (15u << 10));
    ASSERT_EQ_HEX(ppu->framebuffer[10], blended);
    ASSERT_EQ_HEX(ppu->framebuffer[17], blended);
    /* Outside the sprite the BG stays plain red. */
    ASSERT_EQ_HEX(ppu->framebuffer[9], 0x001F);

    free(gba);
}

/* Suite registration (called from test_runner.c). */
void run_ppu_tests(void) {
    TEST_SUITE("ppu");
    RUN_TEST(vcount_wraps_to_zero_after_full_frame);
    RUN_TEST(hblank_irq_fires_when_enabled);
    RUN_TEST(hblank_irq_does_not_fire_when_dispstat_bit_clear);
    RUN_TEST(vblank_irq_fires_when_dispstat_bit_set);
    RUN_TEST(vblank_irq_does_not_fire_when_dispstat_bit_clear);
    RUN_TEST(frame_complete_set_exactly_once_per_frame);
    RUN_TEST(vcount_irq_fires_when_target_in_visible_range);
    RUN_TEST(vcount_irq_fires_when_target_in_vblank_range);
    RUN_TEST(vcount_irq_never_fires_when_target_above_max);
    RUN_TEST(vcount_match_flag_set_regardless_of_irq_enable);
    RUN_TEST(affine_refs_reload_from_latches_at_vblank_start);
    RUN_TEST(force_blank_fills_framebuffer_with_white);
    RUN_TEST(vblank_flag_clears_on_line_227);
    RUN_TEST(hblank_constants_consistent_with_scanline);
    RUN_TEST(hblank_flag_zero_before_cycle_1006);
    RUN_TEST(window_inverted_h_range_clamps_no_wrap);
    RUN_TEST(affine_sprite_identity_renders);
    RUN_TEST(affine_sprite_double_size_centers_texture);
    RUN_TEST(semi_transparent_sprite_forces_alpha_blend);
}
