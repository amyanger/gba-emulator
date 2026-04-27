#define _POSIX_C_SOURCE 200809L

#include "screenshot.h"

#include <stdio.h>
#include <stdlib.h>

#include "common.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void screenshot_path(const char* rom_path, time_t now, char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    struct tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif

    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm_utc);
    snprintf(out, out_size, "%s.%s.png", rom_path ? rom_path : "screenshot", ts);
}

bool screenshot_save(const uint16_t* framebuffer_abgr1555, const char* path) {
    if (!framebuffer_abgr1555 || !path) return false;

    const int32_t w = SCREEN_WIDTH;
    const int32_t h = SCREEN_HEIGHT;
    const size_t pixel_count = (size_t)w * (size_t)h;

    uint8_t* rgb = (uint8_t*)malloc(pixel_count * 3);
    if (!rgb) return false;

    /* SDL_PIXELFORMAT_ABGR1555 layout in a uint16_t (LSB first):
     *   bits  0..4 : R
     *   bits  5..9 : G
     *   bits 10..14: B
     *   bit  15    : A (ignored — GBA framebuffer is opaque) */
    for (size_t i = 0; i < pixel_count; i++) {
        uint16_t px = framebuffer_abgr1555[i];
        uint8_t r5 = (uint8_t)(px & 0x1F);
        uint8_t g5 = (uint8_t)((px >> 5) & 0x1F);
        uint8_t b5 = (uint8_t)((px >> 10) & 0x1F);
        rgb[i * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
        rgb[i * 3 + 1] = (uint8_t)((g5 << 3) | (g5 >> 2));
        rgb[i * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }

    int rc = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);

    if (!rc) {
        LOG_ERROR("screenshot: failed to write %s", path);
        return false;
    }
    return true;
}
