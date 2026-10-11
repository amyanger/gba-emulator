#ifndef VIDEO_FILTER_H
#define VIDEO_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/* Screen filters and LCD color correction for the game image. Pure C, no SDL, no heap:
 * the output lives in static buffers owned by this module. */

typedef enum {
    VIDEO_FILTER_SHARP,    /* nearest neighbour, the original pixels */
    VIDEO_FILTER_SMOOTH,   /* xBR */
    VIDEO_FILTER_SOFT,     /* Super-xBR */
    VIDEO_FILTER_COUNT
} VideoFilter;

#define VIDEO_MAX_FACTOR 4

/* How the filtered image is sized for a game drawn k times the GBA size: the filter
 * output is factor times the GBA size, and is stretched to k with linear or nearest. */
typedef struct {
    int factor;
    bool linear;
} VideoScale;

typedef struct {
    const uint32_t* px;   /* opaque ARGB8888, w x h */
    int w, h;
    bool linear;
} VideoFrame;

const char* video_filter_name(VideoFilter f);    /* "sharp", "smooth", "soft": prefs and CLI */
const char* video_filter_label(VideoFilter f);   /* "Sharp", "Smooth", "Soft": UI */
bool video_filter_from_name(const char* name, VideoFilter* out);

VideoScale video_filter_scale(VideoFilter f, int k);

/* The BGR555 -> ARGB8888 color for c, plain (5 to 8 bit) or LCD color corrected. */
uint32_t video_color(uint16_t c, bool color_correct);

/* True when the frame needs the ARGB path; Sharp without correction can upload BGR555 as is. */
bool video_filter_active(VideoFilter f, bool color_correct);

/* Filters a 240x160 BGR555 framebuffer for a game drawn k times the GBA size.
 * Returns false when the input and settings match the previous call (out still filled). */
bool video_filter_run(const uint16_t* fb, VideoFilter f, bool color_correct, int k, VideoFrame* out);

/* Forget the previous frame, so the next video_filter_run reports a change. */
void video_filter_invalidate(void);

#endif // VIDEO_FILTER_H
