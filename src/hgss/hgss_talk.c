#include "hgss/hgss_talk.h"

#include <string.h>

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint16_t hgss_rule_result(const uint8_t* e) { return (uint16_t)(rd16(e + 0x0A) >> 6); }

bool hgss_mood_band(uint8_t band, int8_t m) {
    switch (band) {
    case 1: return m == 127;
    case 2: return m >= 100 && m <= 126;
    case 3: return m >= 50 && m <= 99;
    case 4: return m >= 30 && m <= 49;
    case 5: return m >= -29 && m <= 29;
    case 6: return m >= -49 && m <= -30;
    case 7: return m >= -126 && m <= -50;
    case 8: return m == -127;
    case 9: return m >= 0;
    case 10: return m <= -1;
    default: return false;
    }
}

bool hgss_friend_band(uint8_t band, uint8_t f) {
    switch (band) {
    case 1: return f == 255;
    case 2: return f >= 200 && f <= 254;
    case 3: return f >= 150 && f <= 199;
    case 4: return f >= 90 && f <= 149;
    case 5: return f >= 60 && f <= 89;
    case 6: return f >= 30 && f <= 59;
    case 7: return f >= 1 && f <= 29;
    case 8: return f == 0;
    case 9: return f >= 90;
    case 10: return f < 60;
    default: return false;
    }
}

static bool species_class_ok(uint8_t want, uint8_t have) {
    switch (want) {
    case 0xFA: return have <= 19;
    case 0xFB: return have <= 130;
    case 0xFC: return have >= 140 && have <= 149;
    case 0xFD: return have >= 160;
    case 0xFE: return have >= 220;
    default: return want == have;
    }
}

static bool rule_passes(const uint8_t* e, const HgssTalkCtx* x, HgssRand rnd, void* ud) {
    const uint8_t* c = x->c;
    if (rnd(ud) % 100u >= e[0x11]) return false;
    if (rd16(e + 0x12) != 0) return false;                                   /* HGSS event flag */
    uint8_t item = e[3] & 0x1F;
    if (item && !(item == 9 ? c[HC_HAS_ITEM] != 0 : item == c[HC_ITEM_CLASS])) return false;
    if (e[0] && e[0] != c[HC_HP]) return false;
    uint8_t st = (uint8_t)(e[2] >> 5);
    if (st && !(st == 7 ? (c[HC_STATUS] >= 2 && c[HC_STATUS] != 6 && c[HC_STATUS] != 7) : st == c[HC_STATUS]))
        return false;
    uint8_t npc = (uint8_t)(rd16(e + 0x0A) & 7);
    if (npc && !(npc == 5 ? c[HC_NPCS] >= 5 : npc == c[HC_NPCS])) return false;
    uint8_t hid = (uint8_t)(e[0x10] >> 5);
    if (hid && !(hid == 4 ? c[HC_HIDDEN] >= 4 : hid == c[HC_HIDDEN])) return false;
    uint8_t lvl = (uint8_t)((e[0x10] >> 1) & 3);
    if (lvl && lvl != c[HC_LEVEL]) return false;
    if ((e[0x10] >> 3) & 3) return false;                                    /* dead bit */
    uint8_t type = e[4] & 0x1F;
    if (type && type != c[HC_TYPE1] && type != c[HC_TYPE2]) return false;
    uint8_t w = (uint8_t)(rd16(e + 8) & 7);
    if (w && w != c[HC_WEATHER]) return false;
    uint16_t tile = rd16(e + 0x0E);
    if (tile && tile != c[HC_TILE]) return false;
    if (e[5] && e[5] != c[HC_ENCOUNTER]) return false;
    uint16_t map = rd16(e + 0x0C);
    if (map && (uint16_t)(map - 1) != x->map_id) return false;
    uint8_t tod = (uint8_t)((rd16(e + 0x0A) >> 3) & 7);
    if (tod && tod != c[HC_TIME]) return false;
    uint8_t mood = e[1] & 0x0F;
    if (mood && !hgss_mood_band(mood, (int8_t)c[HC_MOOD])) return false;
    uint8_t ath = (uint8_t)(e[4] >> 5);
    if (ath && ath != c[HC_ATHLON]) return false;
    uint8_t fr = (uint8_t)(e[1] >> 4);
    if (fr && !hgss_friend_band(fr, c[HC_FRIEND])) return false;
    uint8_t nat = e[2] & 7;
    if (nat && nat != c[HC_NATURE]) return false;
    if (e[6] && !species_class_ok(e[6], c[HC_SPECIES_CLASS])) return false;
    if (e[7] >= 1 && e[7] <= 5 && (c[HC_LEAVES] & (1u << (e[7] - 1)))) return false;
    uint8_t g = (uint8_t)((e[2] >> 3) & 3);
    if (g && g != c[HC_GENDER]) return false;
    uint8_t face = (uint8_t)(rd16(e + 8) >> 13);
    if (face && face != c[HC_FACING]) return false;
    return true;
}

int32_t hgss_rule_pick(const uint8_t* rules, const HgssTalkCtx* ctx, HgssRand rnd, HgssResultOk ok, void* ud) {
    static const uint8_t k_ranges[3][2] = {{30, 41}, {0, 29}, {42, 99}};
    for (int r = 0; r < 3; r++)
        for (int i = k_ranges[r][0]; i <= k_ranges[r][1]; i++) {
            const uint8_t* e = rules + (size_t)i * HGSS_RULE_SIZE;
            uint16_t res = hgss_rule_result(e);
            if (res == 0 || !ok(res, ud)) continue;
            if (rule_passes(e, ctx, rnd, ud)) return i;
        }
    return -1;
}

bool hgss_script_parse(const uint8_t* b, uint32_t len, HgssTalkScript* out) {
    if (len < HGSS_SCRIPT_SIZE) return false;
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < HGSS_STEPS; i++) {
        const uint8_t* s = b + 8 * i;
        if (rd16(s) == 0xFFFF) break;
        HgssTalkStep* t = &out->step[out->step_count++];
        t->anim = rd16(s);
        t->msg = rd16(s + 2);
        t->sound = rd16(s + 4);
        t->emote = s[6];
        t->wait = s[7];
    }
    out->yes_no = (b[0x28] | b[0x29] | b[0x2A] | b[0x2B]) != 0;
    out->next_yes = rd16(b + 0x2C);
    out->next_no = rd16(b + 0x2E);
    out->friend_delta = (int8_t)b[0x30];
    out->mood_delta = (int8_t)b[0x31];
    out->fashion = b[0x32];
    out->leaf = b[0x33];
    return true;
}

uint8_t hgss_anim_parse(const uint8_t* b, uint32_t len, HgssAnimFrame out[HGSS_ANIM_FRAMES]) {
    uint8_t n = 0;
    for (; n < HGSS_ANIM_FRAMES && 8u * n + 8 <= len; n++) {
        const uint8_t* f = b + 8 * n;
        if (f[0] == 0xFF) break;
        out[n].facing = f[0];
        out[n].dur = f[1];
        out[n].dx = (int8_t)f[2];
        out[n].dy = (int8_t)f[3];
        out[n].dz = (int8_t)f[4];
        out[n].sound = f[5] != 0;
    }
    return n;
}
