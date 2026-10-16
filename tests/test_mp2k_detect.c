#include "test_harness.h"
#include "mp2k/mp2k_detect.h"
#include "game/game_profile.h"
#include <string.h>

#define SM 0x100u   /* SoundMain offset in the fake ROM */

static uint8_t s_rom[0x400];

static void put32(uint32_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) s_rom[off + i] = (uint8_t)(v >> (8 * i));
}

/* A fake ROM: 48 arbitrary "SoundMain" bytes plus a valid literal pool. */
static uint32_t build(void) {
    memset(s_rom, 0, sizeof(s_rom));
    for (uint32_t i = 0; i < 48; i++) s_rom[SM + i] = (uint8_t)(i * 7 + 3);
    put32(SM + 0x6C, 0x03007FF0u);
    put32(SM + 0x70, MP2K_ID_NUMBER);
    put32(SM + 0x74, 0x03001AA9u);
    return game_crc32(&s_rom[SM], 48);
}

TEST(mp2k_detect_finds_pool_and_hook) {
    uint32_t crc = build();
    Mp2kDriver d;
    ASSERT_TRUE(mp2k_detect_crc(s_rom, sizeof(s_rom), crc, &d));
    ASSERT_EQ(d.sound_main, 0x08000000u + SM);
    ASSERT_EQ(d.sound_info_ptr, 0x03007FF0u);
    ASSERT_EQ(d.hook_addr, 0x03001AA8u);
}

TEST(mp2k_detect_rejects_changed_code) {
    uint32_t crc = build();
    s_rom[SM + 10] ^= 1;
    Mp2kDriver d;
    ASSERT_TRUE(!mp2k_detect_crc(s_rom, sizeof(s_rom), crc, &d));
}

TEST(mp2k_detect_rejects_bad_pool) {
    Mp2kDriver d;
    uint32_t crc = build();
    put32(SM + 0x74, 0x03001AA8u);             /* even: not a Thumb entry */
    ASSERT_TRUE(!mp2k_detect_crc(s_rom, sizeof(s_rom), crc, &d));
    crc = build();
    put32(SM + 0x74, 0x08001AA9u);             /* not IWRAM */
    ASSERT_TRUE(!mp2k_detect_crc(s_rom, sizeof(s_rom), crc, &d));
    crc = build();
    put32(SM + 0x6C, 0x02007FF0u);             /* SoundInfo pointer not IWRAM */
    ASSERT_TRUE(!mp2k_detect_crc(s_rom, sizeof(s_rom), crc, &d));
}

TEST(mp2k_detect_truncated_rom) {
    uint32_t crc = build();
    Mp2kDriver d;
    /* The pool ends at SM + 0x78; a ROM cut before that must not match or overread. */
    ASSERT_TRUE(!mp2k_detect_crc(s_rom, SM + 0x76, crc, &d));
    ASSERT_TRUE(!mp2k_detect_crc(s_rom, 0, crc, &d));
}

TEST(mp2k_detect_rejects_plain_rom) {
    Mp2kDriver d;
    memset(s_rom, 0xAB, sizeof(s_rom));
    ASSERT_TRUE(!mp2k_detect(s_rom, sizeof(s_rom), &d));
}

void run_mp2k_detect_tests(void) {
    TEST_SUITE("mp2k_detect");
    RUN_TEST(mp2k_detect_finds_pool_and_hook);
    RUN_TEST(mp2k_detect_rejects_changed_code);
    RUN_TEST(mp2k_detect_rejects_bad_pool);
    RUN_TEST(mp2k_detect_truncated_rom);
    RUN_TEST(mp2k_detect_rejects_plain_rom);
}
