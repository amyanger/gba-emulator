#ifndef XBR_H
#define XBR_H

#include <stdint.h>

/* xBR pixel-art scaler by Hyllian, ported from xBRjs (MIT, see xbr.c).
 * Pixels are opaque ARGB8888. out must hold (w * factor) x (h * factor) pixels. */
void xbr_scale(const uint32_t* in, int w, int h, uint32_t* out, int factor);   /* factor 2, 3 or 4 */

#endif // XBR_H
