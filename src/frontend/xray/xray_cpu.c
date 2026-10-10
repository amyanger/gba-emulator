#include "xray.h"
#include "cpu/arm7tdmi.h"
#include "ui/ui_theme.h"
#include "ui/ui_widgets.h"
#include <stdio.h>

static const char* mode_name(uint32_t cpsr) {
    switch (cpsr & 0x1F) {
    case 0x10: return "User mode";
    case 0x11: return "Fast interrupt";
    case 0x12: return "Interrupt";
    case 0x13: return "Supervisor";
    case 0x17: return "Abort";
    case 0x1B: return "Undefined";
    case 0x1F: return "System mode";
    default:   return "Unknown mode";
    }
}

#define REG_ROW 15.0f

uint32_t xray_cpu_exec_addr(const ARM7TDMI* cpu) {
    bool thumb = (cpu->cpsr >> CPSR_T) & 1;
    return cpu->regs[15] - (thumb ? 4u : 8u);
}

static void reg(UiCanvas* c, float x, float y, const char* name, uint32_t v) {
    ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x, y, 0, UI_ALIGN_LEFT, UI_MUTED, name);
    xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x + 120.0f, y, 0, UI_ALIGN_RIGHT, UI_TEXT,
               "%08X", (unsigned)v);
}

static void detail(UiCanvas* c, float x, float y, const char* label) {
    ui_text(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x, y, 0, UI_ALIGN_LEFT, UI_MUTED, label);
}

void xray_render_cpu(UiCanvas* c, ARM7TDMI* cpu) {
    float top = xray_card(c, XRAY_CARD_CPU, "Clock 16.78 MHz");
    UiRect r = xray_cards[XRAY_CARD_CPU];
    float x0 = r.x + XRAY_PAD, right = r.x + r.w - XRAY_PAD;

    static const char* hi[8] = {"R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC"};
    char name[4];
    for (int i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "R%d", i);
        reg(c, x0, top + (float)i * REG_ROW, name, cpu->regs[i]);
        reg(c, x0 + 160.0f, top + (float)i * REG_ROW, hi[i], cpu->regs[8 + i]);
    }

    /* State pills and flags, right of the registers */
    float sx = x0 + 300.0f;
    ui_section_label(c, sx, top, "STATE");
    bool thumb = (cpu->cpsr >> CPSR_T) & 1;
    float py = top + 16.0f;
    ui_pill(c, sx, py, thumb ? "Thumb code" : "ARM code", UI_SELECTED);
    py += 20.0f;
    ui_pill(c, sx, py, mode_name(cpu->cpsr), UI_SELECTED);
    py += 20.0f;
    if (cpu->halted) {
        ui_pill(c, sx, py, "Halted, waiting", UI_WARN);
        py += 20.0f;
    }
    ui_section_label(c, sx, py + 6.0f, "FLAGS");
    py += 22.0f;
    static const char* flag_names[7] = {"N", "Z", "C", "V", "I", "F", "T"};
    static const int flag_bits[7] = {CPSR_N, CPSR_Z, CPSR_C, CPSR_V, CPSR_I, CPSR_F, CPSR_T};
    float fx = sx;
    for (int i = 0; i < 7; i++) {
        float w = ui_text_width(c, UI_FONT_SEMIBOLD, UI_SIZE_PILL, flag_names[i]) + 14.0f;
        if (fx + w > right) {
            fx = sx;
            py += 20.0f;
        }
        bool set = (cpu->cpsr >> flag_bits[i]) & 1;
        fx += ui_pill(c, fx, py, flag_names[i], set ? UI_ACCENT : UI_TRACK) + 3.0f;
    }

    /* Instruction details under the registers */
    float y = top + 8 * REG_ROW + 8.0f;
    ui_hline(c, x0, y, r.w - 2 * XRAY_PAD, UI_DIVIDER);
    y += 8.0f;
    detail(c, x0, y, "Instruction");
    if (thumb)
        xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x0 + 100.0f, y, 0, UI_ALIGN_LEFT, UI_TEXT,
                   "%04X at %08X", (unsigned)(cpu->pipeline[0] & 0xFFFF),
                   (unsigned)xray_cpu_exec_addr(cpu));
    else
        xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x0 + 100.0f, y, 0, UI_ALIGN_LEFT, UI_TEXT,
                   "%08X at %08X", (unsigned)cpu->pipeline[0], (unsigned)xray_cpu_exec_addr(cpu));
    y += XRAY_ROW;
    detail(c, x0, y, "Pipeline");
    xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x0 + 100.0f, y, 0, UI_ALIGN_LEFT,
               cpu->pipeline_valid ? UI_TEXT : UI_FAINT, "%08X  %08X  %s",
               (unsigned)cpu->pipeline[0], (unsigned)cpu->pipeline[1],
               cpu->pipeline_valid ? "ready" : "refilling");
    y += XRAY_ROW;
    detail(c, x0, y, "Status (CPSR)");
    xray_textf(c, UI_FONT_REGULAR, XRAY_SIZE_TEXT, x0 + 100.0f, y, 0, UI_ALIGN_LEFT, UI_TEXT,
               "%08X", (unsigned)cpu->cpsr);
}
