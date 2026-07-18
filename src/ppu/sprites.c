#include "ppu.h"

// OAM constants
#define OAM_ENTRY_COUNT 128
#define OAM_ENTRY_SIZE  8

// OBJ tile data starts at this VRAM offset
#define OBJ_TILE_BASE 0x10000

// OBJ palette starts at this offset in palette RAM
#define OBJ_PALETTE_BASE 0x200

// VRAM total size (96KB). Offsets must be wrapped within this range.
#define VRAM_SIZE 0x18000
#define VRAM_MIRROR_OFFSET 0x8000

// Sprite dimensions lookup table: [shape][size] = {width, height}
// Shape: 0=Square, 1=Horizontal, 2=Vertical
// Size:  0-3
static const uint8_t sprite_width[3][4] = {
    { 8, 16, 32, 64},  // Square
    {16, 32, 32, 64},  // Horizontal
    { 8,  8, 16, 32},  // Vertical
};

static const uint8_t sprite_height[3][4] = {
    { 8, 16, 32, 64},  // Square
    { 8,  8, 16, 32},  // Horizontal
    {16, 32, 32, 64},  // Vertical
};

// Fetch one OBJ texel (palette index) at texture coords (tex_x, tex_y).
// Handles 1D/2D tile mapping and 4/8bpp tile layout; width is the
// TEXTURE width in pixels (not the affine bounding box).
static uint8_t obj_fetch_texel(const PPU* ppu, bool mapping_1d, bool color_8bpp,
                               uint16_t base_tile, uint32_t width,
                               uint32_t tex_x, uint32_t tex_y) {
    uint32_t tile_row = tex_y / 8;
    uint32_t tile_col = tex_x / 8;
    uint32_t pixel_row = tex_y % 8;
    uint32_t pixel_col = tex_x % 8;

    uint16_t tile_num;
    if (color_8bpp) {
        if (mapping_1d) {
            tile_num = base_tile + (uint16_t)((tile_row * (width / 8) + tile_col) * 2);
        } else {
            tile_num = base_tile + (uint16_t)(tile_row * 32 + tile_col * 2);
        }
    } else {
        if (mapping_1d) {
            tile_num = base_tile + (uint16_t)(tile_row * (width / 8) + tile_col);
        } else {
            tile_num = base_tile + (uint16_t)(tile_row * 32 + tile_col);
        }
    }

    // Each tile is 32 bytes; OBJ tiles start at VRAM offset 0x10000.
    uint32_t tile_addr = OBJ_TILE_BASE + (uint32_t)tile_num * 32;

    if (color_8bpp) {
        // 8bpp: 8 bytes per row, 1 byte per pixel
        uint32_t offset = tile_addr + pixel_row * 8 + pixel_col;
        if (offset >= VRAM_SIZE) offset -= VRAM_MIRROR_OFFSET;
        return ppu->vram[offset];
    }
    // 4bpp: 4 bytes per row, each byte holds 2 pixels (low nibble = left)
    uint32_t offset = tile_addr + pixel_row * 4 + pixel_col / 2;
    if (offset >= VRAM_SIZE) offset -= VRAM_MIRROR_OFFSET;
    uint8_t byte = ppu->vram[offset];
    return (pixel_col & 1) ? (byte >> 4) : (byte & 0x0F);
}

// Read PA/PB/PC/PD (8.8 fixed) for the affine group in attr1 bits 13-9.
// Group N's parameters live at OAM offsets N*32 + 6/14/22/30.
static void obj_read_affine_params(const PPU* ppu, uint16_t attr1,
                                   int16_t* pa, int16_t* pb,
                                   int16_t* pc, int16_t* pd) {
    uint32_t grp = (uint32_t)BITS(attr1, 13, 9) * 32;
    *pa = (int16_t)((uint16_t)ppu->oam[grp + 6] | ((uint16_t)ppu->oam[grp + 7] << 8));
    *pb = (int16_t)((uint16_t)ppu->oam[grp + 14] | ((uint16_t)ppu->oam[grp + 15] << 8));
    *pc = (int16_t)((uint16_t)ppu->oam[grp + 22] | ((uint16_t)ppu->oam[grp + 23] << 8));
    *pd = (int16_t)((uint16_t)ppu->oam[grp + 30] | ((uint16_t)ppu->oam[grp + 31] << 8));
}

// Map a bounding-box pixel to texture coords.  For affine sprites the
// screen offset is taken relative to the bbox center, transformed by the
// 8.8 matrix, and re-centered on the texture; out-of-bounds -> false
// (transparent).  Regular sprites just apply the flip bits.
static bool obj_texcoord(int32_t px, int32_t local_y, bool affine,
                         int16_t pa, int16_t pb, int16_t pc, int16_t pd,
                         int32_t bbox_w, int32_t bbox_h,
                         int32_t width, int32_t height,
                         bool h_flip, bool v_flip,
                         int32_t* tex_x, int32_t* tex_y) {
    if (affine) {
        int32_t dx = px - bbox_w / 2;
        int32_t dy = local_y - bbox_h / 2;
        *tex_x = ((pa * dx + pb * dy) >> 8) + width / 2;
        *tex_y = ((pc * dx + pd * dy) >> 8) + height / 2;
        return *tex_x >= 0 && *tex_x < width && *tex_y >= 0 && *tex_y < height;
    }
    *tex_x = h_flip ? (width - 1 - px) : px;
    *tex_y = v_flip ? (height - 1 - local_y) : local_y;
    return true;
}

// Render all sprites whose OAM priority field matches the given priority level.
// Iterates OAM entries from 127 down to 0 so that lower-numbered entries
// overwrite higher-numbered ones (lower index = higher display priority among
// sprites at the same priority level).
void ppu_render_sprites_at_priority(PPU* ppu, int priority) {
    // OBJ enable is DISPCNT bit 12. If OBJ layer is disabled, nothing to do.
    if (!BIT(ppu->dispcnt, 12)) return;

    // DISPCNT bit 6: OBJ tile mapping mode (0=2D, 1=1D)
    bool mapping_1d = BIT(ppu->dispcnt, 6);

    uint16_t scanline = ppu->vcount;

    for (int32_t i = OAM_ENTRY_COUNT - 1; i >= 0; i--) {
        uint32_t oam_base = (uint32_t)i * OAM_ENTRY_SIZE;

        // Read OAM attributes (little-endian)
        uint16_t attr0 = (uint16_t)ppu->oam[oam_base]
                       | ((uint16_t)ppu->oam[oam_base + 1] << 8);
        uint16_t attr1 = (uint16_t)ppu->oam[oam_base + 2]
                       | ((uint16_t)ppu->oam[oam_base + 3] << 8);
        uint16_t attr2 = (uint16_t)ppu->oam[oam_base + 4]
                       | ((uint16_t)ppu->oam[oam_base + 5] << 8);

        // OBJ Mode: attr0 bits 9-8
        // 0 = Regular, 1 = Affine, 2 = Disabled, 3 = Affine double-size
        uint8_t obj_mode = BITS(attr0, 9, 8);
        if (obj_mode == 2) continue;  // Disabled sprite
        bool affine = (obj_mode == 1) || (obj_mode == 3);

        // GFX Mode: attr0 bits 11-10
        // 0 = Normal, 1 = Semi-transparent, 2 = OBJ window
        uint8_t gfx_mode = BITS(attr0, 11, 10);

        // OBJ window sprites are not rendered visually and ignore priority.
        // They only mark pixels in the obj_window buffer.
        if (gfx_mode == 2) continue;  // Handled in a separate pass

        // Priority: attr2 bits 11-10
        uint8_t sprite_prio = BITS(attr2, 11, 10);
        if (sprite_prio != (uint8_t)priority) continue;

        // Shape and size determine sprite dimensions
        uint8_t shape = BITS(attr0, 15, 14);
        uint8_t size  = BITS(attr1, 15, 14);

        // Guard against invalid shape (shape=3 is reserved/undefined)
        if (shape > 2) continue;

        int32_t width  = sprite_width[shape][size];
        int32_t height = sprite_height[shape][size];

        // Affine double-size mode renders into a doubled bounding box.
        int32_t bbox_w = (obj_mode == 3) ? width * 2 : width;
        int32_t bbox_h = (obj_mode == 3) ? height * 2 : height;

        // Y coordinate: attr0 bits 7-0 (unsigned 0-255)
        // Values >= 160 are treated as negative (wrap around): y - 256
        int32_t sprite_y = BITS(attr0, 7, 0);
        if (sprite_y >= 160) sprite_y -= 256;

        // Check if the sprite's bounding box intersects the scanline
        int32_t local_y = (int32_t)scanline - sprite_y;
        if (local_y < 0 || local_y >= bbox_h) continue;

        // Apply vertical mosaic: snap sprite row to the top of the mosaic block.
        // OAM attr0 bit 12 enables mosaic for this sprite.
        if (BIT(attr0, 12)) {
            uint16_t obj_v_size = BITS(ppu->mosaic, 15, 12) + 1;
            if (obj_v_size > 1) {
                local_y = local_y - (local_y % (int32_t)obj_v_size);
            }
        }

        // X coordinate: attr1 bits 8-0 (9-bit signed via sign-extending bit 8)
        int32_t sprite_x = BITS(attr1, 8, 0);
        if (BIT(attr1, 8)) {
            sprite_x |= (int32_t)0xFFFFFE00;  // Sign-extend from bit 8
        }

        // Color mode: attr0 bit 13 (0=4bpp, 1=8bpp)
        bool color_8bpp = BIT(attr0, 13);

        // Tile number: attr2 bits 9-0
        uint16_t base_tile = BITS(attr2, 9, 0);

        // In 8bpp mode, hardware forces tile number bit 0 to 0
        if (color_8bpp) {
            base_tile &= ~(uint16_t)1;
        }

        // Palette number: attr2 bits 15-12 (only used in 4bpp mode)
        uint8_t pal_num = BITS(attr2, 15, 12);

        // Flip flags (regular mode only; affine repurposes these bits
        // as part of the parameter group index)
        bool h_flip = !affine && BIT(attr1, 12);
        bool v_flip = !affine && BIT(attr1, 13);

        int16_t pa = 0x100, pb = 0, pc = 0, pd = 0x100;
        if (affine) {
            obj_read_affine_params(ppu, attr1, &pa, &pb, &pc, &pd);
        }

        // Render each pixel of this bounding-box row
        for (int32_t px = 0; px < bbox_w; px++) {
            int32_t screen_x = sprite_x + px;

            // Clip to visible screen area
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            int32_t tex_x, tex_y;
            if (!obj_texcoord(px, local_y, affine, pa, pb, pc, pd,
                              bbox_w, bbox_h, width, height,
                              h_flip, v_flip, &tex_x, &tex_y)) {
                continue;
            }

            uint8_t color_idx = obj_fetch_texel(ppu, mapping_1d, color_8bpp,
                                                base_tile, (uint32_t)width,
                                                (uint32_t)tex_x, (uint32_t)tex_y);

            // Palette index 0 is transparent in both modes
            if (color_idx == 0) continue;

            // Look up the 15-bit BGR555 color from OBJ palette RAM
            uint32_t pal_addr;
            if (color_8bpp) {
                pal_addr = OBJ_PALETTE_BASE + (uint32_t)color_idx * 2;
            } else {
                pal_addr = OBJ_PALETTE_BASE
                         + ((uint32_t)pal_num * 16 + (uint32_t)color_idx) * 2;
            }

            uint16_t color = (uint16_t)ppu->palette_ram[pal_addr]
                           | ((uint16_t)ppu->palette_ram[pal_addr + 1] << 8);

            // Track layers for blending: push current top pixel down to second
            ppu->second_pixel[screen_x] = ppu->scanline_buffer[screen_x];
            ppu->second_layer[screen_x] = ppu->top_layer[screen_x];
            ppu->scanline_buffer[screen_x] = color;
            ppu->top_layer[screen_x] = 4;  // OBJ layer
            ppu->obj_mosaic[screen_x] = BIT(attr0, 12);
            ppu->obj_semitransparent[screen_x] = (gfx_mode == 1);
        }
    }
}

// Render all sprites for the current scanline across all priority levels.
// Priority 3 (lowest) is drawn first, priority 0 (highest) drawn last, so
// higher-priority sprites overwrite lower-priority ones.
void ppu_render_sprites(PPU* ppu) {
    for (int p = 3; p >= 0; p--) {
        ppu_render_sprites_at_priority(ppu, p);
    }
}

// Build the OBJ window mask for the current scanline.
// Sprites with GFX mode 2 (attr0 bits 11-10) mark non-transparent pixels in
// ppu->obj_window[]. These sprites are NOT drawn visually; they only define
// the OBJ window region. Priority and blending are irrelevant for them.
void ppu_build_obj_window(PPU* ppu) {
    memset(ppu->obj_window, 0, sizeof(ppu->obj_window));

    // OBJ window requires DISPCNT bit 15 (OBJ Window Display Flag)
    if (!BIT(ppu->dispcnt, 15)) return;

    // Also need OBJ layer enabled
    if (!BIT(ppu->dispcnt, 12)) return;

    bool mapping_1d = BIT(ppu->dispcnt, 6);
    uint16_t scanline = ppu->vcount;

    for (int32_t i = OAM_ENTRY_COUNT - 1; i >= 0; i--) {
        uint32_t oam_base = (uint32_t)i * OAM_ENTRY_SIZE;

        uint16_t attr0 = (uint16_t)ppu->oam[oam_base]
                       | ((uint16_t)ppu->oam[oam_base + 1] << 8);
        uint16_t attr1 = (uint16_t)ppu->oam[oam_base + 2]
                       | ((uint16_t)ppu->oam[oam_base + 3] << 8);
        uint16_t attr2 = (uint16_t)ppu->oam[oam_base + 4]
                       | ((uint16_t)ppu->oam[oam_base + 5] << 8);

        uint8_t obj_mode = BITS(attr0, 9, 8);
        if (obj_mode == 2) continue;  // Disabled
        bool affine = (obj_mode == 1) || (obj_mode == 3);

        // Only process GFX mode 2 (OBJ window) sprites
        uint8_t gfx_mode = BITS(attr0, 11, 10);
        if (gfx_mode != 2) continue;

        uint8_t shape = BITS(attr0, 15, 14);
        uint8_t size  = BITS(attr1, 15, 14);
        if (shape > 2) continue;

        int32_t width  = sprite_width[shape][size];
        int32_t height = sprite_height[shape][size];
        int32_t bbox_w = (obj_mode == 3) ? width * 2 : width;
        int32_t bbox_h = (obj_mode == 3) ? height * 2 : height;

        int32_t sprite_y = BITS(attr0, 7, 0);
        if (sprite_y >= 160) sprite_y -= 256;

        int32_t local_y = (int32_t)scanline - sprite_y;
        if (local_y < 0 || local_y >= bbox_h) continue;

        int32_t sprite_x = BITS(attr1, 8, 0);
        if (BIT(attr1, 8)) {
            sprite_x |= (int32_t)0xFFFFFE00;
        }

        bool color_8bpp = BIT(attr0, 13);
        uint16_t base_tile = BITS(attr2, 9, 0);
        if (color_8bpp) base_tile &= ~(uint16_t)1;

        bool h_flip = !affine && BIT(attr1, 12);
        bool v_flip = !affine && BIT(attr1, 13);

        int16_t pa = 0x100, pb = 0, pc = 0, pd = 0x100;
        if (affine) {
            obj_read_affine_params(ppu, attr1, &pa, &pb, &pc, &pd);
        }

        for (int32_t px = 0; px < bbox_w; px++) {
            int32_t screen_x = sprite_x + px;
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            int32_t tex_x, tex_y;
            if (!obj_texcoord(px, local_y, affine, pa, pb, pc, pd,
                              bbox_w, bbox_h, width, height,
                              h_flip, v_flip, &tex_x, &tex_y)) {
                continue;
            }

            uint8_t color_idx = obj_fetch_texel(ppu, mapping_1d, color_8bpp,
                                                base_tile, (uint32_t)width,
                                                (uint32_t)tex_x, (uint32_t)tex_y);

            // Only non-transparent pixels contribute to the OBJ window
            if (color_idx != 0) {
                ppu->obj_window[screen_x] = true;
            }
        }
    }
}
