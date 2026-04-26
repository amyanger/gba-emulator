#include "xray.h"
#include "xray_draw.h"
#include "rewind/rewind.h"

void xray_render_rewind(uint32_t* buf, int buf_w, int buf_h,
                        int px, int py, int pw, int ph,
                        const RewindBuffer* rb) {
    (void)pw;
    (void)ph;
    if (!rb) return;

    uint32_t depth    = rewind_depth(rb);
    uint32_t capacity = rb->capacity;
    double   mb       = (double)rewind_bytes_used(rb) / (1024.0 * 1024.0);

    /* Line 1: depth/capacity and memory used. Always shown. */
    xray_draw_textf(buf, buf_w, buf_h, px + 4, py + 4,
                    XRAY_COL_LABEL,
                    "REW depth: %u/%u   %.1f MB",
                    depth, capacity, mb);

    /* Line 2: live indicator only when rewind is active. */
    if (rewind_active(rb)) {
        xray_draw_text(buf, buf_w, buf_h, px + 4, py + 14,
                       "[REWINDING]", XRAY_COL_FLASH);
    }
}
