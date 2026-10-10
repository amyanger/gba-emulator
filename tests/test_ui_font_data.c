#include "test_harness.h"
#include "ui/ui_stb.h"
#include "ui/font_inter.h"

TEST(inter_fonts_parse_and_have_glyphs) {
    stbtt_fontinfo regular, semibold;
    ASSERT_TRUE(stbtt_InitFont(&regular, k_inter_regular_ttf, 0));
    ASSERT_TRUE(stbtt_InitFont(&semibold, k_inter_semibold_ttf, 0));
    ASSERT_TRUE(stbtt_FindGlyphIndex(&regular, 'A') != 0);
    ASSERT_TRUE(stbtt_FindGlyphIndex(&regular, 0x00D7) != 0);   /* multiply sign */
    ASSERT_TRUE(stbtt_FindGlyphIndex(&regular, 0x2713) != 0);   /* check mark */
    ASSERT_TRUE(stbtt_FindGlyphIndex(&semibold, 0x00E9) != 0);  /* e acute */
    ASSERT_EQ(stbtt_FindGlyphIndex(&regular, 0x2642), 0);       /* male sign is drawn, not a glyph */
}

TEST(stb_arena_bitmap_rasterizes_without_heap) {
    stbtt_fontinfo f;
    int w = 0, h = 0, xo = 0, yo = 0;
    ASSERT_TRUE(stbtt_InitFont(&f, k_inter_regular_ttf, 0));
    float s = stbtt_ScaleForPixelHeight(&f, 26.0f);
    ui_stb_arena_reset();
    unsigned char* bmp = stbtt_GetCodepointBitmap(&f, 0, s, 'W', &w, &h, &xo, &yo);
    ASSERT_TRUE(bmp != NULL);
    ASSERT_TRUE(w > 10 && h > 10);
    int lit = 0;
    for (int i = 0; i < w * h; i++) lit += bmp[i] > 0;
    ASSERT_TRUE(lit > 20);
    ui_stb_arena_reset();
}

TEST(stb_arena_refuses_oversized_requests) {
    ui_stb_arena_reset();
    ASSERT_TRUE(ui_stb_arena_alloc(64) != NULL);
    ASSERT_TRUE(ui_stb_arena_alloc((size_t)1 << 30) == NULL);
    ui_stb_arena_reset();
}

void run_ui_font_data_tests(void) {
    TEST_SUITE("ui_font_data");
    RUN_TEST(inter_fonts_parse_and_have_glyphs);
    RUN_TEST(stb_arena_bitmap_rasterizes_without_heap);
    RUN_TEST(stb_arena_refuses_oversized_requests);
}
