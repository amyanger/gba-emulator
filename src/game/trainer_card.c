#include "game/trainer_card.h"
#include "game/gen3_text.h"
#include "game/pokemon.h"
#include "game/runlog_read.h"
#include <string.h>

/* NuzFacts holds the box list: too big for the stack, and src/game has no heap. */
static NuzFacts s_nf;
static RunFacts s_rf;

static uint32_t fnv1a(uint32_t h, const void* data, uint32_t len) {
    const uint8_t* p = data;
    for (uint32_t i = 0; i < len; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint32_t fnv1a_u32(uint32_t h, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fnv1a(h, b, sizeof(b));
}

CardStatus trainer_card_read(const GameState* gs, const GameMem* m, TrainerCardData* out) {
    memset(out, 0, sizeof(*out));
    const GameProfile* p = gs->profile;
    if (!p || gs->support != GAME_SUPPORT_OK) return CARD_NOT_EMERALD;
    run_read(p, m, 0, &s_rf);
    nuz_read(p, m, false, &s_nf);
    if (!s_rf.valid || !s_nf.valid) return CARD_NO_TEAM;
    if (s_rf.facility) return CARD_FACILITY;

    uint32_t sig = 2166136261u;
    sig = fnv1a_u32(sig, s_rf.sb1);
    sig = fnv1a_u32(sig, s_rf.sb2);
    sig = fnv1a_u32(sig, s_nf.trainer_id);

    /* Same slots as read_party in game.c, so indexes match the Party page selection; bad
     * slots stay listed and trainer_card_pick_ace skips them. */
    uint8_t count = 0, good = 0;
    if (game_mem_read8(m, p->player_party_count, &count) && count > 6) count = 6;
    sig = fnv1a(sig, &count, 1);
    for (uint8_t i = 0; i < count; i++) {
        uint8_t raw[MON_PARTY_SIZE];
        MonData d;
        if (!game_mem_copy(m, p->player_party + (uint32_t)i * MON_PARTY_SIZE, raw, sizeof(raw))) break;
        sig = fnv1a(sig, raw, sizeof(raw));
        if (!pokemon_decode(raw, true, &d)) continue;
        game_fill_mon(m, p, &d, &out->party[out->count]);
        NuzKey k = { d.personality, d.ot_id, (uint8_t)(d.species == NUZ_SPECIES_SHEDINJA ? 1 : 0) };
        out->keys[out->count] = k;
        if (!d.bad) good++;
        out->count++;
    }
    out->sig = sig;
    if (good == 0) return CARD_NO_TEAM;

    gen3_decode(s_rf.player_name, sizeof(s_rf.player_name), out->name, GAME_NAME_LEN);
    out->public_id = (uint16_t)(s_nf.trainer_id & 0xFFFF);
    out->play_frames = s_nf.play_time;
    out->badges = s_rf.badges;
    out->dex_owned = s_rf.dex_owned;
    return CARD_OK;
}

static bool ace_ok(const GamePartyMon* mon) { return mon->present && !mon->bad && !mon->egg; }

int trainer_card_pick_ace(const TrainerCardData* d, int sel, const NuzKey* mvp) {
    if (sel >= 0 && sel < d->count && ace_ok(&d->party[sel])) return sel;
    if (mvp)
        for (int i = 0; i < d->count; i++)
            if (ace_ok(&d->party[i]) && nuz_key_eq(d->keys[i], *mvp)) return i;
    for (int i = 0; i < d->count; i++)
        if (ace_ok(&d->party[i])) return i;
    return -1;
}
