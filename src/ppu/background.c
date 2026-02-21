#include "ppu.h"

// VRAM is 96KB. Addresses past 0x18000 mirror back with a 0x8000 offset.
static inline uint32_t vram_mirror(uint32_t offset) {
    if (offset >= 0x18000) {
        offset -= 0x8000;
    }
    return offset;
}

// Read a little-endian 16-bit value from VRAM at the given byte offset.
static inline uint16_t vram_read16(uint8_t* vram, uint32_t offset) {
    offset = vram_mirror(offset);
    return (uint16_t)vram[offset] | ((uint16_t)vram[offset + 1] << 8);
}

// Render a single scanline for a regular (non-affine) tiled background.
void ppu_render_bg_regular(PPU* ppu, int bg_index) {
    uint16_t bgcnt = ppu->bg_cnt[bg_index];

    // Extract BGCNT fields
    uint32_t char_base = BITS(bgcnt, 3, 2) * 0x4000;   // Tile data base address
    uint32_t screen_base = BITS(bgcnt, 12, 8) * 0x800;  // Tilemap base address
    uint32_t color_mode = BIT(bgcnt, 7);                 // 0 = 4bpp, 1 = 8bpp
    uint32_t screen_size = BITS(bgcnt, 15, 14);          // Map dimensions selector

    // Determine map dimensions in pixels from screen_size
    // 0 = 256x256, 1 = 512x256, 2 = 256x512, 3 = 512x512
    uint32_t map_width  = (screen_size & 1) ? 512 : 256;
    uint32_t map_height = (screen_size & 2) ? 512 : 256;

    // Scroll offsets (only lower 9 bits matter)
    uint32_t hofs = ppu->bg_hofs[bg_index] & 0x1FF;
    uint32_t vofs = ppu->bg_vofs[bg_index] & 0x1FF;

    // Y coordinate within the full map (wrapping)
    uint32_t map_y = (ppu->vcount + vofs) % map_height;

    // Which tile row (0-31 or 0-63) and pixel row within that tile (0-7)
    uint32_t tile_row = map_y / 8;
    uint32_t pixel_y  = map_y % 8;

    // Number of screen blocks per row: 2 if map is 512 wide, else 1
    uint32_t sbb_width = (map_width > 256) ? 2 : 1;

    for (uint32_t screen_x = 0; screen_x < SCREEN_WIDTH; screen_x++) {
        // X coordinate within the full map (wrapping)
        uint32_t map_x = (screen_x + hofs) % map_width;

        // Which tile column and pixel column within that tile
        uint32_t tile_col = map_x / 8;
        uint32_t pixel_x  = map_x % 8;

        // Determine which screen block this tile falls in.
        // Each screen block is 32x32 tiles. For maps wider/taller than 256px,
        // multiple screen blocks are arranged in a grid.
        uint32_t sbb_x = tile_col / 32;  // 0 or 1 if map is 512 wide
        uint32_t sbb_y = tile_row / 32;  // 0 or 1 if map is 512 tall
        uint32_t block_offset = sbb_y * sbb_width + sbb_x;

        // Local tile position within the 32x32 screen block
        uint32_t local_col = tile_col % 32;
        uint32_t local_row = tile_row % 32;

        // Read tilemap entry (16-bit) from VRAM
        uint32_t map_addr = screen_base
                          + block_offset * 0x800
                          + (local_row * 32 + local_col) * 2;
        uint16_t tile_entry = vram_read16(ppu->vram, map_addr);

        // Extract tilemap entry fields
        uint32_t tile_num = tile_entry & 0x3FF;        // Bits 9-0: tile number
        uint32_t h_flip   = BIT(tile_entry, 10);       // Bit 10: horizontal flip
        uint32_t v_flip   = BIT(tile_entry, 11);       // Bit 11: vertical flip
        uint32_t pal_num  = BITS(tile_entry, 15, 12);  // Bits 15-12: palette (4bpp only)

        // Apply vertical flip to select the correct row within the tile
        uint32_t ty = v_flip ? (7 - pixel_y) : pixel_y;

        // Apply horizontal flip to select the correct column within the tile
        uint32_t tx = h_flip ? (7 - pixel_x) : pixel_x;

        uint8_t color_idx;

        if (color_mode == 0) {
            // 4bpp mode: 32 bytes per tile, 4 bytes per row, 2 pixels per byte
            uint32_t tile_addr = char_base + tile_num * 32 + ty * 4 + tx / 2;
            tile_addr = vram_mirror(tile_addr);
            uint8_t byte = ppu->vram[tile_addr];

            // Low nibble = left pixel (even x), high nibble = right pixel (odd x)
            if (tx & 1) {
                color_idx = (byte >> 4) & 0x0F;
            } else {
                color_idx = byte & 0x0F;
            }

            // Palette index 0 = transparent
            if (color_idx == 0) {
                continue;
            }

            // Look up color from the sub-palette (pal_num * 16 + color_idx)
            uint32_t pal_offset = (pal_num * 16 + color_idx) * 2;
            uint16_t color = (uint16_t)ppu->palette_ram[pal_offset]
                           | ((uint16_t)ppu->palette_ram[pal_offset + 1] << 8);
            // Track layers for blending: push current top pixel down to second
            ppu->second_pixel[screen_x] = ppu->scanline_buffer[screen_x];
            ppu->second_layer[screen_x] = ppu->top_layer[screen_x];
            ppu->scanline_buffer[screen_x] = color;
            ppu->top_layer[screen_x] = (uint8_t)bg_index;
        } else {
            // 8bpp mode: 64 bytes per tile, 8 bytes per row, 1 byte per pixel
            uint32_t tile_addr = char_base + tile_num * 64 + ty * 8 + tx;
            tile_addr = vram_mirror(tile_addr);
            color_idx = ppu->vram[tile_addr];

            // Palette index 0 = transparent
            if (color_idx == 0) {
                continue;
            }

            // Look up color from the full 256-color palette
            uint32_t pal_offset = color_idx * 2;
            uint16_t color = (uint16_t)ppu->palette_ram[pal_offset]
                           | ((uint16_t)ppu->palette_ram[pal_offset + 1] << 8);
            // Track layers for blending: push current top pixel down to second
            ppu->second_pixel[screen_x] = ppu->scanline_buffer[screen_x];
            ppu->second_layer[screen_x] = ppu->top_layer[screen_x];
            ppu->scanline_buffer[screen_x] = color;
            ppu->top_layer[screen_x] = (uint8_t)bg_index;
        }
    }
}

// Render a single scanline for an affine (rotation/scaling) background.
// Only BG2 (bg_index=2) and BG3 (bg_index=3) support affine mode.
// Per GBATEK: affine BGs use 8bpp tiles with single-byte map entries.
void ppu_render_bg_affine(PPU* ppu, int bg_index) {
    uint32_t affine_idx = (uint32_t)(bg_index - 2);  /* BG2 → 0, BG3 → 1 */
    uint16_t bgcnt = ppu->bg_cnt[bg_index];

    // Extract BGCNT fields
    uint32_t char_base   = BITS(bgcnt, 3, 2) * 0x4000;
    uint32_t screen_base = BITS(bgcnt, 12, 8) * 0x800;
    uint32_t wraparound  = BIT(bgcnt, 13);
    uint32_t screen_size = BITS(bgcnt, 15, 14);

    // Affine map dimensions: 16, 32, 64, or 128 tiles per side
    static const uint32_t size_lut[4] = { 16, 32, 64, 128 };
    uint32_t tiles_per_side = size_lut[screen_size];
    int32_t map_size_px = (int32_t)(tiles_per_side * 8);

    // Read the current internal reference point (NOT the latch).
    // These are used for the horizontal walk across the scanline.
    // The caller (ppu.c) advances bg_ref_x/y by PB/PD after each scanline.
    int32_t px = ppu->bg_ref_x[affine_idx];
    int32_t py = ppu->bg_ref_y[affine_idx];

    // PA and PC control horizontal stepping within a scanline.
    int16_t pa = ppu->bg_pa[affine_idx];
    int16_t pc = ppu->bg_pc[affine_idx];

    for (uint32_t screen_x = 0; screen_x < SCREEN_WIDTH; screen_x++) {
        // Convert from 8.8 fixed-point to integer pixel coordinates
        int32_t tex_x = px >> 8;
        int32_t tex_y = py >> 8;

        // Bounds check / wraparound
        if (wraparound) {
            // Modulo that handles negative values correctly
            tex_x = ((tex_x % map_size_px) + map_size_px) % map_size_px;
            tex_y = ((tex_y % map_size_px) + map_size_px) % map_size_px;
        } else {
            if (tex_x < 0 || tex_x >= map_size_px ||
                tex_y < 0 || tex_y >= map_size_px) {
                // Outside map and no wraparound — transparent
                px += (int32_t)pa;
                py += (int32_t)pc;
                continue;
            }
        }

        // Affine tilemap entries are 8-bit (one byte per tile, no flip bits)
        uint32_t map_entry_addr = screen_base
                                + (uint32_t)(tex_y / 8) * tiles_per_side
                                + (uint32_t)(tex_x / 8);
        uint8_t tile_num = ppu->vram[vram_mirror(map_entry_addr)];

        // Always 8bpp: 64 bytes per tile, 8 bytes per row, 1 byte per pixel
        uint32_t tile_addr = char_base
                           + (uint32_t)tile_num * 64
                           + (uint32_t)(tex_y % 8) * 8
                           + (uint32_t)(tex_x % 8);
        uint8_t pixel = ppu->vram[vram_mirror(tile_addr)];

        if (pixel != 0) {
            // Look up color from the 256-color palette
            uint32_t pal_offset = (uint32_t)pixel * 2;
            uint16_t color = (uint16_t)ppu->palette_ram[pal_offset]
                           | ((uint16_t)ppu->palette_ram[pal_offset + 1] << 8);
            // Track layers for blending: push current top pixel down to second
            ppu->second_pixel[screen_x] = ppu->scanline_buffer[screen_x];
            ppu->second_layer[screen_x] = ppu->top_layer[screen_x];
            ppu->scanline_buffer[screen_x] = color;
            ppu->top_layer[screen_x] = (uint8_t)bg_index;
        }

        // Advance texture coordinates by PA/PC for the next screen pixel
        px += (int32_t)pa;
        py += (int32_t)pc;
    }
}
