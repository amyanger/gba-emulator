#ifndef HGSS_GFX_H
#define HGSS_GFX_H

#include <stdbool.h>
#include <stdint.h>

#define HGSS_FOLLOW_FRAMES 8          /* 0-1 up, 2-3 down, 4-5 left, 6-7 right */
#define HGSS_FOLLOW_MAX    64
#define HGSS_FONT_GLYPHS   512

typedef struct {
    uint8_t size;                                         /* 32 or 64 */
    uint8_t px[HGSS_FOLLOW_FRAMES][HGSS_FOLLOW_MAX * HGSS_FOLLOW_MAX];   /* palette index, row stride = size */
    uint16_t pal[2][16];                                  /* [0] normal, [1] shiny, BGR555 */
} HgssFollowerGfx;

typedef struct {
    uint8_t px[2][32 * 32];
    uint16_t pal[16];
} HgssEmoteGfx;

typedef struct {
    uint8_t px[16 * 16];           /* 4bpp palette indices, 0 transparent */
    uint16_t pal[16];
} HgssBallGfx;

typedef struct {
    uint8_t px[32 * 32];           /* 0 transparent, else colour index + 1 (1..8) */
    uint16_t pal[16];              /* pal[1 + i] = colour i, pal[0] unused */
} HgssBurstGfx;

typedef struct {
    uint8_t tile[18][64];          /* 6x3 frame tiles, 8x8 palette indices */
    uint16_t pal[16];
} HgssFrameGfx;

typedef struct {
    uint16_t count;
    uint8_t width[HGSS_FONT_GLYPHS];
    uint8_t glyph[HGSS_FONT_GLYPHS][64];   /* raw 2bpp, 4 sub-tiles of 16 bytes */
    uint16_t pal[16];
} HgssFont;

bool hgss_gfx_follower(const uint8_t* btx, uint32_t len, HgssFollowerGfx* out);
bool hgss_gfx_emote(const uint8_t* btx, uint32_t len, HgssEmoteGfx* out);
/* Field-effect models are BMD0 files; only their TEX0 block is read. */
bool hgss_gfx_ball(const uint8_t* bmd, uint32_t len, HgssBallGfx* out);
bool hgss_gfx_burst(const uint8_t* bmd, uint32_t len, HgssBurstGfx* out);
bool hgss_gfx_frame(const uint8_t* ncgr, uint32_t ncgr_len, const uint8_t* nclr, uint32_t nclr_len,
                    HgssFrameGfx* out);
bool hgss_gfx_font(const uint8_t* font, uint32_t len, const uint8_t* nclr, uint32_t nclr_len, HgssFont* out);
/* 0 skip, 1 fg, 2 shadow, 3 bg for glyph pixel (x 0..15, y 0..15). */
uint8_t hgss_font_pixel(const HgssFont* f, uint16_t glyph, uint32_t x, uint32_t y);

#endif // HGSS_GFX_H
