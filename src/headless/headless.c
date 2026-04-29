#include "headless/headless.h"
#include "headless/fb_hash.h"
#include "common.h"

int headless_run(GBA* gba, int frames, FILE* hash_out) {
    if (frames < 0) return 1;
    for (int i = 0; i < frames; i++) {
        gba_run_frame(gba);
        uint32_t h = fb_hash_fnv1a(gba->ppu.framebuffer,
                                   (size_t)SCREEN_WIDTH * SCREEN_HEIGHT);
        if (fprintf(hash_out, "%d %08x\n", i, h) < 0) {
            return 1;
        }
    }
    return 0;
}
