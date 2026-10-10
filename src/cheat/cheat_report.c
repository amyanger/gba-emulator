#include "cheat/cheat_report.h"

const char* cheat_device_name(CheatDevice dev) {
    switch (dev) {
    case CHEAT_DEV_CODEBREAKER: return "CodeBreaker";
    case CHEAT_DEV_GSA:         return "GameShark v1/v2";
    case CHEAT_DEV_PAR3:        return "Action Replay v3";
    case CHEAT_DEV_GSA_RAW:     return "GameShark v1/v2 (raw)";
    case CHEAT_DEV_PAR3_RAW:    return "Action Replay v3 (raw)";
    default:                    return "unknown device";
    }
}

static const char* cmp_text(uint8_t cmp) {
    static const char* const t[] = {"==", "!=", "< (signed)", "> (signed)", "<", ">", "<=", ">=", "&", "keys", "never"};
    return cmp < sizeof(t) / sizeof(t[0]) ? t[cmp] : "?";
}

static void print_op(const CheatOp* op, FILE* out) {
    unsigned bits = op->width * 8u, digits = op->width * 2u;
    switch (op->kind) {
    case CHEAT_OP_WRITE:
        if (op->flags & CHEAT_OPF_INDIRECT)
            fprintf(out, "      write%u [[%08X] + %X] = %0*X\n", bits, (unsigned)op->addr, (unsigned)op->value2,
                    (int)digits, (unsigned)op->value);
        else
            fprintf(out, "      write%u [%08X] = %0*X\n", bits, (unsigned)op->addr, (int)digits, (unsigned)op->value);
        break;
    case CHEAT_OP_ADD: fprintf(out, "      add%u [%08X] += %X\n", bits, (unsigned)op->addr, (unsigned)op->value); break;
    case CHEAT_OP_OR:  fprintf(out, "      or%u [%08X] |= %X\n", bits, (unsigned)op->addr, (unsigned)op->value); break;
    case CHEAT_OP_AND: fprintf(out, "      and%u [%08X] &= %X\n", bits, (unsigned)op->addr, (unsigned)op->value); break;
    case CHEAT_OP_SLIDE:
        fprintf(out, "      slide%u [%08X] = %X, x%u, address +%u, value +%d\n", bits, (unsigned)op->addr,
                (unsigned)op->value, (unsigned)op->count, (unsigned)op->addr_step, (int)op->value_step);
        break;
    case CHEAT_OP_IF: {
        char skip[24];
        if (op->skip == CHEAT_SKIP_BLOCK) snprintf(skip, sizeof(skip), "block");
        else snprintf(skip, sizeof(skip), "next %u", (unsigned)op->skip);
        if (op->cmp == CHEAT_CMP_KEYS) fprintf(out, "      if keys %04X held (%s)\n", (unsigned)op->value, skip);
        else if (op->cmp == CHEAT_CMP_NEVER) fprintf(out, "      if never (%s)\n", skip);
        else
            fprintf(out, "      if%u [%08X] %s %0*X (%s)\n", bits, (unsigned)op->addr, cmp_text(op->cmp),
                    (int)digits, (unsigned)op->value, skip);
        break;
    }
    case CHEAT_OP_ELSE:        fprintf(out, "      else\n"); break;
    case CHEAT_OP_ENDIF:       fprintf(out, "      endif\n"); break;
    case CHEAT_OP_MARKER:      fprintf(out, "      (master code line)\n"); break;
    default:                   fprintf(out, "      (not supported, skipped)\n"); break;
    }
}

void cheat_report_print(const CheatEngine* engine, FILE* out) {
    char code[5] = {(char)(engine->game_code & 0xFF), (char)((engine->game_code >> 8) & 0xFF),
                    (char)((engine->game_code >> 16) & 0xFF), (char)(engine->game_code >> 24), '\0'};
    for (int i = 0; i < 4; i++)
        if (code[i] < 0x20 || code[i] > 0x7E) code[i] = '?';
    fprintf(out, "Game code: %s, %u cheat%s\n", code, (unsigned)engine->cheat_count,
            engine->cheat_count == 1 ? "" : "s");

    for (uint32_t i = 0; i < engine->cheat_count; i++) {
        const CheatEntry* e = &engine->cheats[i];
        fprintf(out, "\n%u. %s [%s]\n", (unsigned)i + 1, e->name, e->enabled ? "on" : "off");
        if (e->broken) {
            fprintf(out, "   won't run: %s\n", e->error);
            continue;
        }
        fprintf(out, "   %s (%s), %u line%s, %u op%s", cheat_device_name(e->device),
                e->device_override ? "chosen" : "detected", (unsigned)e->line_count, e->line_count == 1 ? "" : "s",
                (unsigned)e->op_count, e->op_count == 1 ? "" : "s");
        if (e->markers) fprintf(out, ", %u master code line%s", (unsigned)e->markers, e->markers == 1 ? "" : "s");
        if (e->unsupported) fprintf(out, ", %u not supported", (unsigned)e->unsupported);
        fprintf(out, "\n");
        for (uint32_t k = 0; k < e->op_count; k++) print_op(&e->ops[k], out);
    }
}
