#include "xray.h"
#include "xray_draw.h"
#include "cpu/arm7tdmi.h"

static const char* cpu_mode_name(uint32_t cpsr) {
    switch (cpsr & 0x1F) {
    case 0x10: return "USR";
    case 0x11: return "FIQ";
    case 0x12: return "IRQ";
    case 0x13: return "SVC";
    case 0x17: return "ABT";
    case 0x1B: return "UND";
    case 0x1F: return "SYS";
    default:   return "???";
    }
}

void xray_render_cpu(uint32_t* buf, int buf_w, int buf_h, int px, int py,
                     int pw, int ph, ARM7TDMI* cpu, XRayState* state) {
    (void)pw;
    (void)ph;

    int x0 = px + 8;
    int y0 = py + 18;  /* Below panel header */

    /* Registers R0-R7 (left column) */
    for (int i = 0; i < 8; i++) {
        int y = y0 + i * 12;
        xray_draw_textf(buf, buf_w, buf_h, x0, y, XRAY_COL_LABEL,
                        "R%-2d", i);
        xray_draw_textf(buf, buf_w, buf_h, x0 + 32, y, XRAY_COL_VALUE,
                        "%08X", cpu->regs[i]);
    }

    /* Registers R8-R15 (right column) */
    int x1 = px + 160;
    for (int i = 8; i < 16; i++) {
        int y = y0 + (i - 8) * 12;
        const char* name;
        switch (i) {
        case 13: name = "SP"; break;
        case 14: name = "LR"; break;
        case 15: name = "PC"; break;
        default:
            xray_draw_textf(buf, buf_w, buf_h, x1, y, XRAY_COL_LABEL,
                            "R%-2d", i);
            xray_draw_textf(buf, buf_w, buf_h, x1 + 32, y, XRAY_COL_VALUE,
                            "%08X", cpu->regs[i]);
            continue;
        }
        xray_draw_textf(buf, buf_w, buf_h, x1, y, XRAY_COL_LABEL, "%s", name);
        xray_draw_textf(buf, buf_w, buf_h, x1 + 32, y, XRAY_COL_VALUE,
                        "%08X", cpu->regs[i]);
    }

    /* Separator line */
    int sep_y = y0 + 8 * 12 + 4;
    xray_draw_hline(buf, buf_w, buf_h, x0, sep_y, 300, XRAY_COL_BORDER);

    /* CPSR flags */
    int fy = sep_y + 8;
    xray_draw_text(buf, buf_w, buf_h, x0, fy, "CPSR", XRAY_COL_LABEL);
    xray_draw_textf(buf, buf_w, buf_h, x0 + 48, fy, XRAY_COL_VALUE,
                    "%08X", cpu->cpsr);

    /* Individual flags as lit/unlit indicators */
    int fx = x0 + 160;
    struct { char name; int bit; } flags[] = {
        {'N', CPSR_N}, {'Z', CPSR_Z}, {'C', CPSR_C}, {'V', CPSR_V},
        {'I', CPSR_I}, {'F', CPSR_F}, {'T', CPSR_T}
    };
    for (int i = 0; i < 7; i++) {
        bool set = (cpu->cpsr >> flags[i].bit) & 1;
        uint32_t color = set ? XRAY_COL_VALUE : XRAY_COL_DIM;
        char flag_str[2] = { flags[i].name, '\0' };
        xray_draw_text(buf, buf_w, buf_h, fx, fy, flag_str, color);
        fx += 12;
    }

    /* CPU Mode */
    int my = fy + 14;
    xray_draw_text(buf, buf_w, buf_h, x0, my, "Mode", XRAY_COL_LABEL);
    xray_draw_text(buf, buf_w, buf_h, x0 + 48, my, cpu_mode_name(cpu->cpsr),
                   XRAY_COL_VALUE);

    /* Thumb/ARM indicator */
    bool thumb = (cpu->cpsr >> CPSR_T) & 1;
    xray_draw_text(buf, buf_w, buf_h, x0 + 100, my,
                   thumb ? "THUMB" : "ARM", XRAY_COL_HEADER);

    /* Halted state */
    if (cpu->halted) {
        xray_draw_text(buf, buf_w, buf_h, x0 + 170, my, "HALTED",
                       XRAY_COL_FLASH);
    }

    /* Current instruction */
    int iy = my + 14;
    uint32_t pc = cpu->regs[15];
    uint32_t instr = cpu->pipeline[0];
    xray_draw_text(buf, buf_w, buf_h, x0, iy, "Instr", XRAY_COL_LABEL);
    xray_draw_textf(buf, buf_w, buf_h, x0 + 48, iy, XRAY_COL_VALUE,
                    "%08X @ %08X", instr, pc);

    /* IPS counter */
    int ipy = iy + 14;
    xray_draw_text(buf, buf_w, buf_h, x0, ipy, "IPS", XRAY_COL_LABEL);
    uint64_t ips = state->ips_display;
    if (ips > 1000000) {
        xray_draw_textf(buf, buf_w, buf_h, x0 + 48, ipy, XRAY_COL_VALUE,
                        "%.2f M", (double)ips / 1000000.0);
    } else if (ips > 1000) {
        xray_draw_textf(buf, buf_w, buf_h, x0 + 48, ipy, XRAY_COL_VALUE,
                        "%.1f K", (double)ips / 1000.0);
    } else {
        xray_draw_textf(buf, buf_w, buf_h, x0 + 48, ipy, XRAY_COL_VALUE,
                        "%llu", (unsigned long long)ips);
    }

    /* Pipeline state */
    int ppy = ipy + 14;
    xray_draw_text(buf, buf_w, buf_h, x0, ppy, "Pipe", XRAY_COL_LABEL);
    xray_draw_textf(buf, buf_w, buf_h, x0 + 48, ppy,
                    cpu->pipeline_valid ? XRAY_COL_VALUE : XRAY_COL_DIM,
                    "[%08X] [%08X] %s",
                    cpu->pipeline[0], cpu->pipeline[1],
                    cpu->pipeline_valid ? "valid" : "flushed");
}
