#include "frontend/toast.h"
#include "savestate/savestate.h"

#include <stdio.h>
#include <string.h>

/* Signed difference so a wrapped 32-bit ms clock still compares correctly. */
static int32_t since(uint32_t now, uint32_t then) { return (int32_t)(now - then); }

static uint32_t hold_for(ToastKind k) {
    return (k == TOAST_WARN || k == TOAST_ERROR) ? TOAST_HOLD_LONG_MS : TOAST_HOLD_MS;
}

static bool fading_out(const Toast* t, uint32_t now) { return since(now, t->out_ms) >= 0; }

void toast_init(ToastQueue* q) { memset(q, 0, sizeof(*q)); }

static void fill(Toast* t, ToastKind kind, const char* title, const char* detail) {
    t->kind = kind;
    snprintf(t->title, sizeof(t->title), "%s", title ? title : "");
    snprintf(t->detail, sizeof(t->detail), "%s", detail ? detail : "");
}

static void remove_at(ToastQueue* q, uint32_t i) {
    memmove(&q->items[i], &q->items[i + 1], (q->count - i - 1) * sizeof(Toast));
    q->count--;
}

void toast_push_keyed(ToastQueue* q, uint32_t now, const char* key, ToastKind kind,
                      const char* title, const char* detail) {
    if (!key) key = title ? title : "";
    q->changed = true;
    for (uint32_t i = 0; i < q->count; i++) {
        Toast* t = &q->items[i];
        if (strncmp(t->key, key, sizeof(t->key) - 1) != 0) continue;
        fill(t, kind, title, detail);
        /* A settled toast stays fully in (no replayed slide); one still sliding in
         * keeps its slide and holds from when it settles. */
        uint32_t settled = t->in_ms + TOAST_IN_MS;
        bool was_settled = since(now, settled) > 0;
        t->out_ms = (was_settled ? now : settled) + hold_for(kind);
        if (was_settled) t->in_ms = now - TOAST_IN_MS;
        return;
    }

    uint32_t live = 0;
    for (uint32_t i = 0; i < q->count; i++)
        if (!fading_out(&q->items[i], now)) live++;
    if (live >= TOAST_MAX) {
        for (uint32_t i = 0; i < q->count; i++) {
            if (!fading_out(&q->items[i], now)) {
                q->items[i].out_ms = now;
                break;
            }
        }
    }
    if (q->count == TOAST_CAP) remove_at(q, 0);

    Toast* t = &q->items[q->count++];
    memset(t, 0, sizeof(*t));
    snprintf(t->key, sizeof(t->key), "%s", key);
    fill(t, kind, title, detail);
    t->in_ms = now;
    t->out_ms = now + TOAST_IN_MS + hold_for(kind);
}

void toast_push(ToastQueue* q, uint32_t now, ToastKind kind, const char* title, const char* detail) {
    toast_push_keyed(q, now, NULL, kind, title, detail);
}

void toast_set_badge(ToastQueue* q, BadgeId id, bool on) {
    if (id >= BADGE_COUNT || q->badges[id] == on) return;
    q->badges[id] = on;
    q->changed = true;
}

void toast_tick(ToastQueue* q, uint32_t now) {
    for (uint32_t i = 0; i < q->count;) {
        if (since(now, q->items[i].out_ms + TOAST_OUT_MS) >= 0) {
            remove_at(q, i);
            q->changed = true;
        } else {
            i++;
        }
    }
}

bool toast_animating(const ToastQueue* q, uint32_t now) {
    for (uint32_t i = 0; i < q->count; i++) {
        const Toast* t = &q->items[i];
        if (since(now, t->in_ms + TOAST_IN_MS) < 0 || fading_out(t, now)) return true;
    }
    return false;
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

float toast_alpha(const Toast* t, uint32_t now) {
    float in = clamp01((float)since(now, t->in_ms) / (float)TOAST_IN_MS);
    float out = 1.0f - clamp01((float)since(now, t->out_ms) / (float)TOAST_OUT_MS);
    return in < out ? in : out;
}

float toast_slide(const Toast* t, uint32_t now) {
    float f = clamp01((float)since(now, t->in_ms) / (float)TOAST_IN_MS);
    return 1.0f - (1.0f - f) * (1.0f - f);   /* ease out */
}

const char* toast_savestate_error(int32_t r) {
    switch (r) {
    case SS_ERR_FILE_OPEN:    return "Can't open the file";
    case SS_ERR_FILE_WRITE:   return "Can't write the file";
    case SS_ERR_FILE_READ:    return "Can't read the file";
    case SS_ERR_BAD_MAGIC:    return "Not a save state";
    case SS_ERR_BAD_VERSION:  return "Made by an older version";
    case SS_ERR_ROM_MISMATCH: return "Made with another ROM";
    case SS_ERR_CORRUPT:      return "File is damaged";
    case SS_ERR_TRUNCATED:    return "File is incomplete";
    default:                  return "Unknown error";
    }
}

bool toast_failure_edge(bool* reported, bool attempted, bool ok) {
    if (!attempted) return false;
    if (ok) {
        *reported = false;
        return false;
    }
    if (*reported) return false;
    *reported = true;
    return true;
}
