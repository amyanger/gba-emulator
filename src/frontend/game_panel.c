#include "frontend/game_panel.h"
#include "common.h"
#include "ui/ui_font.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <stdio.h>
#include <string.h>

/* Panel layout in points (600 x 480), from the approved mockups. */
#define PANEL_W     600.0f
#define PANEL_H     480.0f
#define LEFT        16.0f
#define RIGHT       584.0f
#define COL2_X      308.0f
#define COL_W       276.0f
#define KV_SIZE     12.5f
#define KV_LINE     18.0f
#define SECTION_ADV 18.0f
#define CONTENT_BOTTOM 476.0f   /* bottom of the page clip */
#define ROUTE_LIST_Y   78.0f    /* below the tabs (end at 42) and the map title */
#define ROUTE_LIST_END 452.0f   /* above the footer */

#define TIMES "\xC3\x97"      /* × */
#define MINUS "\xE2\x88\x92"  /* − */
#define NDASH "\xE2\x80\x93"  /* – */
#define MDOT  " \xC2\xB7 "    /* " · " */

GamePage game_panel_next_page(GamePage page) {
    return (GamePage)((page + 1) % GAME_PAGE_COUNT);
}

GamePage game_panel_resolve_page(GamePage page, GameContext ctx) {
    if (page != GAME_PAGE_AUTO) return page;
    if (ctx == GAME_CTX_BATTLE_WILD || ctx == GAME_CTX_BATTLE_TRAINER) return GAME_PAGE_COUNT;
    return GAME_PAGE_ENCOUNTERS;
}

GamePage game_panel_follow_battle(GamePage page, GameContext ctx, PanelBattleFollow* f) {
    if (ctx == GAME_CTX_NONE) return page;
    bool battle = ctx == GAME_CTX_BATTLE_WILD || ctx == GAME_CTX_BATTLE_TRAINER;
    if (battle == f->in_battle) return page;
    f->in_battle = battle;
    if (battle) {
        f->saved_page = (uint8_t)page;
        f->restore = page != GAME_PAGE_AUTO;
        return GAME_PAGE_AUTO;
    }
    if (!f->restore) return page;
    f->restore = false;
    return (GamePage)f->saved_page;
}

uint8_t game_panel_select(uint8_t sel, int delta, uint8_t party_count) {
    if (party_count == 0) return 0;
    int s = sel >= party_count ? party_count - 1 : sel;
    s = ((s + delta) % party_count + party_count) % party_count;
    return (uint8_t)s;
}

static bool title_sep(char ch) { return ch == ' ' || ch == '-' || ch == '.'; }

/* Restores Gen 3 spellings that plain title case loses: the whole words "Poke"
 * (the ROM's "POKe", decoded as "POKE") -> "Poké", and "Hp"/"Pp" -> "HP"/"PP".
 * Words that only contain them, like "Pokemon", are left alone. */
static void title_fix_tokens(char* out, size_t out_size) {
    size_t len = strlen(out);
    size_t i = 0;
    while (i < len) {
        size_t j = i;
        while (j < len && !title_sep(out[j])) j++;
        if (j - i == 2 && (memcmp(out + i, "Hp", 2) == 0 || memcmp(out + i, "Pp", 2) == 0)) {
            out[i + 1] = 'P';
        } else if (j - i == 4 && memcmp(out + i, "Poke", 4) == 0 && len + 2 <= out_size) {
            /* "e" becomes the two-byte UTF-8 "\xC3\xA9"; skip it if it would not fit. */
            memmove(out + j + 1, out + j, len - j + 1);
            out[i + 3] = (char)0xC3;
            out[i + 4] = (char)0xA9;
            len++;
            j++;
        }
        i = j + 1;
    }
}

void game_panel_title_case(const char* in, char* out, size_t out_size) {
    if (out_size == 0) return;
    size_t n = 0;
    bool word_start = true;
    for (; in[n] && n + 1 < out_size; n++) {
        char ch = in[n];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + ('a' - 'A'));
        if (word_start && ch >= 'a' && ch <= 'z') ch = (char)(ch - ('a' - 'A'));
        word_start = title_sep(ch);
        out[n] = ch;
    }
    /* Never end on a partial UTF-8 sequence when truncating. */
    if (in[n])
        while (n > 0 && ((unsigned char)in[n] & 0xC0) == 0x80) n--;
    out[n] = '\0';
    title_fix_tokens(out, out_size);
}

/* Gen 3 type ids; the pills use these instead of the ROM's 6-letter names. */
static const char* const k_type_names[18] = {
    "NORMAL", "FIGHTING", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST", "STEEL",
    "???", "FIRE", "WATER", "GRASS", "ELECTRIC", "PSYCHIC", "ICE", "DRAGON", "DARK",
};
/* Nature effect index order {Atk, Def, Spe, SpA, SpD}. */
static const char* const k_nature_stat[5] = {"Atk", "Def", "Spe", "SpA", "SpD"};
/* Snapshot stat order is HP, Atk, Def, Spe, SpA, SpD; rows show Spe last. */
static const uint8_t k_stat_order[6] = {0, 1, 2, 4, 5, 3};
static const char* const k_stat_label[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

typedef struct { char s[GAME_NAME_LEN]; } Name;

static Name title(const char* in) {
    Name n;
    game_panel_title_case(in, n.s, sizeof(n.s));
    return n;
}

static uint32_t hp_color(uint16_t hp, uint16_t max_hp) {
    if (max_hp == 0 || hp * 5u < max_hp) return UI_BAD;
    if (hp * 2u < max_hp) return UI_MID;
    return UI_GOOD;
}

static float hp_frac(uint16_t hp, uint16_t max_hp) {
    return max_hp ? (float)hp / (float)max_hp : 0.0f;
}

static const char* status_text(uint32_t s) {
    if (s & 0x07) return "Asleep";
    if (s & 0x80) return "Badly poisoned";
    if (s & 0x08) return "Poisoned";
    if (s & 0x10) return "Burned";
    if (s & 0x20) return "Frozen";
    if (s & 0x40) return "Paralyzed";
    return NULL;
}

static void section(UiCanvas* c, float x, float y, float w, const char* upper) {
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_LABEL, x, y, w, UI_ALIGN_LEFT, UI_DIM, upper);
}

/* Muted label plus value; returns the value's drawn width. */
static float kv_row(UiCanvas* c, float x, float y, float label_w, float w, const char* label,
                    const char* value, uint32_t color) {
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, x, y, label_w - 6, UI_ALIGN_LEFT, UI_MUTED, label);
    return ui_text(c, UI_FONT_REGULAR, KV_SIZE, x + label_w, y, w - label_w, UI_ALIGN_LEFT, color,
                   value);
}

static void item_row(UiCanvas* c, float x, float y, float label_w, float w, const char* item) {
    if (strcmp(item, "--") == 0 || item[0] == '\0')
        kv_row(c, x, y, label_w, w, "Item", "None", UI_MUTED);
    else {
        Name n = title(item);
        kv_row(c, x, y, label_w, w, "Item", n.s, UI_TEXT);
    }
}

static void nature_row(UiCanvas* c, float x, float y, float label_w, float w, const char* nature,
                       int8_t up, int8_t down) {
    Name n = title(nature);
    float vw = kv_row(c, x, y, label_w, w, "Nature", n.s, UI_TEXT);
    if (up < 0 || down < 0 || up > 4 || down > 4) return;
    char buf[32];
    snprintf(buf, sizeof(buf), " +%s " MINUS "%s", k_nature_stat[up], k_nature_stat[down]);
    float vx = x + label_w + vw;
    if (x + w - vx <= 0) return; /* ui_text treats max_w <= 0 as unlimited */
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, vx, y, x + w - vx, UI_ALIGN_LEFT, UI_MUTED, buf);
}

/* Top of a bar of height h centered on the capitals of a text line at y. */
static float bar_y(UiCanvas* c, float y, float size, float h) {
    return y + ui_text_cap_middle(c, UI_FONT_REGULAR, size) - h / 2;
}

/* Bar plus a right-aligned number in a 26 point column at the right edge. */
static void stat_bar(UiCanvas* c, float x, float y, float w, uint8_t value, float max,
                     uint32_t fill) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", value);
    ui_bar(c, (UiRect){x, bar_y(c, y, UI_SIZE_SMALL, 6.0f), w - 34.0f, 6.0f}, value / max, fill);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x + w, y, 0, UI_ALIGN_RIGHT, UI_TEXT, buf);
}

static uint32_t iv_fill(uint8_t iv) { return iv >= 25 ? UI_GOOD : UI_ACCENT; }

static void iv_row(UiCanvas* c, float x, float y, float w, int row, const uint8_t ivs[6]) {
    uint8_t v = ivs[k_stat_order[row]];
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, 0, UI_ALIGN_LEFT, UI_MUTED, k_stat_label[row]);
    stat_bar(c, x + 42.0f, y, w - 42.0f, v, 31.0f, iv_fill(v));
}

static float pill_width(UiCanvas* c, uint8_t type) {
    if (type >= 18) return 0;
    return ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, k_type_names[type]) + 14.0f;
}

static float pill(UiCanvas* c, float x, float y, uint8_t type) {
    if (type >= 18) return 0;
    return ui_pill(c, x, y, k_type_names[type], ui_type_color(type));
}

/* One or two type pills starting at x (or ending at x when right aligned). */
static float type_pills(UiCanvas* c, float x, float y, const uint8_t ids[2], bool right) {
    uint8_t t1 = ids[0], t2 = ids[1] == ids[0] ? 0xFF : ids[1];
    float w1 = pill_width(c, t1), w2 = pill_width(c, t2);
    float total = w1 + w2 + (w1 > 0 && w2 > 0 ? 6.0f : 0.0f);
    float px = right ? x - total : x;
    px += pill(c, px, y, t1);
    if (w1 > 0) px += 6.0f;
    pill(c, px, y, t2);
    return total;
}

/* Muted "prefix · ♀ · suffix" sharing the baseline of an 18 point title at y. */
static void title_meta(UiCanvas* c, float x, float y, float max_x, const char* prefix,
                       MonGender gender, const char* suffix) {
    float base = y + ui_text_ascent(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE);
    float ty = base - ui_text_ascent(c, UI_FONT_REGULAR, UI_SIZE_BODY);
    if (x >= max_x) return;
    x += ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, ty, max_x - x, UI_ALIGN_LEFT, UI_MUTED,
                 prefix);
    if (gender != MON_GENDER_NONE && x + 11.0f < max_x) {
        x += 1.0f;
        ui_gender(c, x, base - 10.0f, 11.0f, gender == MON_GENDER_FEMALE);
        x += 11.0f;
        if (x < max_x)
            x += ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, ty, max_x - x, UI_ALIGN_LEFT,
                         UI_MUTED, MDOT);
    }
    if (x < max_x)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, ty, max_x - x, UI_ALIGN_LEFT, UI_MUTED,
                suffix);
}

/* pill_x lines the type pills up in one column, as in the mockup's grid. The colours
 * say how good the matchup is for the player, so they flip for the enemy's moves
 * (against_you): x2 against you is a threat, a resisted or blocked move is good news. */
static void move_row(UiCanvas* c, float x, float y, float w, float pill_x, const GameMoveEff* e,
                     bool against_you) {
    const char* txt = "?";
    uint32_t color = UI_MUTED;
    UiFont font = UI_FONT_SEMIBOLD;
    uint32_t strong = against_you ? UI_BAD : UI_GOOD;
    if (e->status_move) {
        txt = "status";
        font = UI_FONT_REGULAR;
    } else {
        switch (e->quarters) {
        case 16: txt = TIMES "4"; color = strong; break;
        case 8: txt = TIMES "2"; color = strong; break;
        case 4: txt = TIMES "1"; font = UI_FONT_REGULAR; break;
        case 2: txt = TIMES "0.5"; color = against_you ? UI_GOOD : UI_WARN; break;
        case 1: txt = TIMES "0.25"; color = against_you ? UI_GOOD : UI_WARN; break;
        case 0: txt = TIMES "0"; color = against_you ? UI_GOOD : UI_BAD; break;
        default: break; /* 0xFF: matchup unknown */
        }
    }
    /* Text centered on the pill (UI_SIZE_PILL + 5 tall). */
    float ty = y + (UI_SIZE_PILL + 5.0f) / 2 - ui_text_cap_middle(c, UI_FONT_REGULAR, KV_SIZE);
    Name move = title(e->move);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, x, ty, 130.0f, UI_ALIGN_LEFT, UI_TEXT, move.s);
    pill(c, pill_x, y, e->type);
    ui_text(c, font, KV_SIZE, x + w, ty, 0, UI_ALIGN_RIGHT, color, txt);
}

static void catch_row(UiCanvas* c, float y, const GameCatchRow* r) {
    char buf[16];
    Name ball = title(r->ball);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, LEFT, y, 326.0f, UI_ALIGN_LEFT, UI_TEXT, ball.s);
    snprintf(buf, sizeof(buf), TIMES "%u", r->quantity);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, 352.0f, y, 44.0f, UI_ALIGN_LEFT, UI_MUTED, buf);
    if (r->permille < 0) {
        ui_text(c, UI_FONT_REGULAR, KV_SIZE, RIGHT, y, 0, UI_ALIGN_RIGHT, UI_MUTED, "n/a");
        return;
    }
    ui_bar(c, (UiRect){402.0f, bar_y(c, y, KV_SIZE, 6.0f), 120.0f, 6.0f}, r->permille / 1000.0f,
           UI_ACCENT);
    snprintf(buf, sizeof(buf), "%d.%d%%", r->permille / 10, r->permille % 10);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, RIGHT, y, 0, UI_ALIGN_RIGHT, UI_TEXT, buf);
}

/* Full width (568) uses the mockup's columns; a half-width column (276) packs them tighter. */
static void enc_row(UiCanvas* c, float x, float y, float w, const GameEncRow* r) {
    char buf[24];
    bool wide = w > 400.0f;
    float name_w = wide ? 302.0f : 92.0f, lv_x = x + (wide ? 332.0f : 118.0f);
    float bar_x = x + (wide ? 410.0f : 186.0f), bar_w = wide ? 110.0f : 44.0f;
    Name species = title(r->species);
    if (r->caught) ui_check(c, x, y, 12.0f, UI_GOOD);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, x + 22.0f, y, name_w, UI_ALIGN_LEFT, UI_TEXT,
            species.s);
    if (r->min_level == r->max_level) snprintf(buf, sizeof(buf), "Lv %u", r->min_level);
    else snprintf(buf, sizeof(buf), "Lv %u" NDASH "%u", r->min_level, r->max_level);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, lv_x, y, bar_x - lv_x - 4.0f, UI_ALIGN_LEFT, UI_MUTED,
            buf);
    ui_bar(c, (UiRect){bar_x, bar_y(c, y, KV_SIZE, 6.0f), bar_w, 6.0f}, r->percent / 100.0f,
           UI_ACCENT);
    snprintf(buf, sizeof(buf), "%u%%", r->percent);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, x + w, y, 0, UI_ALIGN_RIGHT, UI_TEXT, buf);
}

static void more_line(UiCanvas* c, float x, float y, unsigned n) {
    char buf[16];
    snprintf(buf, sizeof(buf), "+%u more", n);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, x, y, 0, UI_ALIGN_LEFT, UI_MUTED, buf);
}

/* ASCII uppercase copy for section titles, truncated to fit. */
static void upper_copy(char* out, size_t out_size, const char* in) {
    size_t n = 0;
    for (; in[n] && n + 1 < out_size; n++) {
        char ch = in[n];
        out[n] = (ch >= 'a' && ch <= 'z') ? (char)(ch - ('a' - 'A')) : ch;
    }
    out[n] = '\0';
}

static void draw_battle(UiCanvas* c, const GameSnapshot* s, float top) {
    if (s->enemy_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, PANEL_W / 2, 220.0f, 568.0f, UI_ALIGN_CENTER,
                UI_MUTED, "Waiting for battle data");
        return;
    }
    const GameEnemy* e = &s->enemies[0];
    bool wild = s->context == GAME_CTX_BATTLE_WILD;
    char buf[64];
    float y = top + 4.0f;

    /* Title row */
    float pills_w = type_pills(c, RIGHT, y + 3.0f, e->type_ids, true);
    Name species = title(e->species);
    float name_w = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, LEFT, y, 220.0f, UI_ALIGN_LEFT,
                           UI_TEXT, species.s);
    snprintf(buf, sizeof(buf), "Lv %u", e->level);
    title_meta(c, LEFT + name_w + 8.0f, y, RIGHT - pills_w - 8.0f,
               wild ? "Wild" MDOT : "Trainer" MDOT, e->gender, buf);
    y += 28.0f;

    if (s->enemy_count > 1) {
        const GameEnemy* e2 = &s->enemies[1];
        Name species2 = title(e2->species);
        snprintf(buf, sizeof(buf), "Also: %s Lv %u, HP %u/%u", species2.s, e2->level,
                 e2->hp, e2->max_hp);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, LEFT, y, RIGHT - LEFT, UI_ALIGN_LEFT, UI_MUTED,
                buf);
        y += 18.0f;
    }

    /* HP row */
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, LEFT, y, 0, UI_ALIGN_LEFT, UI_MUTED, "HP");
    ui_bar(c, (UiRect){44.0f, bar_y(c, y, KV_SIZE, 6.0f), 456.0f, 6.0f}, hp_frac(e->hp, e->max_hp),
           hp_color(e->hp, e->max_hp));
    snprintf(buf, sizeof(buf), "%u / %u", e->hp, e->max_hp);
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, RIGHT, y, 76.0f, UI_ALIGN_RIGHT, UI_TEXT, buf);
    y += 26.0f;

    /* Left column: details and the player's moves */
    float ly = y;
    nature_row(c, LEFT, ly, 84.0f, COL_W, e->nature, e->nature_up, e->nature_down);
    ly += KV_LINE;
    Name ability = title(e->ability);
    kv_row(c, LEFT, ly, 84.0f, COL_W, "Ability", ability.s, UI_TEXT);
    ly += KV_LINE;
    item_row(c, LEFT, ly, 84.0f, COL_W, e->item);
    ly += KV_LINE;
    const char* st = status_text(e->status1);
    if (st) {
        kv_row(c, LEFT, ly, 84.0f, COL_W, "Status", st, UI_WARN);
        ly += KV_LINE;
    }
    /* Both move lists share one pill offset so their type columns line up. Matchup
     * column of 44 plus a 10 gap; the name keeps at least 130. */
    uint8_t eff_n = e->eff_count > 4 ? 4 : e->eff_count;
    uint8_t moves_n = e->move_count > 4 ? 4 : e->move_count;
    float widest = 0;
    for (uint8_t i = 0; i < eff_n; i++) {
        float pw = pill_width(c, e->eff[i].type);
        if (pw > widest) widest = pw;
    }
    for (uint8_t i = 0; i < moves_n; i++) {
        float pw = pill_width(c, e->moves[i].type);
        if (pw > widest) widest = pw;
    }
    float pill_off = COL_W - 54.0f - widest;
    if (pill_off < 138.0f) pill_off = 138.0f;
    char upper[GAME_NAME_LEN];

    if (eff_n > 0) {
        ly += 6.0f;
        if (s->active_name[0]) {
            upper_copy(upper, sizeof(upper), s->active_name);
            snprintf(buf, sizeof(buf), "YOUR %s'S MOVES", upper);
            section(c, LEFT, ly, COL_W, buf);
        } else {
            section(c, LEFT, ly, COL_W, "YOUR MOVES");
        }
        ly += SECTION_ADV;
        for (uint8_t i = 0; i < eff_n; i++) {
            move_row(c, LEFT, ly, COL_W, LEFT + pill_off, &e->eff[i], false);
            ly += 21.0f;
        }
    }

    /* Right column: IVs, then the enemy's moves against the player's active mon */
    float ry = y;
    section(c, COL2_X, ry, COL_W, "IVS");
    ry += SECTION_ADV;
    for (int i = 0; i < 6; i++) {
        iv_row(c, COL2_X, ry, COL_W, i, e->ivs);
        ry += 19.0f;
    }
    if (moves_n > 0) {
        ry += 6.0f;
        upper_copy(upper, sizeof(upper), e->species);
        snprintf(buf, sizeof(buf), "%s'S MOVES", upper);
        section(c, COL2_X, ry, COL_W, buf);
        ry += SECTION_ADV;
        for (uint8_t i = 0; i < moves_n; i++) {
            move_row(c, COL2_X, ry, COL_W, COL2_X + pill_off, &e->moves[i], true);
            ry += 21.0f;
        }
    }

    if (!wild) return;
    y = (ly > ry ? ly : ry) + 8.0f;
    section(c, LEFT, y, RIGHT - LEFT, "CATCH CHANCE");
    y += SECTION_ADV;
    if (s->catch_count == 0)
        ui_text(c, UI_FONT_REGULAR, KV_SIZE, LEFT, y, 0, UI_ALIGN_LEFT, UI_MUTED,
                "No balls in bag");
    /* Whole rows only (each 16 tall at a 19 pitch); the rest become "+N more". */
    int count = s->catch_count > 16 ? 16 : s->catch_count;
    int fit = y + 16.0f <= CONTENT_BOTTOM ? (int)((CONTENT_BOTTOM - y - 16.0f) / 19.0f) + 1 : 0;
    int rows = count > fit ? fit - 1 : count;
    for (int i = 0; i < rows; i++) {
        catch_row(c, y, &s->catch_rows[i]);
        y += 19.0f;
    }
    if (rows >= 0 && rows < count) more_line(c, LEFT, y, (unsigned)(count - rows));
}

static void party_list_row(UiCanvas* c, float y, const GamePartyMon* m, bool selected) {
    if (selected) {
        ui_fill_round_rect(c, (UiRect){LEFT, y, 190.0f, 40.0f}, UI_RADIUS, UI_SELECTED);
        ui_fill_round_rect(c, (UiRect){LEFT, y + 2.0f, 2.0f, 36.0f}, 1.0f, UI_ACCENT);
    }
    float tx = LEFT + 8.0f, ty = y + 8.0f, rx = LEFT + 182.0f;
    /* Rows without an HP bar center their text in the 40 point row. */
    float mid = y + 20.0f - ui_text_cap_middle(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY);
    if (m->bad) {
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY, tx, mid, 120.0f, UI_ALIGN_LEFT, UI_BAD,
                "Bad data");
        return;
    }
    if (m->egg) {
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY, tx, mid, 120.0f, UI_ALIGN_LEFT, UI_MUTED, "Egg");
        return;
    }
    char buf[16];
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY, tx, ty, 120.0f, UI_ALIGN_LEFT, UI_TEXT, m->nickname);
    snprintf(buf, sizeof(buf), "Lv %u", m->level);
    float lvy = ty + ui_text_ascent(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY) -
                ui_text_ascent(c, UI_FONT_REGULAR, 11.5f);
    ui_text(c, UI_FONT_REGULAR, 11.5f, rx, lvy, 50.0f, UI_ALIGN_RIGHT, UI_MUTED, buf);
    ui_bar(c, (UiRect){tx, y + 28.0f, 174.0f, 4.0f}, hp_frac(m->hp, m->max_hp),
           hp_color(m->hp, m->max_hp));
}

static void party_detail(UiCanvas* c, const GamePartyMon* m, float top) {
    const float x = 220.0f, w = RIGHT - x;
    char buf[48];
    float y = top + 4.0f;
    if (m->bad) {
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, w, UI_ALIGN_LEFT, UI_BAD, "Bad data");
        return;
    }
    if (m->egg) {
        ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, w, UI_ALIGN_LEFT, UI_TEXT, "Egg");
        y += 30.0f;
        nature_row(c, x, y, 96.0f, w, m->nature, m->nature_up, m->nature_down);
        y += KV_LINE + 8.0f;
        section(c, x + 42.0f, y, 60.0f, "IV");
        y += SECTION_ADV;
        for (int i = 0; i < 6; i++) {
            uint8_t v = m->ivs[k_stat_order[i]];
            ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, 0, UI_ALIGN_LEFT, UI_MUTED,
                    k_stat_label[i]);
            stat_bar(c, x + 42.0f, y, 157.0f, v, 31.0f, iv_fill(v));
            y += 19.0f;
        }
        return;
    }

    float name_w = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, 170.0f, UI_ALIGN_LEFT, UI_TEXT,
                           m->nickname);
    Name species = title(m->species);
    snprintf(buf, sizeof(buf), "%s" MDOT, species.s);
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv %u", m->level);
    title_meta(c, x + name_w + 8.0f, y, RIGHT, buf, m->gender, lv);
    y += 28.0f;
    if (pill_width(c, m->type_ids[0]) > 0 || pill_width(c, m->type_ids[1]) > 0) {
        type_pills(c, x, y, m->type_ids, false);
        y += 24.0f;
    }

    nature_row(c, x, y, 96.0f, w, m->nature, m->nature_up, m->nature_down);
    y += KV_LINE;
    Name ability = title(m->ability);
    kv_row(c, x, y, 96.0f, w, "Ability", ability.s, UI_TEXT);
    y += KV_LINE;
    item_row(c, x, y, 96.0f, w, m->item);
    y += KV_LINE;
    /* The ROM's type names are abbreviated ("ELECTR"), so name the type from the IVs. */
    uint8_t hp_type = pokemon_hidden_power_type(m->ivs);
    Name hp_name = title(hp_type < 18 ? k_type_names[hp_type] : "?");
    snprintf(buf, sizeof(buf), "%s" MDOT "%u", hp_name.s, m->hp_power);
    kv_row(c, x, y, 96.0f, w, "Hidden Power", buf, UI_TEXT);
    y += KV_LINE;
    snprintf(buf, sizeof(buf), "%u", m->friendship);
    kv_row(c, x, y, 96.0f, w, "Friendship", buf, UI_TEXT);
    y += KV_LINE;
    if (m->level >= 100) snprintf(buf, sizeof(buf), "Max");
    else snprintf(buf, sizeof(buf), "%lu exp", (unsigned long)m->exp_to_next);
    kv_row(c, x, y, 96.0f, w, "Next level", buf, UI_TEXT);
    y += KV_LINE + 8.0f;

    /* IV / EV grid: label 34, then two (bar + value) columns of 157 with an 8 gap */
    section(c, x + 42.0f, y, 60.0f, "IV");
    section(c, x + 207.0f, y, 60.0f, "EV");
    y += SECTION_ADV;
    for (int i = 0; i < 6; i++) {
        uint8_t iv = m->ivs[k_stat_order[i]], ev = m->evs[k_stat_order[i]];
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, x, y, 0, UI_ALIGN_LEFT, UI_MUTED,
                k_stat_label[i]);
        stat_bar(c, x + 42.0f, y, 157.0f, iv, 31.0f, iv_fill(iv));
        stat_bar(c, x + 207.0f, y, 157.0f, ev, 255.0f, UI_ACCENT);
        y += 19.0f;
    }
}

static void draw_party(UiCanvas* c, const GameSnapshot* s, float top, uint8_t sel) {
    uint8_t count = s->party_count > 6 ? 6 : s->party_count;
    if (count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, LEFT, top + 8.0f, 190.0f, UI_ALIGN_LEFT, UI_MUTED,
                "No Pok\xC3\xA9mon in party");
        return;
    }
    sel = game_panel_select(sel, 0, count);
    for (uint8_t i = 0; i < count; i++)
        party_list_row(c, top + 4.0f + i * 44.0f, &s->party[i], i == sel);
    party_detail(c, &s->party[sel], top);
}

static bool rod_method(int k) { return k >= ENC_OLD_ROD; }

static uint8_t enc_rows(const GameSnapshot* s, int k) {
    return s->enc_count[k] > 12 ? 12 : s->enc_count[k];
}

/* Column heights with every row shown at the layout's pitch. */
static void route_heights(const GameSnapshot* s, RouteLayout* l) {
    bool any[2] = {false, false};
    l->height[0] = l->height[1] = 0;
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        uint8_t n = enc_rows(s, k);
        if (n == 0) continue;
        int col = l->two_columns && rod_method(k);
        if (any[col]) l->height[col] += l->gap;
        l->height[col] += l->section_adv + n * l->row_h;
        any[col] = true;
    }
}

void game_panel_route_layout(const GameSnapshot* s, RouteLayout* out) {
    static const float pitch[2][3] = {{SECTION_ADV, 19.0f, 8.0f}, {16.0f, 16.0f, 4.0f}};
    memset(out, 0, sizeof(*out));
    out->avail = ROUTE_LIST_END - ROUTE_LIST_Y;
    if (!s->has_encounters) return;
    /* Roomy then tight in one column, then the same with rods in a second column. */
    for (int two = 0; two < 2; two++) {
        for (int p = 0; p < 2; p++) {
            out->section_adv = pitch[p][0];
            out->row_h = pitch[p][1];
            out->gap = pitch[p][2];
            out->two_columns = two != 0;
            route_heights(s, out);
            if (out->height[0] <= out->avail && out->height[1] <= out->avail) {
                for (int k = 0; k < ENC_METHOD_COUNT; k++) out->shown[k] = enc_rows(s, k);
                return;
            }
        }
    }
    /* Still too long (tight, two columns): keep whole rows and leave a row for "+N more". */
    for (int col = 0; col < 2; col++) {
        bool fits = out->height[col] <= out->avail, stop = false, any = false;
        float used = 0, limit = out->avail - out->row_h;
        for (int k = 0; k < ENC_METHOD_COUNT; k++) {
            uint8_t n = enc_rows(s, k);
            if (n == 0 || (int)rod_method(k) != col) continue;
            if (fits) { out->shown[k] = n; continue; }
            float y0 = used + (any ? out->gap : 0);
            float room = limit - y0 - out->section_adv;
            int rows = stop || room < out->row_h ? 0 : (int)(room / out->row_h);
            if (rows > n) rows = n;
            out->more[col] = (uint8_t)(out->more[col] + n - rows);
            if (rows < n) stop = true;
            if (rows == 0) continue;
            out->shown[k] = (uint8_t)rows;
            used = y0 + out->section_adv + rows * out->row_h;
            any = true;
        }
        if (!fits) out->height[col] = used + out->row_h;
    }
}

static void draw_route(UiCanvas* c, const GameSnapshot* s, float top) {
    char buf[32];
    float y = top + 4.0f;
    if (s->map_name[0])
        game_panel_title_case(s->map_name, buf, sizeof(buf));
    else
        snprintf(buf, sizeof(buf), "Map %u:%u", s->map_group, s->map_num);
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, LEFT, y, 0, UI_ALIGN_LEFT, UI_TEXT, buf);

    /* The layout keeps whole rows above the footer; the clip is only a backstop. */
    RouteLayout l;
    game_panel_route_layout(s, &l);
    ui_canvas_clip(c, (UiRect){UI_PAD, top, PANEL_W - 2 * UI_PAD, ROUTE_LIST_END - top});
    const float cx[2] = {LEFT, COL2_X}, cw = l.two_columns ? COL_W : RIGHT - LEFT;
    float cy[2] = {ROUTE_LIST_Y, ROUTE_LIST_Y};
    bool any[2] = {false, false};
    for (int k = 0; k < ENC_METHOD_COUNT; k++) {
        if (l.shown[k] == 0) continue;
        int col = l.two_columns && rod_method(k);
        if (any[col]) cy[col] += l.gap;
        any[col] = true;
        const char* name = game_method_name((EncMethod)k);
        size_t n = 0;
        for (; name[n] && n + 1 < sizeof(buf); n++)
            buf[n] = (name[n] >= 'a' && name[n] <= 'z') ? (char)(name[n] - ('a' - 'A')) : name[n];
        buf[n] = '\0';
        section(c, cx[col], cy[col], cw, buf);
        cy[col] += l.section_adv;
        for (uint8_t i = 0; i < l.shown[k]; i++) {
            enc_row(c, cx[col], cy[col], cw, &s->enc[k][i]);
            cy[col] += l.row_h;
        }
    }
    for (int col = 0; col < 2; col++)
        if (l.more[col]) more_line(c, cx[col] + 22.0f, cy[col], l.more[col]);
    if (!any[0] && !any[1] && l.more[0] + l.more[1] == 0)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, LEFT, ROUTE_LIST_Y, 0, UI_ALIGN_LEFT, UI_MUTED,
                "No wild Pok\xC3\xA9mon here");

    ui_canvas_clip(c, (UiRect){UI_PAD, top, PANEL_W - 2 * UI_PAD, PANEL_H - top - 4.0f});
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, 456.0f, RIGHT - LEFT, UI_ALIGN_LEFT, UI_FAINT,
            "\xE2\x9C\x93 caught. Not shown: outbreaks, Feebas tiles, roamers, Altering Cave, "
            "lead abilities, Pyramid/Pike.");
}

/* ---- Nuzlocke page ---- */

#define NUZ_ROW_H    20.0f
#define NUZ_LIST_END 450.0f
#define NUZ_FOOT_Y   456.0f

uint8_t game_panel_nuz_sub(uint8_t sub, int delta) {
    return (uint8_t)(((sub + delta) % NUZ_SUB_COUNT + NUZ_SUB_COUNT) % NUZ_SUB_COUNT);
}

uint8_t game_panel_nuz_route_start(const NuzView* v, uint8_t fit) {
    if (fit == 0 || v->route_count <= fit) return 0;
    int start = 0;
    /* Keep a couple of rows after the current location so the next routes show. */
    if (v->here_row >= 0) start = v->here_row - (int)fit + 3;
    if (start < 0) start = 0;
    if (start > v->route_count - fit) start = v->route_count - fit;
    return (uint8_t)start;
}

static void place_title(const char* in, char* out, size_t out_size) {
    game_panel_title_case(in, out, out_size);
}

static void nuz_stat(UiCanvas* c, float x, float y, float w, unsigned value, const char* label,
                     uint32_t color) {
    char buf[12];
    ui_fill_round_rect(c, (UiRect){x, y, w, 40.0f}, UI_RADIUS, UI_SURFACE);
    snprintf(buf, sizeof(buf), "%u", value);
    ui_text(c, UI_FONT_SEMIBOLD, 16.0f, x + 10.0f, y + 4.0f, w - 20.0f, UI_ALIGN_LEFT, color, buf);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, x + 10.0f, y + 24.0f, w - 20.0f, UI_ALIGN_LEFT,
            UI_MUTED, label);
}

static void nuz_route_row(UiCanvas* c, float y, const NuzViewRoute* r) {
    static const char* const labels[] = {"You are here", "In battle", "Caught", "Missed", "Dead",
                                         "Owned", "Gift"};
    static const uint32_t colors[] = {UI_ACCENT, UI_MID, UI_GOOD, UI_DIVIDER, UI_BAD, UI_DIVIDER,
                                      UI_ACCENT};
    char place[GAME_MAP_NAME_LEN + 8];
    Name who = title(r->who);
    place_title(r->place, place, sizeof(place));
    ui_text(c, r->here ? UI_FONT_SEMIBOLD : UI_FONT_REGULAR, KV_SIZE, LEFT, y, 200.0f, UI_ALIGN_LEFT,
            r->here ? UI_ACCENT : UI_MUTED, place);
    bool struck = r->pill == NUZ_PILL_MISSED || r->pill == NUZ_PILL_DEAD;
    if (r->who[0]) {
        float w = ui_text(c, UI_FONT_REGULAR, KV_SIZE, 228.0f, y, 170.0f, UI_ALIGN_LEFT,
                          struck ? UI_MUTED : UI_TEXT, who.s);
        if (struck) ui_hline(c, 228.0f, y + ui_text_cap_middle(c, UI_FONT_REGULAR, KV_SIZE), w, UI_MUTED);
    } else {
        ui_text(c, UI_FONT_REGULAR, KV_SIZE, 228.0f, y, 170.0f, UI_ALIGN_LEFT, UI_FAINT,
                "No encounter yet");
    }
    uint8_t pi = r->pill <= NUZ_PILL_GIFT ? r->pill : NUZ_PILL_MISSED;
    float pw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, labels[pi]) + 14.0f;
    float py = y + ui_text_cap_middle(c, UI_FONT_REGULAR, KV_SIZE) - (UI_SIZE_PILL + 5.0f) / 2;
    ui_pill(c, RIGHT - pw, py, labels[pi], colors[pi]);
    if (r->reroll) {
        float rw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, "Re-roll") + 14.0f;
        ui_pill(c, RIGHT - pw - 6.0f - rw, py, "Re-roll", UI_WARN);
    }
}

static void nuz_routes(UiCanvas* c, const NuzView* v, float y) {
    uint8_t fit = (uint8_t)((NUZ_LIST_END - y) / NUZ_ROW_H);
    if (v->route_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, LEFT, y, 0, UI_ALIGN_LEFT, UI_MUTED,
                "No routes yet. Walk into tall grass to find your first encounter.");
        return;
    }
    if (v->route_count > fit) fit--;  /* room for the "showing" line */
    uint8_t start = game_panel_nuz_route_start(v, fit);
    for (uint8_t i = start; i < v->route_count && i < start + fit; i++, y += NUZ_ROW_H)
        nuz_route_row(c, y, &v->routes[i]);
    if (v->route_count > fit) {
        char buf[48];
        snprintf(buf, sizeof(buf), "Showing %u" NDASH "%u of %u places", start + 1u,
                 (unsigned)(start + fit), (unsigned)v->route_count);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, y + 2.0f, 0, UI_ALIGN_LEFT, UI_FAINT, buf);
    }
}

static void nuz_team(UiCanvas* c, const NuzView* v, float y) {
    static const uint32_t strip[] = {UI_GOOD, UI_DIVIDER, UI_FAINT, UI_BAD};
    static const char* const where[] = {"", MDOT "PC", MDOT "Away", MDOT "Dead"};
    const float gap = 8.0f, w = (RIGHT - LEFT - 2 * gap) / 3, h = 40.0f;
    if (v->team_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, LEFT, y, 0, UI_ALIGN_LEFT, UI_MUTED,
                "No Pok\xC3\xA9mon in this run yet");
        return;
    }
    int rows_fit = (int)((NUZ_LIST_END - y + gap) / (h + gap));
    int shown = 0;
    for (uint8_t i = 0; i < v->team_count; i++) {
        int row = i / 3, col = i % 3;
        if (row >= rows_fit) break;
        if (row == rows_fit - 1 && col == 2 && v->team_total > (unsigned)(i + 1)) break;  /* "+N" */
        const NuzViewMon* m = &v->team[i];
        float x = LEFT + col * (w + gap), cy = y + row * (h + gap);
        bool dead = m->where == NUZ_WHERE_DEAD;
        ui_fill_round_rect(c, (UiRect){x, cy, w, h}, UI_RADIUS, UI_SURFACE);
        ui_fill_rect(c, (UiRect){x, cy + 4.0f, 3.0f, h - 8.0f}, strip[m->where & 3]);
        ui_text(c, UI_FONT_SEMIBOLD, KV_SIZE, x + 10.0f, cy + 5.0f, w - 16.0f, UI_ALIGN_LEFT,
                dead ? UI_MUTED : UI_TEXT, m->nick);
        char line[64];
        Name sp = title(m->species);
        if (m->level) snprintf(line, sizeof(line), "%s" MDOT "Lv %u%s", sp.s, m->level, where[m->where & 3]);
        else snprintf(line, sizeof(line), "%s%s", sp.s, where[m->where & 3]);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL - 1.0f, x + 10.0f, cy + 22.0f, w - 16.0f,
                UI_ALIGN_LEFT, UI_MUTED, line);
        shown++;
    }
    if (v->team_total > (unsigned)shown) {
        int row = shown / 3, col = shown % 3;
        char buf[16];
        snprintf(buf, sizeof(buf), "+%u more", (unsigned)(v->team_total - shown));
        ui_text(c, UI_FONT_REGULAR, KV_SIZE, LEFT + col * (w + gap) + 10.0f,
                y + row * (h + gap) + 12.0f, 0, UI_ALIGN_LEFT, UI_MUTED, buf);
    }
}

static void nuz_check_row(UiCanvas* c, float y, const char* label, const char* value, uint32_t bg) {
    ui_text(c, UI_FONT_REGULAR, KV_SIZE, LEFT, y, 400.0f, UI_ALIGN_LEFT, UI_TEXT, label);
    float pw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, value) + 14.0f;
    float py = y + ui_text_cap_middle(c, UI_FONT_REGULAR, KV_SIZE) - (UI_SIZE_PILL + 5.0f) / 2;
    ui_pill(c, RIGHT - pw, py, value, bg);
}

static void nuz_graveyard(UiCanvas* c, const NuzView* v, float y) {
    char buf[96];
    const float checks_h = 3 * NUZ_ROW_H + 30.0f;
    if (v->grave_count == 0) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, LEFT, y, 0, UI_ALIGN_LEFT, UI_MUTED,
                "No deaths yet.");
        y += 30.0f;
    }
    uint8_t shown = 0;
    for (uint8_t i = 0; i < v->grave_count; i++) {
        const NuzViewGrave* g = &v->graves[i];
        float h = 46.0f + (g->foe[0] ? 0.0f : -16.0f) + (g->reloaded_after ? 18.0f : 0.0f) + 16.0f;
        if (y + h > NUZ_LIST_END - checks_h) break;
        ui_fill_round_rect(c, (UiRect){LEFT, y, RIGHT - LEFT, h - 6.0f}, UI_RADIUS, UI_SURFACE);
        ui_fill_rect(c, (UiRect){LEFT, y + 4.0f, 3.0f, h - 14.0f}, UI_BAD);
        float tx = LEFT + 12.0f, ty = y + 6.0f;
        float nw = ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY, tx, ty, 200.0f, UI_ALIGN_LEFT, UI_TEXT,
                           g->nick);
        Name sp = title(g->species);
        snprintf(buf, sizeof(buf), "  %s" MDOT "Lv %u", sp.s, g->level);
        ui_text(c, UI_FONT_REGULAR, KV_SIZE, tx + nw, ty, 300.0f, UI_ALIGN_LEFT, UI_MUTED, buf);
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, RIGHT - 10.0f, ty + 2.0f, 0, UI_ALIGN_RIGHT,
                UI_FAINT, g->time);
        char a[GAME_MAP_NAME_LEN + 8], b[GAME_MAP_NAME_LEN + 8];
        place_title(g->caught_at[0] ? g->caught_at : "unknown", a, sizeof(a));
        place_title(g->died_at[0] ? g->died_at : "unknown", b, sizeof(b));
        snprintf(buf, sizeof(buf), "Met at %s" MDOT "died at %s", a, b);
        ty += 18.0f;
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, tx, ty, RIGHT - tx - 10.0f, UI_ALIGN_LEFT, UI_MUTED, buf);
        if (g->foe[0]) {
            Name foe = title(g->foe);
            snprintf(buf, sizeof(buf), "Fell to %s", foe.s);
            ty += 16.0f;
            ui_text(c, UI_FONT_REGULAR, UI_SIZE_SMALL, tx, ty, RIGHT - tx - 10.0f, UI_ALIGN_LEFT,
                    UI_MUTED, buf);
        }
        if (g->reloaded_after) {
            ty += 18.0f;
            ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_SMALL, tx, ty, RIGHT - tx - 10.0f, UI_ALIGN_LEFT, UI_WARN,
                    "Reloaded after this death. The death still counts.");
        }
        y += h;
        shown++;
    }
    if (v->grave_total > shown && shown > 0) {
        snprintf(buf, sizeof(buf), "+%u more", (unsigned)(v->grave_total - shown));
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, y - 2.0f, 0, UI_ALIGN_LEFT, UI_FAINT, buf);
        y += 14.0f;
    }

    y = NUZ_LIST_END - checks_h + 6.0f;
    section(c, LEFT, y, RIGHT - LEFT, "RULE CHECKS");
    y += 18.0f;
    snprintf(buf, sizeof(buf), "%u", (unsigned)v->deadused);
    nuz_check_row(c, y, "Dead Pok\xC3\xA9mon sent into battle", v->deadused ? buf : "None",
                  v->deadused ? UI_BAD : UI_GOOD);
    y += NUZ_ROW_H;
    if (v->rerolls) {
        char place[GAME_MAP_NAME_LEN + 8];
        place_title(v->reroll_place, place, sizeof(place));
        snprintf(buf, sizeof(buf), "%u" MDOT "%s", (unsigned)v->rerolls, place);
    }
    nuz_check_row(c, y, "Encounters re-rolled after a reload", v->rerolls ? buf : "None",
                  v->rerolls ? UI_WARN : UI_GOOD);
    y += NUZ_ROW_H;
    nuz_check_row(c, y, "Reloaded after a death", v->reloaded_after_death ? "Yes" : "No",
                  v->reloaded_after_death ? UI_WARN : UI_GOOD);
}

static void nuz_no_run(UiCanvas* c, const NuzView* v) {
    static const char* const lines[] = {
        "Track a Nuzlocke run for this save: the first encounter on",
        "each route, every death, and any reload after a death.",
        "",
        "Rules: dupes clause by evolution line, shiny clause.",
        "Gift Pok\xC3\xA9mon don't use up a route.",
        "Pok\xC3\xA9mon you already have join the run as they are.",
    };
    float x = PANEL_W / 2, w = RIGHT - LEFT, y = 140.0f;
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, w, UI_ALIGN_CENTER, UI_TEXT, "Nuzlocke");
    y += 34.0f;
    if (v->state == NUZ_VIEW_OFF) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, UI_MUTED, v->reason);
        return;
    }
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++, y += 20.0f)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, UI_MUTED, lines[i]);
    y += 16.0f;
    float pw = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY, "Press N to start a run") + 28.0f;
    ui_fill_round_rect(c, (UiRect){x - pw / 2, y - 6.0f, pw, 30.0f}, 15.0f, UI_ACCENT);
    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, 0xFFFFFFFFu,
            "Press N to start a run");
}

static void draw_nuzlocke(UiCanvas* c, const NuzView* v, float top, uint8_t sub) {
    static const char* const subs[NUZ_SUB_COUNT] = {"Routes", "Team", "Graveyard"};
    if (!v || v->state != NUZ_VIEW_RUN) {
        static const NuzView off = { .state = NUZ_VIEW_OFF, .reason = "Waiting for save data" };
        nuz_no_run(c, v ? v : &off);
        return;
    }
    float y = top + 6.0f, gap = 8.0f, sw = (RIGHT - LEFT - 3 * gap) / 4;
    nuz_stat(c, LEFT, y, sw, v->alive, "alive", UI_GOOD);
    nuz_stat(c, LEFT + (sw + gap), y, sw, v->dead, "dead", v->dead ? UI_BAD : UI_TEXT);
    nuz_stat(c, LEFT + 2 * (sw + gap), y, sw, v->routes_used, "routes used", UI_TEXT);
    nuz_stat(c, LEFT + 3 * (sw + gap), y, sw, v->reloads, "reloads", v->reloads ? UI_WARN : UI_TEXT);

    y += 52.0f;
    float x = LEFT;
    for (uint8_t i = 0; i < NUZ_SUB_COUNT; i++)
        x += ui_pill(c, x, y, subs[i], i == sub ? UI_ACCENT : UI_SURFACE) + 6.0f;
    y += 28.0f;

    ui_canvas_clip(c, (UiRect){UI_PAD, top, PANEL_W - 2 * UI_PAD, NUZ_LIST_END + 2.0f - top});
    if (sub == NUZ_SUB_TEAM) nuz_team(c, v, y);
    else if (sub == NUZ_SUB_GRAVEYARD) nuz_graveyard(c, v, y);
    else nuz_routes(c, v, y);
    ui_canvas_clip(c, (UiRect){UI_PAD, top, PANEL_W - 2 * UI_PAD, PANEL_H - top - 4.0f});

    char foot[96];
    if (v->write_failed) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, NUZ_FOOT_Y, RIGHT - LEFT, UI_ALIGN_LEFT, UI_WARN,
                "Can't write the run file: this run is only kept until you quit.");
        return;
    }
    snprintf(foot, sizeof(foot), "%sDupes clause" MDOT "shiny clause" MDOT "started at %s",
             v->wiped ? "Party wiped: run over." MDOT : "", v->start_time);
    ui_text(c, UI_FONT_REGULAR, UI_SIZE_LABEL, LEFT, NUZ_FOOT_Y, RIGHT - LEFT, UI_ALIGN_LEFT,
            v->wiped ? UI_BAD : UI_FAINT, foot);
}

/* The ROM itself is unsupported: say so plainly, since a hack would otherwise look broken. */
static void draw_unsupported(UiCanvas* c, GameSupport support) {
    static const char* const hack[] = {
        "This ROM identifies itself as Pok\xC3\xA9mon Emerald, but its contents",
        "don't match the original game. It is most likely a ROM hack.",
    };
    static const char* const other[] = {
        "This ROM isn't Pok\xC3\xA9mon Emerald (US/English).",
    };
    static const char* const scope[] = {
        "Emerald Companion only works with the original, unmodified",
        "Pok\xC3\xA9mon Emerald (US/English) ROM. That is the version it was",
        "built and tested with. The game itself still plays normally.",
    };
    bool is_hack = support == GAME_SUPPORT_MODIFIED;
    const char* const* lead = is_hack ? hack : other;
    int lead_n = is_hack ? 2 : 1;
    float x = PANEL_W / 2, w = RIGHT - LEFT, y = 170.0f, line = 20.0f;

    ui_text(c, UI_FONT_SEMIBOLD, UI_SIZE_TITLE, x, y, w, UI_ALIGN_CENTER, UI_TEXT,
            is_hack ? "ROM hack not supported" : "Game not supported");
    y += 36.0f;
    for (int i = 0; i < lead_n; i++, y += line)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, UI_MUTED, lead[i]);
    y += 12.0f;
    for (int i = 0; i < 3; i++, y += line)
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, x, y, w, UI_ALIGN_CENTER, UI_MUTED, scope[i]);
}

void game_panel_draw(UiCanvas* c, const GameSnapshot* snap, GamePage page, uint8_t party_sel,
                     const NuzView* nuz, uint8_t nuz_sub) {
    static const char* const labels[4] = {"Battle", "Party", "Route", "Nuzlocke"};
    GamePage shown = game_panel_resolve_page(page, snap->context);
    int active = shown == GAME_PAGE_PARTY ? 1 : shown == GAME_PAGE_ENCOUNTERS ? 2
               : shown == GAME_PAGE_NUZLOCKE ? 3 : 0;
    const char* hint = "F10 page";
    if (active == 1) hint = "F10 page   [ ] select";
    else if (active == 3) hint = nuz && nuz->state == NUZ_VIEW_NO_RUN ? "F10 page   N start" : "F10 page   [ ] view";

    ui_canvas_unclip(c);
    ui_canvas_clear(c, UI_BG);
    float top = ui_tabs(c, LEFT, 12.0f, RIGHT - LEFT, labels, 4, active,
                        page == GAME_PAGE_AUTO ? "Auto" : NULL, hint);

    ui_canvas_clip(c, (UiRect){UI_PAD, top, PANEL_W - 2 * UI_PAD, PANEL_H - top - 4.0f});
    if (!snap->valid && snap->support != GAME_SUPPORT_OK) {
        draw_unsupported(c, snap->support);
    } else if (active == 3) {
        draw_nuzlocke(c, nuz, top, nuz_sub);
    } else if (!snap->valid) {
        ui_text(c, UI_FONT_REGULAR, UI_SIZE_BODY, PANEL_W / 2, 220.0f, RIGHT - LEFT,
                UI_ALIGN_CENTER, UI_MUTED, snap->reason);
    } else if (active == 1) {
        draw_party(c, snap, top, party_sel);
    } else if (active == 2) {
        draw_route(c, snap, top);
    } else {
        draw_battle(c, snap, top);
    }
    ui_canvas_unclip(c);
}
