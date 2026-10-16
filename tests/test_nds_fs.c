#include "test_harness.h"
#include "hgss/nds_fs.h"

#define TMP "test_nds_tmp.nds"

static void w16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }

/* Layout: header 0x000-0x1FF, FNT 0x200, FAT 0x300, NARC 0x400-0x443. */
static uint32_t build_rom(uint8_t* rom) {
    memset(rom, 0, 0x600);
    memcpy(rom + 0x0C, "TEST", 4);
    w32(rom + 0x40, 0x200); w32(rom + 0x44, 0x40);
    w32(rom + 0x48, 0x300); w32(rom + 0x4C, 8);
    /* FNT main table: root (dir 0) and dir 1, 8 bytes each. */
    uint8_t* fnt = rom + 0x200;
    w32(fnt + 0, 0x10); w16(fnt + 4, 0); w16(fnt + 6, 2);     /* root: sub at +0x10, 2 dirs */
    w32(fnt + 8, 0x18); w16(fnt + 12, 0); w16(fnt + 14, 0xF000);
    /* root subtable: dir "a" -> 0xF001, end */
    fnt[0x10] = 0x81; fnt[0x11] = 'a'; w16(fnt + 0x12, 0xF001); fnt[0x14] = 0;
    /* dir 1 subtable: file "x" (id 0), end */
    fnt[0x18] = 0x01; fnt[0x19] = 'x'; fnt[0x1A] = 0;
    /* FAT: file 0 = NARC at 0x400..0x444 */
    w32(rom + 0x300, 0x400); w32(rom + 0x304, 0x444);
    /* NARC: header 0x10, BTAF (0x0C + 2*8 = 0x1C), BTNF (8), GMIF (8 + 8) */
    uint8_t* n = rom + 0x400;
    memcpy(n, "NARC", 4); w16(n + 0x0C, 0x10);
    uint8_t* btaf = n + 0x10;
    memcpy(btaf, "BTAF", 4); w32(btaf + 4, 0x1C); w16(btaf + 8, 2);
    w32(btaf + 0x0C, 0); w32(btaf + 0x10, 2);      /* "hi" */
    w32(btaf + 0x14, 4); w32(btaf + 0x18, 7);      /* "abc" (4-aligned start) */
    uint8_t* btnf = btaf + 0x1C;
    memcpy(btnf, "BTNF", 4); w32(btnf + 4, 8);
    uint8_t* gmif = btnf + 8;
    memcpy(gmif, "GMIF", 4); w32(gmif + 4, 8 + 8);
    memcpy(gmif + 8, "hi\0\0abc", 7);
    return 0x444;
}

static bool write_rom(uint32_t len, const uint8_t* rom) {
    FILE* f = fopen(TMP, "wb");
    if (!f) return false;
    bool ok = fwrite(rom, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

TEST(nds_open_reads_code) {
    static uint8_t rom[0x600];
    ASSERT_TRUE(write_rom(build_rom(rom), rom));
    NdsRom r;
    ASSERT_TRUE(nds_open(&r, TMP));
    ASSERT_STR_EQ(r.code, "TEST");
    nds_close(&r);
    remove(TMP);
}

TEST(nds_find_and_narc_members) {
    static uint8_t rom[0x600];
    ASSERT_TRUE(write_rom(build_rom(rom), rom));
    NdsRom r;
    ASSERT_TRUE(nds_open(&r, TMP));
    NdsSpan narc, m;
    ASSERT_TRUE(nds_find(&r, "a/x", &narc));
    ASSERT_EQ(narc.start, 0x400);
    uint16_t count = 0;
    ASSERT_TRUE(narc_count(&r, narc, &count));
    ASSERT_EQ(count, 2);
    ASSERT_TRUE(narc_member(&r, narc, 1, &m));
    ASSERT_EQ(m.end - m.start, 3);
    char buf[8] = {0};
    uint32_t len = 0;
    ASSERT_TRUE(narc_read(&r, narc, 1, buf, sizeof(buf), &len));
    ASSERT_EQ(len, 3);
    ASSERT_MEM_EQ(buf, "abc", 3);
    ASSERT_TRUE(!nds_find(&r, "a/y", &narc));
    ASSERT_TRUE(!nds_find(&r, "b/x", &narc));
    nds_close(&r);
    remove(TMP);
}

TEST(nds_open_rejects_truncated) {
    static uint8_t rom[0x600];
    build_rom(rom);
    ASSERT_TRUE(write_rom(0x250, rom));   /* FAT and NARC cut off */
    NdsRom r;
    ASSERT_TRUE(!nds_open(&r, TMP));
    ASSERT_TRUE(!nds_open(&r, "does_not_exist.nds"));
    remove(TMP);
}

TEST(narc_member_rejects_out_of_range) {
    static uint8_t rom[0x600];
    uint32_t len = build_rom(rom);
    w32(rom + 0x400 + 0x10 + 0x18, 0x1000);   /* member 1 end far past GMIF */
    ASSERT_TRUE(write_rom(len, rom));
    NdsRom r;
    ASSERT_TRUE(nds_open(&r, TMP));
    NdsSpan narc, m;
    ASSERT_TRUE(nds_find(&r, "a/x", &narc));
    ASSERT_TRUE(!narc_member(&r, narc, 1, &m));
    ASSERT_TRUE(!narc_member(&r, narc, 2, &m));
    char buf[2];
    uint32_t got = 0;
    ASSERT_TRUE(!narc_read(&r, narc, 0, buf, 1, &got));   /* 2 bytes don't fit in 1 */
    nds_close(&r);
    remove(TMP);
}

TEST(narc_rejects_block_size_that_wraps) {
    static uint8_t rom[0x600];
    uint32_t len = build_rom(rom);
    /* BTAF at 0x410 with size 2^32 - 0x310: unchecked, the next block wraps
     * around to 0x100, where a planted BTNF points on to the real GMIF. */
    w32(rom + 0x410 + 4, 0xFFFFFCF0);
    memcpy(rom + 0x100, "BTNF", 4);
    w32(rom + 0x104, 0x334);
    ASSERT_TRUE(write_rom(len, rom));
    NdsRom r;
    ASSERT_TRUE(nds_open(&r, TMP));
    NdsSpan narc;
    uint16_t count = 0;
    ASSERT_TRUE(nds_find(&r, "a/x", &narc));
    ASSERT_TRUE(!narc_count(&r, narc, &count));
    nds_close(&r);
    remove(TMP);
}

TEST(narc_member_rejects_entry_outside_btaf) {
    static uint8_t rom[0x600];
    build_rom(rom);
    /* Count 6, but the BTAF block holds 2 entries. Entry 5 would sit at 0x444,
     * just past the NARC, where these bytes describe a 2-byte member. */
    w16(rom + 0x410 + 8, 6);
    w32(rom + 0x444, 0);
    w32(rom + 0x448, 2);
    ASSERT_TRUE(write_rom(0x450, rom));
    NdsRom r;
    ASSERT_TRUE(nds_open(&r, TMP));
    NdsSpan narc, m;
    ASSERT_TRUE(nds_find(&r, "a/x", &narc));
    ASSERT_TRUE(narc_member(&r, narc, 1, &m));
    ASSERT_TRUE(!narc_member(&r, narc, 5, &m));
    nds_close(&r);
    remove(TMP);
}

void run_nds_fs_tests(void) {
    RUN_TEST(nds_open_reads_code);
    RUN_TEST(nds_find_and_narc_members);
    RUN_TEST(nds_open_rejects_truncated);
    RUN_TEST(narc_member_rejects_out_of_range);
    RUN_TEST(narc_rejects_block_size_that_wraps);
    RUN_TEST(narc_member_rejects_entry_outside_btaf);
}
