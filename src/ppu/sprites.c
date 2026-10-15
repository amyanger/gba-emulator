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

// GBATEK "LCD OBJ - OAM Attributes", attr2 tile number: in BG modes 3-5 the
// lower 16K of OBJ VRAM belongs to the bitmap, so "only tile numbers 512-1023
// may be used ... attempts to use tiles 0-511 are ignored (not displayed)".
// Matches mGBA, which skips the whole sprite on the base tile number.
static bool obj_tile_hidden_in_bitmap_mode(const PPU* ppu, uint16_t base_tile) {
    return (ppu->dispcnt & 0x7) >= 3 && base_tile < 512;
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

static void extra_obj_render(PPU* ppu, const PpuExtraObj* e) {
    int32_t local_y = (int32_t)ppu->vcount - e->y;
    if (local_y < 0 || local_y >= e->h) return;
    const uint8_t* row = e->pixels + (uint32_t)local_y * e->w;
    for (int32_t px = 0; px < e->w; px++) {
        int32_t sx = e->x + px;
        if (sx < 0 || sx >= SCREEN_WIDTH || row[px] == 0) continue;
        if (ppu_push_pixel(ppu, (uint32_t)sx, e->pal[row[px] & 15], 4)) {
            ppu->obj_mosaic[sx] = false;
            ppu->obj_semitransparent[sx] = false;
        }
    }
}

/* Extra OBJs of this priority whose slot equals `slot`, in list order. */
static void extra_objs_at(PPU* ppu, int priority, int32_t slot) {
    for (int k = 0; k < PPU_EXTRA_OBJ_MAX; k++) {
        const PpuExtraObj* e = &ppu->extra_obj[k];
        if (e->on && e->pixels && e->priority == priority && e->slot == slot) extra_obj_render(ppu, e);
    }
}

// One OAM entry decoded for the current scanline.
typedef struct {
    uint16_t attr0;
    uint8_t gfx_mode, pal_num;
    bool affine, color_8bpp, h_flip, v_flip;
    uint16_t base_tile;
    int32_t x, local_y, width, height, bbox_w, bbox_h;
    int16_t pa, pb, pc, pd;
} ObjLine;

// Decode OAM entry i for the current scanline. window_pass selects GFX mode 2
// (OBJ window) sprites; otherwise only visible sprites at `priority`. False
// when the entry is disabled, filtered out, has a reserved shape, misses the
// line or is hidden in a bitmap mode.
static bool obj_decode_line(const PPU* ppu, int32_t i, bool window_pass, int priority, ObjLine* o) {
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
    if (obj_mode == 2) return false;  // Disabled sprite

    // GFX Mode: attr0 bits 11-10
    // 0 = Normal, 1 = Semi-transparent, 2 = OBJ window
    // OBJ window sprites are not drawn and ignore priority; they only mark
    // pixels in the obj_window buffer (ppu_build_obj_window).
    uint8_t gfx_mode = BITS(attr0, 11, 10);
    if (window_pass != (gfx_mode == 2)) return false;

    // Priority: attr2 bits 11-10
    if (!window_pass && BITS(attr2, 11, 10) != (uint8_t)priority) return false;

    // Shape and size determine sprite dimensions
    uint8_t shape = BITS(attr0, 15, 14);
    uint8_t size  = BITS(attr1, 15, 14);

    // Guard against invalid shape (shape=3 is reserved/undefined)
    if (shape > 2) return false;

    o->width  = sprite_width[shape][size];
    o->height = sprite_height[shape][size];

    // Affine double-size mode renders into a doubled bounding box.
    o->bbox_w = (obj_mode == 3) ? o->width * 2 : o->width;
    o->bbox_h = (obj_mode == 3) ? o->height * 2 : o->height;

    // Y coordinate: attr0 bits 7-0 (unsigned 0-255)
    // Values >= 160 are treated as negative (wrap around): y - 256
    int32_t sprite_y = BITS(attr0, 7, 0);
    if (sprite_y >= 160) sprite_y -= 256;

    // Check if the sprite's bounding box intersects the scanline
    o->local_y = (int32_t)ppu->vcount - sprite_y;
    if (o->local_y < 0 || o->local_y >= o->bbox_h) return false;

    // Apply vertical mosaic: snap sprite row to the top of the mosaic block.
    // OAM attr0 bit 12 enables mosaic for this sprite. Not applied to OBJ
    // window sprites (kept as before; unverified against hardware).
    if (!window_pass && BIT(attr0, 12)) {
        uint16_t obj_v_size = BITS(ppu->mosaic, 15, 12) + 1;
        if (obj_v_size > 1) {
            o->local_y = o->local_y - (o->local_y % (int32_t)obj_v_size);
        }
    }

    // X coordinate: attr1 bits 8-0 (9-bit signed via sign-extending bit 8)
    o->x = BITS(attr1, 8, 0);
    if (BIT(attr1, 8)) {
        o->x |= (int32_t)0xFFFFFE00;  // Sign-extend from bit 8
    }

    // Color mode: attr0 bit 13 (0=4bpp, 1=8bpp)
    o->color_8bpp = BIT(attr0, 13);

    // Tile number: attr2 bits 9-0
    o->base_tile = BITS(attr2, 9, 0);

    // In 8bpp mode, hardware forces tile number bit 0 to 0
    if (o->color_8bpp) {
        o->base_tile &= ~(uint16_t)1;
    }

    if (obj_tile_hidden_in_bitmap_mode(ppu, o->base_tile)) return false;

    // Palette number: attr2 bits 15-12 (only used in 4bpp mode)
    o->pal_num = BITS(attr2, 15, 12);

    // Flip flags (regular mode only; affine repurposes these bits
    // as part of the parameter group index)
    o->affine = (obj_mode == 1) || (obj_mode == 3);
    o->h_flip = !o->affine && BIT(attr1, 12);
    o->v_flip = !o->affine && BIT(attr1, 13);

    o->pa = 0x100;
    o->pb = 0;
    o->pc = 0;
    o->pd = 0x100;
    if (o->affine) {
        obj_read_affine_params(ppu, attr1, &o->pa, &o->pb, &o->pc, &o->pd);
    }

    o->attr0 = attr0;
    o->gfx_mode = gfx_mode;
    return true;
}

// Palette index at bounding-box column px of a decoded sprite line
// (0: transparent, or outside an affine sprite's texture).
static uint8_t obj_line_texel(const PPU* ppu, const ObjLine* o, bool mapping_1d, int32_t px) {
    int32_t tex_x, tex_y;
    if (!obj_texcoord(px, o->local_y, o->affine, o->pa, o->pb, o->pc, o->pd,
                      o->bbox_w, o->bbox_h, o->width, o->height,
                      o->h_flip, o->v_flip, &tex_x, &tex_y)) {
        return 0;
    }
    return obj_fetch_texel(ppu, mapping_1d, o->color_8bpp, o->base_tile, (uint32_t)o->width,
                           (uint32_t)tex_x, (uint32_t)tex_y);
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

    bool extra = false;
    for (int k = 0; k < PPU_EXTRA_OBJ_MAX; k++)
        if (ppu->extra_obj[k].on && ppu->extra_obj[k].priority == priority) extra = true;

    if (extra) extra_objs_at(ppu, priority, 128);
    for (int32_t i = OAM_ENTRY_COUNT - 1; i >= 0; i--) {
        if (extra) extra_objs_at(ppu, priority, i);
        ObjLine o;
        if (!obj_decode_line(ppu, i, false, priority, &o)) continue;

        // Render each pixel of this bounding-box row
        for (int32_t px = 0; px < o.bbox_w; px++) {
            int32_t screen_x = o.x + px;

            // Clip to visible screen area
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            // Palette index 0 is transparent in both modes
            uint8_t color_idx = obj_line_texel(ppu, &o, mapping_1d, px);
            if (color_idx == 0) continue;

            // Look up the 15-bit BGR555 color from OBJ palette RAM
            uint32_t pal_addr;
            if (o.color_8bpp) {
                pal_addr = OBJ_PALETTE_BASE + (uint32_t)color_idx * 2;
            } else {
                pal_addr = OBJ_PALETTE_BASE
                         + ((uint32_t)o.pal_num * 16 + (uint32_t)color_idx) * 2;
            }

            uint16_t color = (uint16_t)ppu->palette_ram[pal_addr]
                           | ((uint16_t)ppu->palette_ram[pal_addr + 1] << 8);

            // Window-filtered write: dropped if OBJ is masked out at this pixel.
            // Only set mosaic/semi-transparent flags when the write landed.
            if (ppu_push_pixel(ppu, (uint32_t)screen_x, color, 4)) {
                ppu->obj_mosaic[screen_x] = BIT(o.attr0, 12);
                ppu->obj_semitransparent[screen_x] = (o.gfx_mode == 1);
            }
        }
    }

    if (extra) extra_objs_at(ppu, priority, -1);
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

    for (int32_t i = OAM_ENTRY_COUNT - 1; i >= 0; i--) {
        ObjLine o;
        if (!obj_decode_line(ppu, i, true, 0, &o)) continue;

        for (int32_t px = 0; px < o.bbox_w; px++) {
            int32_t screen_x = o.x + px;
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            // Only non-transparent pixels contribute to the OBJ window
            if (obj_line_texel(ppu, &o, mapping_1d, px) != 0) {
                ppu->obj_window[screen_x] = true;
            }
        }
    }
}
