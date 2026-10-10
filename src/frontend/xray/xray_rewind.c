#include "xray.h"
#include "rewind/rewind.h"
#include "ui/ui_theme.h"

void xray_render_rewind(UiCanvas* c, float x, float y, float w, const RewindBuffer* rb) {
    if (!rb) return;
    double mb = (double)rewind_bytes_used(rb) / (1024.0 * 1024.0);
    float tw = xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_HEAD, x, y, w, UI_ALIGN_LEFT, UI_DIM,
                          "Rewind memory: %u of %u snapshots \xC2\xB7 %.1f MB",
                          (unsigned)rewind_depth(rb), (unsigned)rb->capacity, mb);
    if (rewind_active(rb))
        ui_text(c, UI_FONT_SEMIBOLD, XRAY_SIZE_HEAD, x + tw + 8.0f, y, 0, UI_ALIGN_LEFT, UI_WARN,
                "Rewinding");
}
