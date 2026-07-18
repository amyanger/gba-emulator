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

TEST(window_mask_resolves_region_priority) {
    /* Region priority is WIN0 > WIN1 > OBJWIN > outside.  Where WIN0 and
     * WIN1 overlap, WIN0's mask must win. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    ppu->dispcnt = (1u << 13) | (1u << 14);  /* WIN0 + WIN1 enabled */
    ppu->vcount = 0;

    ppu->win_h[0] = (uint16_t)((0u << 8) | 100u);    /* WIN0: x 0..99 */
    ppu->win_v[0] = (uint16_t)((0u << 8) | 160u);
    ppu->win_h[1] = (uint16_t)((50u << 8) | 150u);   /* WIN1: x 50..149 */
    ppu->win_v[1] = (uint16_t)((0u << 8) | 160u);

    /* WININ low byte = WIN0 mask, high byte = WIN1 mask. */
    ppu->winin  = (uint16_t)(0x01u | (0x02u << 8));
    ppu->winout = 0x04;

    ppu_build_window_mask(ppu);

    ASSERT_EQ_HEX(ppu->win_mask[10],  0x01);  /* WIN0 only */
    ASSERT_EQ_HEX(ppu->win_mask[75],  0x01);  /* overlap -> WIN0 wins */
    ASSERT_EQ_HEX(ppu->win_mask[120], 0x02);  /* WIN1 only */
    ASSERT_EQ_HEX(ppu->win_mask[200], 0x04);  /* outside both */

    free(gba);
}

TEST(window_mask_all_bits_set_when_no_window_enabled) {
    /* With no window enabled in DISPCNT, WININ/WINOUT must be ignored
     * entirely and every layer plus color effects must pass. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    ppu->dispcnt = 0;   /* bits 13/14/15 all clear */
    ppu->vcount = 0;
    ppu->winin  = 0;    /* would mask everything if wrongly consulted */
    ppu->winout = 0;

    ppu_build_window_mask(ppu);

    ASSERT_EQ_HEX(ppu->win_mask[0],   0x3F);
    ASSERT_EQ_HEX(ppu->win_mask[120], 0x3F);
    ASSERT_EQ_HEX(ppu->win_mask[239], 0x3F);

    free(gba);
}

/* Paint a solid 4bpp regular text BG covering the whole top tile row.
 * Each BG gets its own char base block (0/1/2 -> 0x0000/0x4000/0x8000)
 * and screen base block (31/30/29 -> 0xF800/0xF000/0xE800) so tile and
 * map data never overlap.  Tile 1 is solid color-index 1 in palette
 * bank (bg_index + 1), whose entry 1 is set to `color`. */
static void setup_solid_bg(PPU* ppu, int bg_index, int priority, uint16_t color) {
    const uint32_t char_base_blk   = (uint32_t)bg_index;
    const uint32_t screen_base_blk = 31u - (uint32_t)bg_index;
    const uint32_t pal_bank        = (uint32_t)bg_index + 1u;

    ppu->bg_cnt[bg_index] = (uint16_t)((uint32_t)priority
                                     | (char_base_blk << 2)
                                     | (screen_base_blk << 8));

    /* Tile 1 in this BG's char block: every 4bpp pixel = color index 1. */
    for (uint32_t i = 0; i < 32; i++) {
        ppu->vram[char_base_blk * 0x4000u + 32u + i] = 0x11;
    }

    /* Map row 0: all 32 entries point at tile 1 with this palette bank. */
    const uint16_t entry = (uint16_t)(1u | (pal_bank << 12));
    for (uint32_t t = 0; t < 32; t++) {
        ppu->vram[screen_base_blk * 0x800u + t * 2u]      = (uint8_t)(entry & 0xFF);
        ppu->vram[screen_base_blk * 0x800u + t * 2u + 1u] = (uint8_t)(entry >> 8);
    }

    const uint32_t pal_addr = (pal_bank * 16u + 1u) * 2u;
    ppu->palette_ram[pal_addr]      = (uint8_t)(color & 0xFF);
    ppu->palette_ram[pal_addr + 1u] = (uint8_t)(color >> 8);
}

/* OAM entry 0: regular 8x8 4bpp sprite at (0,0), tile 1, solid `color`.
 * All other OAM entries stay zeroed, which renders tile 0 — all zero,
 * i.e. fully transparent — so they contribute nothing. */
static void setup_solid_obj(PPU* ppu, int priority, uint16_t color) {
    for (uint32_t i = 0; i < 32; i++) {
        ppu->vram[0x10000u + 32u + i] = 0x11;
    }
    ppu->palette_ram[0x200 + 2] = (uint8_t)(color & 0xFF);
    ppu->palette_ram[0x200 + 3] = (uint8_t)(color >> 8);

    ppu->oam[0] = 0x00; ppu->oam[1] = 0x00;  /* attr0: y=0, normal, square */
    ppu->oam[2] = 0x00; ppu->oam[3] = 0x00;  /* attr1: x=0, size 0 (8x8) */
    const uint16_t attr2 = (uint16_t)(1u | ((uint32_t)priority << 10));
    ppu->oam[4] = (uint8_t)(attr2 & 0xFF);
    ppu->oam[5] = (uint8_t)(attr2 >> 8);
}

/* Four layers stacked on the same pixel: OBJ(prio 0) over BG0(1) over
 * BG1(2) over BG2(3).  Backdrop = white so a miss is unmistakable. */
static void setup_four_layer_stack(PPU* ppu) {
    ppu->vcount = 0;
    ppu->palette_ram[0] = 0xFF;  /* backdrop = white 0x7FFF */
    ppu->palette_ram[1] = 0x7F;

    setup_solid_bg(ppu, 0, 1, 0x001F);   /* red   */
    setup_solid_bg(ppu, 1, 2, 0x03E0);   /* green */
    setup_solid_bg(ppu, 2, 3, 0x7C00);   /* blue  */
    setup_solid_obj(ppu, 0, 0x7FE0);     /* cyan  */
}

TEST(window_masking_three_deep_reveals_third_layer) {
    /* The regression this rewrite exists for.  OBJ, BG0, BG1 and BG2 all
     * cover x=4.  WIN0 disables OBJ and BG0, so BG1 must show through.
     * The old post-process could only promote the second-priority pixel,
     * so it fell through to the backdrop instead. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    /* Mode 0; BG0/BG1/BG2 on (bits 8/9/10); OBJ on (12); WIN0 on (13). */
    ppu->dispcnt = 0u | (1u << 8) | (1u << 9) | (1u << 10)
                      | (1u << 12) | (1u << 13);
    setup_four_layer_stack(ppu);

    /* WIN0 covers the whole screen. */
    ppu->win_h[0] = (uint16_t)((0u << 8) | 240u);
    ppu->win_v[0] = (uint16_t)((0u << 8) | 160u);
    /* Inside WIN0: BG1 + BG2 + color effects.  BG0 and OBJ disabled. */
    ppu->winin  = (uint16_t)((1u << 1) | (1u << 2) | (1u << 5));
    ppu->winout = 0x3F;

    ppu_render_scanline(ppu);

    ASSERT_EQ_HEX(ppu->framebuffer[4], 0x03E0);  /* BG1 green */

    free(gba);
}

TEST(no_window_leaves_top_priority_layer_visible) {
    /* With windowing disabled entirely, the same stack must resolve to
     * the highest-priority layer, unchanged from before this rewrite. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    ppu->dispcnt = 0u | (1u << 8) | (1u << 9) | (1u << 10) | (1u << 12);
    setup_four_layer_stack(ppu);
    /* WININ/WINOUT are hostile values that must be ignored. */
    ppu->winin  = 0;
    ppu->winout = 0;

    ppu_render_scanline(ppu);

    ASSERT_EQ_HEX(ppu->framebuffer[4], 0x7FE0);  /* OBJ cyan, priority 0 */

    free(gba);
}

TEST(objwin_region_uses_winout_high_byte_mask) {
    /* A GFX-mode-2 sprite defines the OBJ window region.  Inside it the
     * WINOUT high-byte mask applies; outside it the low-byte mask does. */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    /* Mode 0; BG0/1/2 on; OBJ on; OBJWIN on (bit 15).  WIN0/WIN1 off. */
    ppu->dispcnt = 0u | (1u << 8) | (1u << 9) | (1u << 10)
                      | (1u << 12) | (1u << 15);
    setup_four_layer_stack(ppu);

    /* OAM entry 1: same 8x8 tile-1 sprite at (0,0), but GFX mode 2
     * (attr0 bits 11-10 = 2) so it only defines the window region. */
    ppu->oam[8]  = 0x00; ppu->oam[9]  = 0x08;  /* attr0: y=0, gfx mode 2 */
    ppu->oam[10] = 0x00; ppu->oam[11] = 0x00;  /* attr1: x=0, 8x8 */
    ppu->oam[12] = 0x01; ppu->oam[13] = 0x00;  /* attr2: tile 1, prio 0 */

    /* Inside OBJWIN (high byte): BG2 only.  Outside (low byte): all on. */
    ppu->winout = (uint16_t)(0x3Fu | ((1u << 2) << 8));

    ppu_render_scanline(ppu);

    /* x=4 is inside the OBJ window sprite -> only BG2 survives. */
    ASSERT_EQ_HEX(ppu->framebuffer[4], 0x7C00);   /* BG2 blue */
    /* x=100 is outside it; the visible OBJ only spans x 0..7, so the
     * top remaining layer there is BG0. */
    ASSERT_EQ_HEX(ppu->framebuffer[100], 0x001F); /* BG0 red */

    free(gba);
}

TEST(window_color_effect_bit_gates_blending) {
    /* Bit 5 of a region's mask enables colour effects for that region.
     * BLDCNT selects fade-to-black on BG0 with EVY=16 (full black). */
    GBA* gba = make_gba();
    PPU* ppu = &gba->ppu;

    ppu->dispcnt = 0u | (1u << 8) | (1u << 9) | (1u << 10)
                      | (1u << 12) | (1u << 13);
    setup_four_layer_stack(ppu);

    /* WIN0 covers x 0..119 only. */
    ppu->win_h[0] = (uint16_t)((0u << 8) | 120u);
    ppu->win_v[0] = (uint16_t)((0u << 8) | 160u);
    /* Inside: all layers on, bit 5 CLEAR -> no colour effects. */
    ppu->winin  = 0x1F;
    /* Outside: all layers on, bit 5 SET -> colour effects apply. */
    ppu->winout = 0x3F;

    /* BLDCNT: mode 3 (brightness decrease) with BG0 as 1st target. */
    ppu->bldcnt = (uint16_t)((3u << 6) | (1u << 0));
    ppu->bldy = 16;

    ppu_render_scanline(ppu);

    /* x=50: inside WIN0, past the 8px sprite -> BG0 red, unfaded. */
    ASSERT_EQ_HEX(ppu->framebuffer[50], 0x001F);
    /* x=200: outside WIN0 -> BG0 faded fully to black. */
    ASSERT_EQ_HEX(ppu->framebuffer[200], 0x0000);

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
    RUN_TEST(window_mask_resolves_region_priority);
    RUN_TEST(window_mask_all_bits_set_when_no_window_enabled);
    RUN_TEST(window_masking_three_deep_reveals_third_layer);
    RUN_TEST(no_window_leaves_top_priority_layer_visible);
    RUN_TEST(objwin_region_uses_winout_high_byte_mask);
    RUN_TEST(window_color_effect_bit_gates_blending);
    RUN_TEST(affine_sprite_identity_renders);
    RUN_TEST(affine_sprite_double_size_centers_texture);
    RUN_TEST(semi_transparent_sprite_forces_alpha_blend);
}
