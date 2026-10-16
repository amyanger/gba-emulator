#include <stddef.h>

#include "test_harness.h"
#include "hgss/hgss_gfx.h"
#include "hgss/hgss_msg.h"
#include "hgss/hgss_rs.h"
#include "hgss/nds_fs.h"

/* A field added on one side only would make Rust read or write the wrong bytes. */
TEST(hgss_rs_layout_matches_c) {
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_STRVARS), sizeof(HgssStrVars));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_FOLLOWER), sizeof(HgssFollowerGfx));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_EMOTE), sizeof(HgssEmoteGfx));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_BALL), sizeof(HgssBallGfx));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_BURST), sizeof(HgssBurstGfx));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_FRAME), sizeof(HgssFrameGfx));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_FONT), sizeof(HgssFont));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM), sizeof(NdsRom));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSSPAN), sizeof(NdsSpan));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM_SIZE), offsetof(NdsRom, size));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM_CODE), offsetof(NdsRom, code));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM_FNT), offsetof(NdsRom, fnt));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM_FNT_SIZE), offsetof(NdsRom, fnt_size));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM_FAT), offsetof(NdsRom, fat));
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_NDSROM_FAT_SIZE), offsetof(NdsRom, fat_size));
}

void run_hgss_rs_tests(void) {
    RUN_TEST(hgss_rs_layout_matches_c);
}
