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
        if (obj_mode == 1 || obj_mode == 3) continue;  // Affine: skip for now

        // Priority: attr2 bits 11-10
        uint8_t sprite_prio = BITS(attr2, 11, 10);
        if (sprite_prio != (uint8_t)priority) continue;

        // Shape and size determine sprite dimensions
        uint8_t shape = BITS(attr0, 15, 14);
        uint8_t size  = BITS(attr1, 15, 14);

        // Guard against invalid shape (shape=3 is reserved/undefined)
        if (shape > 2) continue;

        uint8_t width  = sprite_width[shape][size];
        uint8_t height = sprite_height[shape][size];

        // Y coordinate: attr0 bits 7-0 (unsigned 0-255)
        // Values >= 160 are treated as negative (wrap around): y - 256
        int32_t sprite_y = BITS(attr0, 7, 0);
        if (sprite_y >= 160) sprite_y -= 256;

        // Check if the sprite intersects the current scanline
        int32_t local_y = (int32_t)scanline - sprite_y;
        if (local_y < 0 || local_y >= height) continue;

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

        // Flip flags (regular mode only)
        bool h_flip = BIT(attr1, 12);
        bool v_flip = BIT(attr1, 13);

        // Apply vertical flip to the local Y coordinate
        int32_t tex_y = v_flip ? (height - 1 - local_y) : local_y;

        // Render each pixel of this sprite row
        for (int32_t px = 0; px < width; px++) {
            int32_t screen_x = sprite_x + px;

            // Clip to visible screen area
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            // Apply horizontal flip
            int32_t tex_x = h_flip ? (width - 1 - px) : px;

            // Compute the tile number for this pixel
            uint16_t tile_num;
            uint32_t tile_row = (uint32_t)tex_y / 8;
            uint32_t tile_col = (uint32_t)tex_x / 8;
            uint32_t pixel_row = (uint32_t)tex_y % 8;
            uint32_t pixel_col = (uint32_t)tex_x % 8;

            if (color_8bpp) {
                // 8bpp tile mapping
                if (mapping_1d) {
                    tile_num = base_tile
                             + (uint16_t)(tile_row * ((uint32_t)width / 8) + tile_col) * 2;
                } else {
                    tile_num = base_tile
                             + (uint16_t)(tile_row * 32 + tile_col * 2);
                }
            } else {
                // 4bpp tile mapping
                if (mapping_1d) {
                    tile_num = base_tile
                             + (uint16_t)(tile_row * ((uint32_t)width / 8) + tile_col);
                } else {
                    tile_num = base_tile
                             + (uint16_t)(tile_row * 32 + tile_col);
                }
            }

            // Compute VRAM offset for the pixel within the tile.
            // Each tile is 32 bytes. OBJ tiles start at VRAM offset 0x10000.
            uint32_t tile_addr = OBJ_TILE_BASE + (uint32_t)tile_num * 32;
            uint8_t color_idx;

            if (color_8bpp) {
                // 8bpp: 8 bytes per row, 1 byte per pixel
                uint32_t offset = tile_addr + pixel_row * 8 + pixel_col;
                if (offset >= VRAM_SIZE) offset -= VRAM_MIRROR_OFFSET;
                color_idx = ppu->vram[offset];
            } else {
                // 4bpp: 4 bytes per row, each byte holds 2 pixels (low nibble = left)
                uint32_t offset = tile_addr + pixel_row * 4 + pixel_col / 2;
                if (offset >= VRAM_SIZE) offset -= VRAM_MIRROR_OFFSET;
                uint8_t byte = ppu->vram[offset];
                if (pixel_col & 1) {
                    color_idx = byte >> 4;   // High nibble = right pixel
                } else {
                    color_idx = byte & 0x0F; // Low nibble = left pixel
                }
            }

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

            ppu->scanline_buffer[screen_x] = color;
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
