#include "test_harness.h"

#ifdef ENABLE_REWIND

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
    bool ok = rewind_init(&rb, 1800, 0);
    ASSERT_TRUE(ok);
    ASSERT_EQ(rewind_depth(&rb), 0);
    rewind_shutdown(&rb);
    /* Double shutdown must be safe */
    rewind_shutdown(&rb);
}

TEST(rewind_record_increments_count) {
    RewindBuffer rb;
    rewind_init(&rb, 8, 0);
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
    rewind_init(&rb, 4, 0);
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
    rewind_init(&rb, 16, 0);
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

TEST(rewind_handles_incompressible_payload) {
    RewindBuffer rb;
    rewind_init(&rb, 4, 0);
    GBA gba;
    gba_init(&gba);

    /* Fill EWRAM with pseudo-random bytes (high entropy) — LZ4 ratio ~1.0. */
    uint32_t s = 0x12345678u;
    for (size_t i = 0; i < sizeof(gba.bus.ewram); i++) {
        s = s * 1103515245u + 12345u;
        gba.bus.ewram[i] = (uint8_t)(s >> 16);
    }
    rewind_record_frame(&rb, &gba);
    ASSERT_EQ(rewind_depth(&rb), 1);

    /* Trash and replay. */
    uint8_t expected = gba.bus.ewram[0];
    memset(gba.bus.ewram, 0, sizeof(gba.bus.ewram));
    ASSERT_TRUE(rewind_begin(&rb));
    ASSERT_TRUE(rewind_step(&rb, &gba));
    ASSERT_EQ(gba.bus.ewram[0], expected);
    rewind_end(&rb);

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_recovers_from_corrupt_slot) {
    RewindBuffer rb;
    rewind_init(&rb, 4, 0);
    GBA gba;
    gba_init(&gba);

    rewind_record_frame(&rb, &gba);
    ASSERT_EQ(rewind_depth(&rb), 1);

    /* Corrupt the first slot's payload — flip a byte well inside the data. */
    rb.slots[0].data[16] ^= 0xFFu;

    ASSERT_TRUE(rewind_begin(&rb));
    bool ok = rewind_step(&rb, &gba);
    ASSERT_TRUE(!ok);                 /* must report failure cleanly */
    ASSERT_TRUE(!rewind_active(&rb)); /* must auto-end on error */

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_clear_resets_buffer) {
    RewindBuffer rb;
    rewind_init(&rb, 4, 0);
    GBA gba;
    gba_init(&gba);

    rewind_record_frame(&rb, &gba);
    rewind_record_frame(&rb, &gba);
    rewind_record_frame(&rb, &gba);
    ASSERT_TRUE(rewind_depth(&rb) > 0);
    ASSERT_TRUE(rewind_bytes_used(&rb) > 0);

    rewind_clear(&rb);
    ASSERT_EQ(rewind_depth(&rb), 0);
    ASSERT_EQ(rewind_bytes_used(&rb), 0);
    ASSERT_TRUE(!rewind_active(&rb));

    /* Buffer should be reusable after clear. */
    rewind_record_frame(&rb, &gba);
    rewind_record_frame(&rb, &gba);
    ASSERT_TRUE(rewind_depth(&rb) > 0);

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

/* Fill EWRAM with high-entropy bytes so LZ4 can't compress and every
 * snapshot's payload is close to the raw state size. */
static void fill_ewram_entropy(GBA* gba, uint32_t seed) {
    uint32_t s = seed;
    for (size_t i = 0; i < sizeof(gba->bus.ewram); i++) {
        s = s * 1103515245u + 12345u;
        gba->bus.ewram[i] = (uint8_t)(s >> 16);
    }
}

TEST(rewind_bytes_used_tracks_slot_allocations) {
    RewindBuffer rb;
    rewind_init(&rb, 4, 0);  /* unlimited budget */
    GBA gba;
    gba_init(&gba);

    /* Four big incompressible snapshots grow every slot's allocation. */
    fill_ewram_entropy(&gba, 0x1234u);
    for (int i = 0; i < 8; i++) {
        rewind_record_frame(&rb, &gba);
    }
    ASSERT_EQ(rewind_depth(&rb), 4);

    /* Four small snapshots (zeroed EWRAM compresses hard) overwrite the ring.
     * The big allocations are retained for reuse and must stay counted. */
    memset(gba.bus.ewram, 0, sizeof(gba.bus.ewram));
    for (int i = 0; i < 8; i++) {
        rewind_record_frame(&rb, &gba);
    }

    size_t caps = 0;
    for (uint32_t i = 0; i < rb.capacity; i++) {
        caps += rb.slots[i].cap;
    }
    ASSERT_EQ(rewind_bytes_used(&rb), caps);

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_evicts_oldest_when_over_byte_budget) {
    RewindBuffer rb;
    GBA gba;
    gba_init(&gba);
    fill_ewram_entropy(&gba, 0x5678u);

    /* Measure one snapshot's allocated footprint. */
    rewind_init(&rb, 64, 0);
    rewind_record_frame(&rb, &gba);
    size_t one = rewind_bytes_used(&rb);
    ASSERT_TRUE(one > 0);
    rewind_shutdown(&rb);

    /* Budget for ~2.5 snapshots; record 4. Oldest must be evicted. */
    rewind_init(&rb, 64, one * 2 + one / 2);
    for (int i = 0; i < 8; i++) {
        gba.bus.ewram[0] = (uint8_t)(0x10 + i);  /* marker per call */
        rewind_record_frame(&rb, &gba);
    }
    ASSERT_TRUE(rewind_bytes_used(&rb) <= rb.max_bytes);
    ASSERT_TRUE(rewind_depth(&rb) >= 1);
    ASSERT_TRUE(rewind_depth(&rb) < 4);

    /* The newest snapshot (marker from call i=6) must survive eviction. */
    ASSERT_TRUE(rewind_begin(&rb));
    gba.bus.ewram[0] = 0xEE;
    ASSERT_TRUE(rewind_step(&rb, &gba));
    ASSERT_EQ(gba.bus.ewram[0], 0x16);
    rewind_end(&rb);

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_step_releases_slot_memory) {
    RewindBuffer rb;
    rewind_init(&rb, 8, 0);
    GBA gba;
    gba_init(&gba);
    fill_ewram_entropy(&gba, 0xDEF0u);

    for (int i = 0; i < 4; i++) {
        rewind_record_frame(&rb, &gba);  /* 2 recorded */
    }
    size_t before = rewind_bytes_used(&rb);
    ASSERT_TRUE(before > 0);

    /* Popped frames must return their memory, or a deep rewind near the
     * byte budget leaves stale allocations that starve later recording. */
    ASSERT_TRUE(rewind_begin(&rb));
    ASSERT_TRUE(rewind_step(&rb, &gba));
    ASSERT_TRUE(rewind_bytes_used(&rb) < before);
    ASSERT_TRUE(rewind_step(&rb, &gba));
    ASSERT_EQ(rewind_bytes_used(&rb), 0);
    rewind_end(&rb);

    gba_destroy(&gba);
    rewind_shutdown(&rb);
}

TEST(rewind_keeps_newest_frame_when_budget_tiny) {
    RewindBuffer rb;
    rewind_init(&rb, 8, 1);  /* budget smaller than any snapshot */
    GBA gba;
    gba_init(&gba);
    fill_ewram_entropy(&gba, 0x9ABCu);

    /* Every record evicts the previous frame but must keep the newest. */
    for (int i = 0; i < 4; i++) {
        gba.bus.ewram[0] = (uint8_t)(0x20 + i);
        rewind_record_frame(&rb, &gba);
    }
    ASSERT_EQ(rewind_depth(&rb), 1);

    ASSERT_TRUE(rewind_begin(&rb));
    gba.bus.ewram[0] = 0xEE;
    ASSERT_TRUE(rewind_step(&rb, &gba));
    ASSERT_EQ(gba.bus.ewram[0], 0x22);  /* marker from call i=2, the last even */
    rewind_end(&rb);

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
    RUN_TEST(rewind_handles_incompressible_payload);
    RUN_TEST(rewind_recovers_from_corrupt_slot);
    RUN_TEST(rewind_clear_resets_buffer);
    RUN_TEST(rewind_bytes_used_tracks_slot_allocations);
    RUN_TEST(rewind_evicts_oldest_when_over_byte_budget);
    RUN_TEST(rewind_step_releases_slot_memory);
    RUN_TEST(rewind_keeps_newest_frame_when_budget_tiny);
}

#else /* !ENABLE_REWIND */

void run_rewind_tests(void) {
    /* Rewind feature disabled — no tests to run. */
}

#endif /* ENABLE_REWIND */
