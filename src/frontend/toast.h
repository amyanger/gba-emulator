#ifndef TOAST_H
#define TOAST_H

#include <stdbool.h>
#include <stdint.h>

/* Toasts (short in-window notices) and status badges. Pure C, no SDL, no heap:
 * callers pass wall-clock milliseconds so fades run at normal speed while the
 * game is paused, fast-forwarding or rewinding. hud_draw.c paints this state. */

typedef enum { TOAST_OK, TOAST_INFO, TOAST_WARN, TOAST_ERROR } ToastKind;
typedef enum { BADGE_PAUSED, BADGE_FAST, BADGE_REWIND, BADGE_MUTED, BADGE_COUNT } BadgeId;

#define TOAST_MAX          4      /* non-fading toasts on screen */
#define TOAST_CAP          6      /* storage, so a new toast shows while old ones fade */
#define TOAST_TITLE_LEN    48
#define TOAST_DETAIL_LEN   48
#define TOAST_IN_MS        150u
#define TOAST_HOLD_MS      2500u
#define TOAST_HOLD_LONG_MS 4000u  /* WARN and ERROR */
#define TOAST_OUT_MS       300u

typedef struct {
    ToastKind kind;
    char key[TOAST_TITLE_LEN];     /* merge key; the title unless given */
    char title[TOAST_TITLE_LEN];
    char detail[TOAST_DETAIL_LEN]; /* "" = none */
    uint32_t in_ms;                /* slide-in start */
    uint32_t out_ms;               /* fade-out start; removed at out_ms + TOAST_OUT_MS */
} Toast;

typedef struct {
    Toast items[TOAST_CAP];        /* oldest first */
    uint32_t count;
    bool badges[BADGE_COUNT];
    bool changed;                  /* push, merge, expiry or badge change since last cleared */
} ToastQueue;

void  toast_init(ToastQueue* q);
/* Merges into a visible toast with the same title, else appends. */
void  toast_push(ToastQueue* q, uint32_t now_ms, ToastKind kind, const char* title, const char* detail);
/* Same, but merges by key, so on/off variants of one toggle replace each other. */
void  toast_push_keyed(ToastQueue* q, uint32_t now_ms, const char* key, ToastKind kind,
                       const char* title, const char* detail);
void  toast_set_badge(ToastQueue* q, BadgeId id, bool on);
void  toast_tick(ToastQueue* q, uint32_t now_ms);
bool  toast_animating(const ToastQueue* q, uint32_t now_ms);
float toast_alpha(const Toast* t, uint32_t now_ms);   /* 0..1 */
float toast_slide(const Toast* t, uint32_t now_ms);   /* 0..1, 1 = settled */
const char* toast_savestate_error(int32_t ss_result); /* SaveStateResult -> plain text */
/* One report per failure streak: true on the first failed attempt after a success
 * (or start); a successful attempt re-arms it; no attempt changes nothing. */
bool  toast_failure_edge(bool* reported, bool attempted, bool ok);

#endif // TOAST_H
