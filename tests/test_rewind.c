#include "test_harness.h"
#include "rewind/rewind_lz4.h"
#include <string.h>

TEST(lz4_roundtrip_smoke) {
    const char* src = "the quick brown fox jumps over the lazy dog "
                      "the quick brown fox jumps over the lazy dog";
    int src_len = (int)strlen(src);
    char compressed[256];
    int c_len = LZ4_compress_default(src, compressed, src_len, sizeof(compressed));
    ASSERT_TRUE(c_len > 0);

    char decompressed[256];
    int d_len = LZ4_decompress_safe(compressed, decompressed, c_len, sizeof(decompressed));
    ASSERT_EQ(d_len, src_len);
    ASSERT_MEM_EQ(decompressed, src, (size_t)src_len);
}

#include "rewind/rewind.h"

TEST(rewind_init_shutdown_clean) {
    RewindBuffer rb;
    bool ok = rewind_init(&rb, 1800);
    ASSERT_TRUE(ok);
    ASSERT_EQ(rewind_depth(&rb), 0);
    rewind_shutdown(&rb);
    /* Double shutdown must be safe */
    rewind_shutdown(&rb);
}

void run_rewind_tests(void) {
    TEST_SUITE("rewind");
    RUN_TEST(lz4_roundtrip_smoke);
    RUN_TEST(rewind_init_shutdown_clean);
}
