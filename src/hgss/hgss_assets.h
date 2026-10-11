#ifndef HGSS_ASSETS_H
#define HGSS_ASSETS_H

#include "hgss/hgss_gfx.h"
#include "hgss/hgss_msg.h"
#include "hgss/hgss_talk.h"
#include "hgss/nds_fs.h"

#define HGSS_EMOTES       14
#define HGSS_MSG_BANK_MAX (96 * 1024)
#define HGSS_TEXT_W       200
#define HGSS_TEXT_LINES   2
#define HGSS_PAGE_MAX     8

typedef struct {
    bool ok;
    NdsRom rom;
    NdsSpan mmodel, tp_param, msg, rules, scripts, anims, species_class, frames, fonts, emotes;
    uint8_t global_rules[70][HGSS_RULE_SIZE];
    uint8_t species_class_tbl[496];
    uint8_t msg_bank[HGSS_MSG_BANK_MAX];
    uint32_t msg_len;
    HgssFrameGfx frame;
    HgssFont font;
    HgssEmoteGfx emote[HGSS_EMOTES];
    HgssBallGfx ball;
    HgssBurstGfx burst;
    bool fx_ok;                   /* ball/burst decoded; the effect is optional */
    char error[96];               /* why ok is false, for the toast/log */
} HgssAssets;

/* Opens the ROM, checks IPKE (IPGE only if every path resolves), loads all
 * small tables, the frame, font, emotes and message bank. */
bool hgss_assets_open(HgssAssets* a, const char* path);
void hgss_assets_close(HgssAssets* a);
/* Follower sprite for a national dex species. form: Unown letter 0..27 else 0. */
bool hgss_assets_follower(HgssAssets* a, uint16_t national, bool female, uint8_t form, HgssFollowerGfx* out,
                          bool* too_tall);
/* rules[0..29] = local table for mapsec (zeros when mapsec == 0 or missing); rules[30..99] = global. */
void hgss_assets_rules(HgssAssets* a, uint16_t mapsec, uint8_t rules[HGSS_RULES][HGSS_RULE_SIZE]);
bool hgss_assets_script(HgssAssets* a, uint16_t id, HgssTalkScript* out);
uint8_t hgss_assets_anim(HgssAssets* a, uint16_t id, HgssAnimFrame out[HGSS_ANIM_FRAMES]);
bool hgss_assets_line(const HgssAssets* a, uint16_t msg1, uint16_t* out, uint32_t cap, uint32_t* n);

/* Text layout: pages of up to 2 lines of glyph codes, wrapped at HGSS_TEXT_W px. */
typedef struct {
    uint16_t line[HGSS_TEXT_LINES][64];
    uint8_t len[HGSS_TEXT_LINES];
} HgssTextPage;
uint8_t hgss_text_layout(const HgssFont* f, const uint16_t* text, uint32_t n, HgssTextPage* pages, uint8_t max);
/* Draws the box (frame + interior) and `chars` characters of page p onto a
 * 240x160 BGR555 framebuffer. arrow: draw the continue arrow. */
void hgss_text_draw(uint16_t* fb, const HgssFrameGfx* fr, const HgssFont* f, const HgssTextPage* p,
                    uint32_t chars, bool arrow);

#endif // HGSS_ASSETS_H
