#include "ppu.h"

// Compute affine-transformed texture coordinates for a background pixel
void ppu_affine_transform(int16_t pa, int16_t pb, int16_t pc, int16_t pd,
                          int32_t ref_x, int32_t ref_y, int screen_x,
                          int32_t* tex_x, int32_t* tex_y) {
    // Affine transform: texture_coord = ref_point + matrix * screen_offset
    // tex_x = ref_x + pa * screen_x  (ref_y uses pc)
    // tex_y = ref_y + pb * screen_x  (note: per-scanline, ref advances by pc/pd)

    // Fixed point: reference points are 19.8, pa/pb/pc/pd are 8.8
    *tex_x = ref_x + pa * screen_x;
    *tex_y = ref_y + pc * screen_x;
}

// Transform sprite coordinates for affine sprites
void ppu_affine_sprite_transform(int16_t pa, int16_t pb, int16_t pc, int16_t pd,
                                 int cx, int cy, int screen_x, int screen_y,
                                 int* tex_x, int* tex_y) {
    // Center-relative transformation
    int dx = screen_x - cx;
    int dy = screen_y - cy;
    *tex_x = (pa * dx + pb * dy) >> 8;
    *tex_y = (pc * dx + pd * dy) >> 8;
    *tex_x += cx;
    *tex_y += cy;
}
