#include "frontend/cry_player.h"

#include <string.h>

static const int8_t k_delta[16] = {0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1};

/* pokeemerald species ids: Celebi 251, Treecko 277; gCryTable has 388 entries. */
#define CRY_LAST_DIRECT   251
#define CRY_FIRST_HOENN   277
#define CRY_HOENN_COUNT   135
#define CRY_UNOWN_SLOT    200
#define CRY_TABLE_COUNT   388

bool cry_species_index(const GameProfile* p, const GameMem* m, uint16_t species, uint32_t* index) {
    uint16_t id = 0;
    if (species == 0) return false;
    if (species <= CRY_LAST_DIRECT) {
        *index = species - 1u;
        return true;
    }
    if (species < CRY_FIRST_HOENN) {
        *index = CRY_UNOWN_SLOT;
        return true;
    }
    if (species - CRY_FIRST_HOENN >= CRY_HOENN_COUNT ||
        !game_mem_read16(m, p->hoenn_cry_ids + 2u * (species - CRY_FIRST_HOENN), &id) || id >= CRY_TABLE_COUNT)
        return false;
    *index = id;
    return true;
}

bool cry_decode(const uint8_t* w, uint32_t avail, int8_t* out, uint32_t cap, uint32_t* len, uint32_t* rate) {
    if (avail < 16) return false;
    uint32_t freq = (uint32_t)w[4] | ((uint32_t)w[5] << 8) | ((uint32_t)w[6] << 16) | ((uint32_t)w[7] << 24);
    uint32_t n = (uint32_t)w[12] | ((uint32_t)w[13] << 8) | ((uint32_t)w[14] << 16) | ((uint32_t)w[15] << 24);
    if (w[0] != 1 || n == 0 || n > cap) return false;
    uint32_t blocks = (n + 63) / 64;
    if (16 + blocks * 33 > avail) return false;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* b = w + 16 + (i / 64) * 33;
        uint32_t k = i % 64;
        if (k == 0) { out[i] = (int8_t)b[0]; continue; }
        uint8_t nib = k == 1 ? (b[1] & 0x0F) : (k % 2 == 0 ? (b[1 + k / 2] >> 4) : (b[1 + k / 2] & 0x0F));
        out[i] = (int8_t)(uint8_t)(out[i - 1] + k_delta[nib]);
    }
    *len = n;
    *rate = freq / 1024;
    return true;
}

void cry_start(CryPlayer* c) {
    c->pos_fp = 0;
    c->playing = c->len > 0 && c->rate > 0;
}

void cry_mix(CryPlayer* c, int16_t* stereo, uint32_t frames, uint32_t out_rate) {
    if (!c->playing || out_rate == 0) return;
    uint64_t step = ((uint64_t)c->rate << 32) / out_rate;
    for (uint32_t f = 0; f < frames; f++) {
        uint32_t i = (uint32_t)(c->pos_fp >> 32);
        if (i >= c->len) { c->playing = false; return; }
        int32_t s = (int32_t)c->pcm[i] * 96;          /* about -6 dB under full scale */
        for (int ch = 0; ch < 2; ch++) {
            int32_t v = stereo[2 * f + ch] + s;
            stereo[2 * f + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        c->pos_fp += step;
    }
}
