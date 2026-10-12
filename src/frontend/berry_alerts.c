#include "frontend/berry_alerts.h"
#include "frontend/game_panel.h"
#include <stdio.h>
#include <string.h>

#define SENT_RIPE  1u
#define SENT_WATER 2u
#define SENT_FALL  4u
#define MDOT " \xC2\xB7 "
#define FOLD_PLACES 3   /* this many places in one update become a single toast */

void berry_alerts_reset(BerryAlerts* a) {
    memset(a, 0, sizeof(*a));
    a->here = BERRY_PLACE_NONE;
}

void berry_alerts_rebase(BerryAlerts* a) {
    berry_alerts_reset(a);
    a->quiet = true;
}

static bool needs_water(const BerryTree* t) {
    return t->stage >= 1 && t->stage <= 4 && !((t->watered >> (t->stage - 1)) & 1);
}

/* Appends "N word" (or the singular) with a " · " separator. */
static void add_part(char* out, size_t size, unsigned n, const char* one, const char* many) {
    if (n == 0) return;
    size_t len = strlen(out);
    snprintf(out + len, size - len, "%s%u %s", len ? MDOT : "", n, n == 1 ? one : many);
}

static void place_name(const BerryView* v, uint8_t p, char* out, size_t size) {
    if (p < v->place_count) game_panel_title_case(v->place[p].name, out, size);
    else snprintf(out, size, "Berries");
}

static void place_key(const BerryView* v, uint8_t p, char* out, size_t size) {
    snprintf(out, size, "berry:%s", p < v->place_count ? v->place[p].name : "?");
}

/* "Oran is ripe", or counts when a place has several events. */
static void place_detail(const BerryAlerts* a, uint8_t p, char* out, size_t size) {
    char name[BERRY_NAME_LEN + 4];
    out[0] = '\0';
    unsigned total = (unsigned)a->ripe[p] + a->water[p] + a->fall[p];
    if (total == 1) {
        game_panel_title_case(a->one[p], name, sizeof(name));
        if (a->ripe[p]) snprintf(out, size, "%s is ripe", name);
        else if (a->water[p]) snprintf(out, size, "%s needs water", name);
        return;   /* falling fills its own text with the time */
    }
    add_part(out, size, a->fall[p], "dropping", "dropping");
    add_part(out, size, a->ripe[p], "ripe", "ripe");
    add_part(out, size, a->water[p], "needs water", "need water");
}

static uint8_t emit(BerryAlerts* a, const BerryView* v, BerryToast* out, uint8_t max) {
    uint8_t places = 0, n = 0;
    unsigned ripe = 0, water = 0, fall = 0;
    for (uint8_t p = 0; p < BERRY_PLACE_MAX; p++) {
        if (!(a->ripe[p] || a->water[p] || a->fall[p])) continue;
        places++;
        ripe += a->ripe[p];
        water += a->water[p];
        fall += a->fall[p];
    }
    if (places == 0 || max == 0) return 0;
    if (places >= FOLD_PLACES) {
        BerryToast* t = &out[n++];
        char parts[BERRY_TOAST_LEN] = "";
        add_part(parts, sizeof(parts), fall, "dropping", "dropping");
        add_part(parts, sizeof(parts), ripe, "ripe", "ripe");
        add_part(parts, sizeof(parts), water, "needs water", "need water");
        snprintf(t->key, sizeof(t->key), "berry:all");
        snprintf(t->title, sizeof(t->title), "Berries");
        /* Places can be shops as well as routes. Drop the count rather than cut a word. */
        char where[16];
        snprintf(where, sizeof(where), ", %u places", places);
        size_t len = strlen(parts), wlen = strlen(where);
        memcpy(t->detail, parts, len + 1);
        if (len + wlen < sizeof(t->detail)) memcpy(t->detail + len, where, wlen + 1);
    } else {
        for (uint8_t p = 0; p < BERRY_PLACE_MAX && n < max; p++) {
            if (!(a->ripe[p] || a->water[p] || a->fall[p])) continue;
            BerryToast* t = &out[n++];
            place_key(v, p, t->key, sizeof(t->key));
            place_name(v, p, t->title, sizeof(t->title));
            if (a->fall[p] == 1 && a->ripe[p] + a->water[p] == 0) {
                char name[BERRY_NAME_LEN + 4];
                game_panel_title_case(a->one[p], name, sizeof(name));
                snprintf(t->detail, sizeof(t->detail), "%s drops in %s", name, a->fall_in[p]);
            } else {
                place_detail(a, p, t->detail, sizeof(t->detail));
            }
        }
    }
    memset(a->ripe, 0, sizeof(a->ripe));
    memset(a->water, 0, sizeof(a->water));
    memset(a->fall, 0, sizeof(a->fall));
    return n;
}

/* Marks what is already true so it is never toasted later. */
static uint8_t sent_for(const BerryTree* t) {
    uint8_t s = 0;
    if (t->stage == 5) s |= SENT_RIPE;
    if (t->status == BERRY_FALLING) s |= SENT_FALL;
    if (needs_water(t)) s |= SENT_WATER;
    return s;
}

static uint8_t baseline(BerryAlerts* a, const BerryView* v, BerryToast* out, uint8_t max) {
    unsigned ripe = 0, water = 0;
    /* Before the game loads the save (boot, intro) there is nothing to compare against. */
    if (!v->clock_set || v->count == 0) return 0;
    for (uint8_t i = 0; i < v->count; i++) {
        const BerryTree* t = &v->tree[i];
        a->stage[t->id] = t->stage;
        a->sent[t->id] = sent_for(t);
        if (t->paused) continue;
        if (t->stage == 5) ripe++;
        else if (needs_water(t)) water++;
    }
    a->baselined = true;
    a->here = v->here;
    if (a->quiet || (ripe == 0 && water == 0) || max == 0) return 0;
    BerryToast* t = &out[0];
    snprintf(t->key, sizeof(t->key), "berry:all");
    snprintf(t->title, sizeof(t->title), "Berries");
    t->detail[0] = '\0';
    add_part(t->detail, sizeof(t->detail), ripe, "ripe", "ripe");
    add_part(t->detail, sizeof(t->detail), water, "needs water", "need water");
    return 1;
}

/* Arriving somewhere with trees that need you. Untouched ripe trees count here. */
static bool arrival(const BerryView* v, BerryToast* t) {
    unsigned ripe = 0, water = 0;
    for (uint8_t i = 0; i < v->count; i++) {
        const BerryTree* tr = &v->tree[i];
        if (tr->place != v->here) continue;
        if (tr->stage == 5) ripe++;
        else if (!tr->paused && needs_water(tr)) water++;
    }
    if (ripe == 0 && water == 0) return false;
    place_key(v, v->here, t->key, sizeof(t->key));
    place_name(v, v->here, t->title, sizeof(t->title));
    t->detail[0] = '\0';
    add_part(t->detail, sizeof(t->detail), ripe, "ripe", "ripe");
    add_part(t->detail, sizeof(t->detail), water, "needs water", "need water");
    return true;
}

uint8_t berry_alerts_update(BerryAlerts* a, const BerryView* v, BerryToast* out, uint8_t max) {
    if (!v->valid) return 0;
    if (!a->baselined) return baseline(a, v, out, max);

    bool seen[BERRY_TREE_COUNT] = {false};
    for (uint8_t i = 0; i < v->count; i++) {
        const BerryTree* t = &v->tree[i];
        uint8_t prev = a->stage[t->id];
        seen[t->id] = true;
        a->stage[t->id] = t->stage;
        if (prev == 0) {                  /* new tree (planted, or first seen): no events for now */
            a->sent[t->id] = sent_for(t);
            continue;
        }
        if (t->stage != prev) a->sent[t->id] = 0;
        uint8_t now = sent_for(t), fresh = (uint8_t)(now & ~a->sent[t->id]);
        if (t->stage == prev) fresh &= (uint8_t)~SENT_WATER;   /* water only when a stage starts */
        a->sent[t->id] |= now;
        if (!fresh || t->paused || t->place == v->here || t->place >= BERRY_PLACE_MAX) continue;
        uint8_t p = t->place;
        if (fresh & SENT_FALL) {
            a->fall[p]++;
            game_panel_berry_time(t->next_min, a->fall_in[p], sizeof(a->fall_in[p]));
        } else if (fresh & SENT_RIPE) {
            a->ripe[p]++;
        } else if (fresh & SENT_WATER) {
            a->water[p]++;
        }
        snprintf(a->one[p], sizeof(a->one[p]), "%s", t->name);
    }
    for (uint32_t id = 0; id < BERRY_TREE_COUNT; id++)
        if (!seen[id]) a->stage[id] = a->sent[id] = 0;

    uint8_t n = 0;
    if (v->here != a->here && v->here != BERRY_PLACE_NONE && max > 0 && arrival(v, &out[0])) n = 1;
    a->here = v->here;
    if (v->in_battle) return n;
    return (uint8_t)(n + emit(a, v, out + n, (uint8_t)(max - n)));
}
