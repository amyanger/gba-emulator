#include "test_harness.h"
#include "cheat/cheat.h"
#include "cheat/cheat_crypto.h"
#include "gba.h"

#define EMERALD_ID 0x45455042u

static GBA* make_gba(void) {
    static GBA* gba;
    if (!gba) gba = calloc(1, sizeof(GBA));
    gba_init(gba);
    cheat_set_game_code(&gba->cheats, EMERALD_ID);
    return gba;
}

static char err[160];

static uint16_t ew16(GBA* g, uint32_t off) { return (uint16_t)(g->bus.ewram[off] | (g->bus.ewram[off + 1] << 8)); }

static void set_ew16(GBA* g, uint32_t off, uint16_t v) {
    g->bus.ewram[off] = (uint8_t)v;
    g->bus.ewram[off + 1] = (uint8_t)(v >> 8);
}

TEST(cheat_add_text_codebreaker_writes_ram) {
    GBA* g = make_gba();
    int32_t i = cheat_add_text(&g->cheats, "Money:\n82025838 03E7\n", NULL, CHEAT_DEV_AUTO, err, sizeof(err));
    ASSERT_EQ(i, 0);
    ASSERT_STR_EQ(g->cheats.cheats[0].name, "Money");
    ASSERT_EQ(g->cheats.cheats[0].device, CHEAT_DEV_CODEBREAKER);
    ASSERT_TRUE(g->cheats.cheats[0].enabled);
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ_HEX(ew16(g, 0x25838), 0x03E7);
}

TEST(cheat_add_text_rejects_bad_paste) {
    GBA* g = make_gba();
    ASSERT_EQ(cheat_add_text(&g->cheats, "82025838 3E7\n", NULL, CHEAT_DEV_AUTO, err, sizeof(err)), -1);
    ASSERT_TRUE(strstr(err, "Line 1") != NULL);
    ASSERT_EQ(g->cheats.cheat_count, 0);
}

TEST(cheat_add_text_not_confident_asks_for_device) {
    GBA* g = make_gba();
    ASSERT_EQ(cheat_add_text(&g->cheats, "FFFFFFFF FFFFFFFF\n", NULL, CHEAT_DEV_AUTO, err, sizeof(err)), -1);
    ASSERT_TRUE(strstr(err, "pick") != NULL);
}

TEST(cheat_add_text_encrypted_par3_and_override) {
    GBA* g = make_gba();
    uint32_t a = 0x02224ABC, b = 0x0000270F;
    cheat_tea_encrypt(&a, &b, CHEAT_SEEDS_PAR3);
    char text[64];
    snprintf(text, sizeof(text), "%08X %08X\n", (unsigned)a, (unsigned)b);
    int32_t i = cheat_add_text(&g->cheats, text, "Test", CHEAT_DEV_AUTO, err, sizeof(err));
    ASSERT_EQ(i, 0);
    ASSERT_EQ(g->cheats.cheats[0].device, CHEAT_DEV_PAR3);
    ASSERT_TRUE(!g->cheats.cheats[0].device_override);
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ_HEX(ew16(g, 0x24ABC), 0x270F);
    /* Forcing the wrong device fails and leaves the cheat as it was. */
    ASSERT_TRUE(!cheat_set_device(&g->cheats, 0, CHEAT_DEV_CODEBREAKER));
    ASSERT_EQ(g->cheats.cheats[0].device, CHEAT_DEV_PAR3);
}

TEST(cheat_if_skips_when_false) {
    GBA* g = make_gba();
    set_ew16(g, 0x100, 5);
    cheat_add_text(&g->cheats, "72000100 0001\n82000200 1111\n82000202 2222\n", "x", CHEAT_DEV_AUTO, err, sizeof(err));
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ_HEX(ew16(g, 0x200), 0);        /* skipped: [100] != 1 */
    ASSERT_EQ_HEX(ew16(g, 0x202), 0x2222);
    set_ew16(g, 0x100, 1);
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ_HEX(ew16(g, 0x200), 0x1111);
}

TEST(cheat_par3_block_else_endif_runs_one_branch) {
    GBA* g = make_gba();
    set_ew16(g, 0x24ABC, 1);
    cheat_add_text(&g->cheats,
                   "8A224ABC 00000001\n00224AC0 00000011\n00000000 60000000\n"
                   "00224AC1 00000022\n00000000 40000000\n00224AC2 00000033\n",
                   "x", CHEAT_DEV_PAR3_RAW, err, sizeof(err));
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(g->bus.ewram[0x24AC0], 0x11);
    ASSERT_EQ(g->bus.ewram[0x24AC1], 0x00);   /* ELSE branch not run */
    ASSERT_EQ(g->bus.ewram[0x24AC2], 0x33);   /* after ENDIF always runs */
    set_ew16(g, 0x24ABC, 2);
    g->bus.ewram[0x24AC0] = 0;
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(g->bus.ewram[0x24AC0], 0x00);
    ASSERT_EQ(g->bus.ewram[0x24AC1], 0x22);
}

TEST(cheat_signed_vs_unsigned_compare) {
    GBA* g = make_gba();
    set_ew16(g, 0x300, 0xFFFF);   /* -1 signed, 65535 unsigned */
    /* CodeBreaker C: IF [a] < 5 (signed) -> true for -1 */
    cheat_add_text(&g->cheats, "C2000300 0005\n82000310 0001\n", "s", CHEAT_DEV_AUTO, err, sizeof(err));
    /* PAR3 unsigned < (cond 28): 65535 < 5 is false */
    cheat_add_text(&g->cheats, "2A200300 00000005\n02200312 00000001\n", "u", CHEAT_DEV_PAR3_RAW, err, sizeof(err));
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(ew16(g, 0x310), 1);
    ASSERT_EQ(ew16(g, 0x312), 0);
}

TEST(cheat_keys_condition_uses_active_low_keyinput) {
    GBA* g = make_gba();
    cheat_add_text(&g->cheats, "D0000020 0001\n82000400 0001\n", "k", CHEAT_DEV_AUTO, err, sizeof(err));
    g->input.keyinput = 0x03FF;   /* nothing held */
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(ew16(g, 0x400), 0);
    g->input.keyinput = 0x03FE;   /* A held (bit 0 low) */
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(ew16(g, 0x400), 1);
}

TEST(cheat_slide_and_add) {
    GBA* g = make_gba();
    cheat_add_text(&g->cheats, "42000500 0010\n00010003 0002\nE2000600 0005\n", "s", CHEAT_DEV_AUTO, err, sizeof(err));
    set_ew16(g, 0x600, 10);
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(ew16(g, 0x500), 0x10);
    ASSERT_EQ(ew16(g, 0x502), 0x11);
    ASSERT_EQ(ew16(g, 0x504), 0x12);
    ASSERT_EQ(ew16(g, 0x600), 15);
}

TEST(cheat_disabled_does_nothing_and_toggle_remove) {
    GBA* g = make_gba();
    cheat_add_text(&g->cheats, "82000700 0001\n", "a", CHEAT_DEV_AUTO, err, sizeof(err));
    cheat_add_text(&g->cheats, "82000702 0002\n", "b", CHEAT_DEV_AUTO, err, sizeof(err));
    cheat_toggle(&g->cheats, 0);
    ASSERT_TRUE(cheat_any_enabled(&g->cheats));
    cheat_apply(&g->cheats, &g->bus);
    ASSERT_EQ(ew16(g, 0x700), 0);
    ASSERT_EQ(ew16(g, 0x702), 2);
    cheat_remove(&g->cheats, 0);
    ASSERT_EQ(g->cheats.cheat_count, 1);
    ASSERT_STR_EQ(g->cheats.cheats[0].name, "b");
    cheat_toggle(&g->cheats, 0);
    ASSERT_TRUE(!cheat_any_enabled(&g->cheats));
}

TEST(cheat_engine_full_returns_minus_one) {
    GBA* g = make_gba();
    for (int i = 0; i < CHEAT_MAX_CHEATS; i++)
        ASSERT_EQ(cheat_add_text(&g->cheats, "82000700 0001\n", "x", CHEAT_DEV_AUTO, err, sizeof(err)), i);
    ASSERT_EQ(cheat_add_text(&g->cheats, "82000700 0001\n", "x", CHEAT_DEV_AUTO, err, sizeof(err)), -1);
    ASSERT_TRUE(strstr(err, "full") != NULL);
}

TEST(cheat_apply_costs_no_bus_side_effects_for_ram) {
    GBA* g = make_gba();
    cheat_add_text(&g->cheats, "82000800 1234\n", "x", CHEAT_DEV_AUTO, err, sizeof(err));
    Bus before = g->bus;
    cheat_apply(&g->cheats, &g->bus);
    g->bus.ewram[0x800] = before.ewram[0x800];
    g->bus.ewram[0x801] = before.ewram[0x801];
    ASSERT_MEM_EQ(&before, &g->bus, sizeof(Bus));
}

void run_cheat_tests(void) {
    TEST_SUITE("cheat");
    RUN_TEST(cheat_add_text_codebreaker_writes_ram);
    RUN_TEST(cheat_add_text_rejects_bad_paste);
    RUN_TEST(cheat_add_text_not_confident_asks_for_device);
    RUN_TEST(cheat_add_text_encrypted_par3_and_override);
    RUN_TEST(cheat_if_skips_when_false);
    RUN_TEST(cheat_par3_block_else_endif_runs_one_branch);
    RUN_TEST(cheat_signed_vs_unsigned_compare);
    RUN_TEST(cheat_keys_condition_uses_active_low_keyinput);
    RUN_TEST(cheat_slide_and_add);
    RUN_TEST(cheat_disabled_does_nothing_and_toggle_remove);
    RUN_TEST(cheat_engine_full_returns_minus_one);
    RUN_TEST(cheat_apply_costs_no_bus_side_effects_for_ram);
}
