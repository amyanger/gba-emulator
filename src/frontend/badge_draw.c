#include "frontend/badge_draw.h"
#include <math.h>

#define MISSING_COLOR 0xFF2E3442u
#define KANTO_RAINBOW 3

/* Presentation only. Kanto: Boulder, Cascade, Thunder, Rainbow, Soul, Marsh, Volcano, Earth. */
uint32_t badge_color(GameRegion r, int i) {
    static const uint32_t hoenn[8] = {0xFFA8B0C4, 0xFFD9A85B, 0xFFF2C94C, 0xFFEB5757,
                                      0xFFC9B27C, 0xFF8DB2F0, 0xFFFF5D8F, 0xFF4C9BFF};
    static const uint32_t kanto[8] = {0xFFA8B0C4, 0xFF4C9BFF, 0xFFF2C94C, 0xFFB58CFF,
                                      0xFFFF5D8F, 0xFFD9A85B, 0xFFEB5757, 0xFF5BBF6A};
    if (i < 0 || i > 7) return 0xFF000000u;
    return r == GAME_REGION_KANTO ? kanto[i] : hoenn[i];
}

static uint8_t mix8(uint8_t a, uint8_t b, float t) { return (uint8_t)(a + (b - a) * t + 0.5f); }

static uint32_t mix_rgb(uint32_t a, uint8_t target, float t) {
    return 0xFF000000u | (uint32_t)mix8((uint8_t)(a >> 16), target, t) << 16 |
           (uint32_t)mix8((uint8_t)(a >> 8), target, t) << 8 | mix8((uint8_t)a, target, t);
}

static uint32_t tint(uint32_t base, uint8_t idx) {
    switch (idx) {
    case 1: return mix_rgb(base, 255, 0.65f);
    case 2: return mix_rgb(base, 255, 0.25f);
    case 3: return base;
    case 4: return mix_rgb(base, 0, 0.35f);
    default: return mix_rgb(base, 0, 0.78f);
    }
}

/* Hue from the angle around the badge centre, HSV s 0.55 v 0.95, integer RGB. */
static uint32_t rainbow_base(int x, int y) {
    const float pi = 3.14159265f;
    float h = atan2f((float)y - 7.5f, (float)x - 7.5f) / (2.0f * pi);
    if (h < 0.0f) h += 1.0f;
    float hp = h * 6.0f;
    int sector = (int)hp;
    if (sector > 5) sector = 5;
    float f = hp - (float)sector, v = 0.95f, s = 0.55f;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    float rgb[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
    uint32_t r = (uint32_t)(rgb[sector][0] * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(rgb[sector][1] * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(rgb[sector][2] * 255.0f + 0.5f);
    return 0xFF000000u | r << 16 | g << 8 | b;
}

void badge_draw(UiCanvas* c, float x, float y, float box, const uint8_t px[256], GameRegion r, int i,
                bool have) {
    int k = (int)(box / 16.0f * c->scale + 0.25f);
    if (k < 1) k = 1;
    int side = 16 * k;
    int ox = ui_px(c, x + box / 2) - side / 2;
    int oy = ui_px(c, y + box / 2) - side / 2;
    bool rainbow = have && r == GAME_REGION_KANTO && i == KANTO_RAINBOW;
    uint32_t base = badge_color(r, i);
    for (int sy = 0; sy < 16; sy++)
        for (int sx = 0; sx < 16; sx++) {
            uint8_t idx = px[sy * 16 + sx];
            if (idx == 0) continue;
            uint32_t col = !have ? MISSING_COLOR : tint(rainbow ? rainbow_base(sx, sy) : base, idx);
            for (int dy = 0; dy < k; dy++) {
                int py = oy + sy * k + dy;
                if (py < c->cy0 || py >= c->cy1) continue;
                for (int dx = 0; dx < k; dx++) {
                    int pxx = ox + sx * k + dx;
                    if (pxx < c->cx0 || pxx >= c->cx1) continue;
                    c->px[py * c->w + pxx] = col;
                }
            }
        }
}
