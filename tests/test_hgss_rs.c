#include "test_harness.h"
#include "hgss/hgss_msg.h"
#include "hgss/hgss_rs.h"

/* A field added on one side only would make Rust read or write the wrong bytes. */
TEST(hgss_rs_layout_matches_c) {
    ASSERT_EQ(hgss_rs_layout(HGSS_RS_STRVARS), sizeof(HgssStrVars));
}

void run_hgss_rs_tests(void) {
    RUN_TEST(hgss_rs_layout_matches_c);
}
