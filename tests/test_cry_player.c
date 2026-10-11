#include "test_harness.h"
#include "frontend/cry_player.h"

TEST(cry_decode_blocks) {
    uint8_t w[16 + 33 * 2];
    memset(w, 0, sizeof(w));
    w[0] = 1;                                   /* compressed */
    uint32_t freq = 10512u * 1024u;
    for (int i = 0; i < 4; i++) w[4 + i] = (uint8_t)(freq >> (8 * i));
    w[12] = 66;                                 /* 66 samples: one full block + 2 */
    w[16] = 10;                                 /* sample 0 = 10 */
    w[17] = 0x01;                               /* low nibble 1 -> +1 = 11 */
    w[18] = 0x2F;                               /* +4 = 15, then -1 = 14 */
    w[16 + 33] = (uint8_t)-5;                   /* block 2 sample 0 */
    w[16 + 34] = 0x08;                          /* -64 -> -69 */
    static int8_t out[128];
    uint32_t len = 0, rate = 0;
    ASSERT_TRUE(cry_decode(w, sizeof(w), out, 128, &len, &rate));
    ASSERT_EQ(len, 66);
    ASSERT_EQ(rate, 10512);
    ASSERT_EQ(out[0], 10);
    ASSERT_EQ(out[1], 11);
    ASSERT_EQ(out[2], 15);
    ASSERT_EQ(out[3], 14);
    ASSERT_EQ(out[64], -5);
    ASSERT_EQ(out[65], -69);
    ASSERT_TRUE(!cry_decode(w, 30, out, 128, &len, &rate));   /* truncated */
}

TEST(cry_mix_adds_and_stops) {
    static CryPlayer c;
    memset(&c, 0, sizeof(c));
    c.len = 4; c.rate = 4;
    c.pcm[0] = 64; c.pcm[1] = 64; c.pcm[2] = 64; c.pcm[3] = 64;
    cry_start(&c);
    int16_t out[2 * 8];
    memset(out, 0, sizeof(out));
    cry_mix(&c, out, 8, 4);
    ASSERT_TRUE(out[0] > 0);
    ASSERT_EQ(out[0], out[1]);
    ASSERT_EQ(out[2 * 7], 0);
    ASSERT_TRUE(!c.playing);
}

void run_cry_player_tests(void) {
    RUN_TEST(cry_decode_blocks);
    RUN_TEST(cry_mix_adds_and_stops);
}
