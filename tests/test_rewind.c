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
#include "gba.h"

TEST(rewind_init_shutdown_clean) {
    RewindBuffer rb;
    bool ok = rewind_init(&rb, 1800);
    ASSERT_TRUE(ok);
    ASSERT_EQ(rewind_depth(&rb), 0);
    rewind_shutdown(&rb);
    /* Double shutdown must be safe */
    rewind_shutdown(&rb);
}

TEST(rewind_record_increments_count) {
    RewindBuffer rb;
    rewind_init(&rb, 8);
    GBA gba;
    gba_init(&gba);

    /* Even frames are recorded; odd frames are skipped per spec. */
    rewind_record_frame(&rb, &gba);  /* frame_counter 0 (even) -> recorded */
    rewind_record_frame(&rb, &gba);  /* 1 odd -> skipped */
    rewind_record_frame(&rb, &gba);  /* 2 even -> recorded */
    ASSERT_EQ(rewind_depth(&rb), 2);

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_ring_wraps_at_capacity) {
    RewindBuffer rb;
    rewind_init(&rb, 4);
    GBA gba;
    gba_init(&gba);

    /* Need 4 *recorded* frames to fill, plus more to overflow. Even frames
     * only, so 8 calls = 4 recorded; 16 calls = 8 recorded -> wraps. */
    for (int i = 0; i < 16; i++) {
        rewind_record_frame(&rb, &gba);
    }
    ASSERT_EQ(rewind_depth(&rb), 4);   /* saturated at capacity */

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_step_restores_prior_state) {
    RewindBuffer rb;
    rewind_init(&rb, 16);
    GBA gba;
    gba_init(&gba);

    /* Frame A: stamp EWRAM with 0xA1 */
    gba.bus.ewram[0] = 0xA1;
    rewind_record_frame(&rb, &gba);  /* even -> recorded */
    rewind_record_frame(&rb, &gba);  /* odd -> skipped */

    /* Frame B: stamp EWRAM with 0xB2 */
    gba.bus.ewram[0] = 0xB2;
    rewind_record_frame(&rb, &gba);  /* even -> recorded */

    ASSERT_EQ(rewind_depth(&rb), 2);

    /* Begin rewind, step once -> should restore to frame B's state (the
     * most recent recorded snapshot, since head was advanced past it). */
    bool ok = rewind_begin(&rb);
    ASSERT_TRUE(ok);

    gba.bus.ewram[0] = 0xCC;  /* trash current */
    ok = rewind_step(&rb, &gba);
    ASSERT_TRUE(ok);
    ASSERT_EQ(gba.bus.ewram[0], 0xB2);

    /* Step again -> restores frame A. */
    ok = rewind_step(&rb, &gba);
    ASSERT_TRUE(ok);
    ASSERT_EQ(gba.bus.ewram[0], 0xA1);

    /* No more frames; next step returns false. */
    ok = rewind_step(&rb, &gba);
    ASSERT_TRUE(!ok);

    rewind_end(&rb);
    ASSERT_TRUE(!rewind_active(&rb));

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

void run_rewind_tests(void) {
    TEST_SUITE("rewind");
    RUN_TEST(lz4_roundtrip_smoke);
    RUN_TEST(rewind_init_shutdown_clean);
    RUN_TEST(rewind_record_increments_count);
    RUN_TEST(rewind_ring_wraps_at_capacity);
    RUN_TEST(rewind_step_restores_prior_state);
}
