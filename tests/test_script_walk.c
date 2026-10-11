#include "test_harness.h"
#include "game_fixture.h"
#include "game/script_walk.h"

static uint8_t s_rom[0x4000];
static GameMem rom_mem(void) { GameMem m = {NULL, NULL, s_rom, sizeof(s_rom)}; return m; }
static uint32_t s_pc;
static void org(uint32_t a) { s_pc = a; }
static void b(uint8_t v) { s_rom[s_pc++ - 0x08000000u] = v; }
static void b16(uint16_t v) { b((uint8_t)v); b((uint8_t)(v >> 8)); }
static void b32(uint32_t v) { b16((uint16_t)v); b16((uint16_t)(v >> 16)); }
static void give(uint16_t item) { b(0x1A); b16(0x8000); b16(item); b(0x1A); b16(0x8001); b16(1); b(0x09); b(0x00); }
static void setflag(uint16_t f) { b(0x29); b16(f); }
static void checkflag_goto(uint16_t f, uint32_t target) { b(0x2B); b16(f); b(0x06); b(0x01); b32(target); }
static void end_(void) { b(0x02); }
static ScriptGifts walk(uint32_t start) {
    ScriptGifts g;
    GameMem m = rom_mem();
    script_walk_gifts(&m, start, 5, 0x300, &g);
    return g;
}
static void reset(void) { memset(s_rom, 0, sizeof(s_rom)); }

TEST(sw_plain_gift) {
    reset(); org(0x08000100); give(0x110); setflag(0x123); end_();
    ScriptGifts g = walk(0x08000100);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(g.gift[0].item[0], 0x110);
    ASSERT_EQ(g.gift[0].item_count, 1);
    ASSERT_EQ(g.gift[0].flag, 0x123);
}

TEST(sw_gift_behind_checkflag_prefers_checked_flag) {
    reset(); org(0x08000100); checkflag_goto(0x123, 0x08000200); give(0x110); setflag(0x150); setflag(0x123); end_();
    org(0x08000200); end_();
    ScriptGifts g = walk(0x08000100);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(g.gift[0].flag, 0x123);
}

TEST(sw_gym_post_battle_gift) {
    reset(); org(0x08000100);
    b(0x5C); b(0x01); b16(7); b16(0); b32(0x08000300); b32(0x08000300); b32(0x08000200);  /* type 1, 18 bytes */
    end_();
    org(0x08000200); give(0x149); setflag(0x0A5); end_();
    org(0x08000300); end_();
    ScriptGifts g = walk(0x08000100);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(g.gift[0].item[0], 0x149);
    ASSERT_EQ(g.gift[0].flag, 0x0A5);
}

TEST(sw_choice_under_one_flag) {
    reset(); org(0x08000100); b(0x06); b(0x01); b32(0x08000200); give(0x103); setflag(0x05A); end_();
    org(0x08000200); give(0x104); setflag(0x05A); end_();
    ScriptGifts g = walk(0x08000100);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(g.gift[0].item_count, 2);
    ASSERT_EQ(g.gift[0].item[0], 0x103);
    ASSERT_EQ(g.gift[0].item[1], 0x104);
}

TEST(sw_two_flags_kept_in_walk_order) {
    reset(); org(0x08000100); b(0x06); b(0x01); b32(0x08000200); give(0x10); setflag(0x100); end_();
    org(0x08000200); give(0x20); setflag(0x101); end_();
    ScriptGifts g = walk(0x08000100);
    ASSERT_EQ(g.count, 2);
    ASSERT_EQ(g.gift[0].flag, 0x100);
    ASSERT_EQ(g.gift[1].flag, 0x101);
}

TEST(sw_three_flags_no_tag) {
    reset(); org(0x08000100);
    b(0x06); b(0x01); b32(0x08000200); b(0x06); b(0x01); b32(0x08000300); give(0x10); setflag(0x100); end_();
    org(0x08000200); give(0x20); setflag(0x101); end_();
    org(0x08000300); give(0x30); setflag(0x102); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);
}

TEST(sw_daily_or_system_flag_no_tag) {
    reset(); org(0x08000100); give(0x10); setflag(0x930); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);
    reset(); org(0x08000100); give(0x10); setflag(0x864); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);
}

TEST(sw_dynamic_item_no_tag) {
    reset(); org(0x08000100); give(0x4000); setflag(0x123); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);
}

TEST(sw_pre_flag_and_self_remove_rules) {
    reset(); org(0x08000100); b(0x2B); b16(0x200); setflag(0x200); give(0x10); end_();
    ScriptGifts g = walk(0x08000100);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(g.gift[0].flag, 0x200);
    reset(); org(0x08000100); give(0x10); b(0x53); b16(0x800F); end_();
    g = walk(0x08000100);
    ASSERT_EQ(g.count, 1);
    ASSERT_EQ(g.gift[0].flag, 0x300);      /* the template's hide flag */
    reset(); org(0x08000100); give(0x10); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);  /* no flag at all */
}

TEST(sw_self_loop_terminates) {
    reset(); org(0x08000100); b(0x05); b32(0x08000100);
    ASSERT_EQ(walk(0x08000100).count, 0);
}

TEST(sw_bad_pointer_and_unknown_opcode_stop) {
    reset(); org(0x08000100); b(0x05); b32(0x02000000);
    ASSERT_EQ(walk(0x08000100).count, 0);
    reset(); org(0x08000100); b(0xFF); give(0x10); setflag(0x123); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);
    ASSERT_EQ(walk(0x02000000).count, 0);
}

TEST(sw_budget_and_depth_limits) {
    reset(); org(0x08000100);
    for (int i = 0; i < 300; i++) b(0x00);   /* nop */
    give(0x10); setflag(0x123); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);    /* past the 256-command budget */
    reset();
    for (int i = 0; i < 4; i++) { org(0x08000100 + 0x20u * (uint32_t)i); b(0x05); b32(0x08000100 + 0x20u * (uint32_t)(i + 1)); }
    org(0x08000180); give(0x10); setflag(0x123); end_();
    ASSERT_EQ(walk(0x08000100).count, 1);    /* depth 4 reaches it */
    reset();
    for (int i = 0; i < 5; i++) { org(0x08000100 + 0x20u * (uint32_t)i); b(0x05); b32(0x08000100 + 0x20u * (uint32_t)(i + 1)); }
    org(0x080001A0); give(0x10); setflag(0x123); end_();
    ASSERT_EQ(walk(0x08000100).count, 0);    /* depth 5 does not */
}

void run_script_walk_tests(void) {
    RUN_TEST(sw_plain_gift);
    RUN_TEST(sw_gift_behind_checkflag_prefers_checked_flag);
    RUN_TEST(sw_gym_post_battle_gift);
    RUN_TEST(sw_choice_under_one_flag);
    RUN_TEST(sw_two_flags_kept_in_walk_order);
    RUN_TEST(sw_three_flags_no_tag);
    RUN_TEST(sw_daily_or_system_flag_no_tag);
    RUN_TEST(sw_dynamic_item_no_tag);
    RUN_TEST(sw_pre_flag_and_self_remove_rules);
    RUN_TEST(sw_self_loop_terminates);
    RUN_TEST(sw_bad_pointer_and_unknown_opcode_stop);
    RUN_TEST(sw_budget_and_depth_limits);
}
