#ifndef SUPER_XBR_H
#define SUPER_XBR_H

#include <stdint.h>

/* Super-xBR 2x by Hyllian (MIT, see super_xbr.c). Opaque ARGB8888 in, (2w) x (2h) out. */
void super_xbr_scale2x(const uint32_t* in, int w, int h, uint32_t* out);

#endif // SUPER_XBR_H
