#include <stdint.h>
#include <stddef.h>

void* ui_stb_arena_alloc(size_t n);
void ui_stb_arena_reset(void);

#define STBTT_malloc(x, u) ((void)(u), ui_stb_arena_alloc(x))
#define STBTT_free(x, u) ((void)(x), (void)(u))
#define STB_TRUETYPE_IMPLEMENTATION
#include "ui/stb_truetype.h"

#define UI_STB_ARENA_SIZE (512 * 1024)

static uint8_t s_arena[UI_STB_ARENA_SIZE];
static size_t s_used;

void* ui_stb_arena_alloc(size_t n) {
    size_t aligned = (n + 15) & ~(size_t)15;
    if (n == 0 || aligned < n || aligned > UI_STB_ARENA_SIZE - s_used) return NULL;
    void* p = s_arena + s_used;
    s_used += aligned;
    return p;
}

void ui_stb_arena_reset(void) {
    s_used = 0;
}
