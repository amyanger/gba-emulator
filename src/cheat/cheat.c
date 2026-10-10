#include "cheat/cheat.h"
#include "cheat/cheat_decode.h"
#include "cheat/cheat_parse.h"
#include "input/input.h"
#include "memory/bus.h"

#include <stdio.h>
#include <string.h>

void cheat_init(CheatEngine* engine) { memset(engine, 0, sizeof(*engine)); }

void cheat_set_game_code(CheatEngine* engine, uint32_t game_code) { engine->game_code = game_code; }

/* Decodes e->lines with dev into e. Returns false (e->error set) if it fails. */
static bool decode_entry(CheatEntry* e, CheatDevice dev) {
    static CheatRawList raw;
    static CheatOp ops[CHEAT_MAX_OPS];
    CheatDecodeInfo info;
    uint32_t n = 0;
    memset(&raw, 0, sizeof(raw));
    memcpy(raw.lines, e->lines, e->line_count * sizeof(CheatRawLine));
    raw.count = e->line_count;
    if (!cheat_decode(&raw, dev, ops, CHEAT_MAX_OPS, &n, &info)) {
        snprintf(e->error, sizeof(e->error), "%s", info.fail);
        return false;
    }
    memcpy(e->ops, ops, n * sizeof(CheatOp));
    e->op_count = (uint8_t)n;
    e->markers = (uint8_t)info.markers;
    e->unsupported = (uint8_t)info.unsupported;
    e->device = dev;
    e->broken = false;
    e->error[0] = '\0';
    return true;
}

static void fill_entry(CheatEntry* e, const char* name, const CheatRawList* raw) {
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    memcpy(e->lines, raw->lines, raw->count * sizeof(CheatRawLine));
    e->line_count = (uint8_t)raw->count;
}

int32_t cheat_add_raw(CheatEngine* engine, const char* name, const CheatRawList* raw, CheatDevice dev,
                      bool override, bool enabled) {
    if (engine->cheat_count >= CHEAT_MAX_CHEATS || raw->count == 0) return -1;
    CheatEntry* e = &engine->cheats[engine->cheat_count];
    fill_entry(e, name, raw);
    e->enabled = enabled;
    e->device_override = override && dev != CHEAT_DEV_AUTO;
    e->confident = true;
    if (dev == CHEAT_DEV_AUTO) {
        CheatDetect d;
        cheat_detect(raw, engine->game_code, &d);
        dev = d.device;
        e->confident = d.confident;
    }
    e->device = dev == CHEAT_DEV_AUTO ? CHEAT_DEV_GSA : dev;
    /* Like a paste: an unsure guess is kept (and saved) but never run. */
    bool unsure = dev == CHEAT_DEV_AUTO || !e->confident;
    if (unsure || !decode_entry(e, dev)) {
        if (unsure) snprintf(e->error, sizeof(e->error), "Couldn't tell which device these codes are for");
        e->broken = true;
        e->op_count = 0;
    }
    return (int32_t)engine->cheat_count++;
}

int32_t cheat_add_text(CheatEngine* engine, const char* text, const char* name, CheatDevice dev,
                       char* err, size_t err_len) {
    static CheatRawList raw;
    if (engine->cheat_count >= CHEAT_MAX_CHEATS) {
        snprintf(err, err_len, "The cheat list is full (%d cheats)", CHEAT_MAX_CHEATS);
        return -1;
    }
    if (!cheat_parse_text(text, &raw, err, err_len)) return -1;

    bool forced = dev != CHEAT_DEV_AUTO;
    if (!forced) {
        CheatDetect d;
        cheat_detect(&raw, engine->game_code, &d);
        if (!d.confident) {
            snprintf(err, err_len, "Not sure which device these codes are for. Please pick one.");
            return -1;
        }
        dev = d.device;
    }

    char label[CHEAT_NAME_LEN];
    if (name && name[0]) snprintf(label, sizeof(label), "%s", name);
    else if (raw.name[0]) snprintf(label, sizeof(label), "%s", raw.name);
    else snprintf(label, sizeof(label), "Cheat %u", (unsigned)engine->cheat_count + 1);

    CheatEntry* e = &engine->cheats[engine->cheat_count];
    fill_entry(e, label, &raw);
    if (!decode_entry(e, dev)) {
        snprintf(err, err_len, "%s", e->error);
        memset(e, 0, sizeof(*e));
        return -1;
    }
    e->enabled = true;
    e->confident = true;
    e->device_override = forced;
    return (int32_t)engine->cheat_count++;
}

bool cheat_set_device(CheatEngine* engine, uint32_t index, CheatDevice dev) {
    if (index >= engine->cheat_count || dev >= CHEAT_DEV_COUNT) return false;
    static CheatEntry trial;
    trial = engine->cheats[index];
    bool was_broken = trial.broken;
    if (!decode_entry(&trial, dev)) return false;
    /* A repaired cheat starts off: turning it on must go through cheat_enable,
     * which backs the save up first. */
    if (was_broken) trial.enabled = false;
    trial.device_override = true;
    trial.confident = true;
    engine->cheats[index] = trial;
    return true;
}

void cheat_remove(CheatEngine* engine, uint32_t index) {
    if (index >= engine->cheat_count) return;
    memmove(&engine->cheats[index], &engine->cheats[index + 1],
            (engine->cheat_count - index - 1) * sizeof(CheatEntry));
    engine->cheat_count--;
}

void cheat_toggle(CheatEngine* engine, uint32_t index) {
    if (index >= engine->cheat_count) return;
    engine->cheats[index].enabled = !engine->cheats[index].enabled;
}

void cheat_clear_all(CheatEngine* engine) { engine->cheat_count = 0; }

bool cheat_any_enabled(const CheatEngine* engine) {
    for (uint32_t i = 0; i < engine->cheat_count; i++)
        if (engine->cheats[i].enabled && !engine->cheats[i].broken) return true;
    return false;
}

/* ---- Runtime ---- */

/* RAM goes straight to the arrays: a cheat must not cost CPU cycles or move
 * prefetch/open-bus state, so timing is the same as without cheats. */
static uint8_t* ram_ptr(Bus* bus, uint32_t addr, uint32_t width) {
    if (addr >= 0x02000000u && addr + width <= 0x02000000u + EWRAM_SIZE) return &bus->ewram[addr - 0x02000000u];
    if (addr >= 0x03000000u && addr + width <= 0x03000000u + IWRAM_SIZE) return &bus->iwram[addr - 0x03000000u];
    return NULL;
}

static uint32_t mem_read(Bus* bus, uint32_t addr, uint32_t width) {
    if (!cheat_addr_allowed(addr, width)) return 0;
    uint8_t* p = ram_ptr(bus, addr, width);
    if (p) {
        uint32_t v = 0;
        for (uint32_t i = 0; i < width; i++) v |= (uint32_t)p[i] << (8 * i);
        return v;
    }
    if (width == 1) return bus_read8(bus, addr);
    if (width == 2) return bus_read16(bus, addr);
    return bus_read32(bus, addr);
}

static void mem_write(Bus* bus, uint32_t addr, uint32_t width, uint32_t value) {
    if (!cheat_addr_allowed(addr, width)) return;
    uint8_t* p = ram_ptr(bus, addr, width);
    if (p) {
        for (uint32_t i = 0; i < width; i++) p[i] = (uint8_t)(value >> (8 * i));
        return;
    }
    if (width == 1) bus_write8(bus, addr, (uint8_t)value);
    else if (width == 2) bus_write16(bus, addr, (uint16_t)value);
    else bus_write32(bus, addr, value);
}

static int32_t sign_extend(uint32_t v, uint32_t width) {
    if (width == 1) return (int8_t)v;
    if (width == 2) return (int16_t)v;
    return (int32_t)v;
}

static bool cond_true(const CheatOp* op, Bus* bus) {
    if (op->cmp == CHEAT_CMP_NEVER) return false;
    if (op->cmp == CHEAT_CMP_KEYS) {
        uint16_t held = (uint16_t)(~bus->input->keyinput & 0x03FFu);   /* KEYINPUT is active low */
        return (held & op->value) == op->value;
    }
    uint32_t m = mem_read(bus, op->addr, op->width);
    switch (op->cmp) {
    case CHEAT_CMP_EQ:   return m == op->value;
    case CHEAT_CMP_NE:   return m != op->value;
    case CHEAT_CMP_LT_S: return sign_extend(m, op->width) < sign_extend(op->value, op->width);
    case CHEAT_CMP_GT_S: return sign_extend(m, op->width) > sign_extend(op->value, op->width);
    case CHEAT_CMP_LT_U: return m < op->value;
    case CHEAT_CMP_GT_U: return m > op->value;
    case CHEAT_CMP_LE_U: return m <= op->value;
    case CHEAT_CMP_GE_U: return m >= op->value;
    case CHEAT_CMP_AND:  return (m & op->value) != 0;
    default:             return false;
    }
}

/* From a block IF at i, the index of its ELSE (if want_else) or ENDIF,
 * honoring nested blocks; n if missing. */
static uint32_t find_block_end(const CheatEntry* e, uint32_t i, bool want_else) {
    uint32_t depth = 0;
    for (uint32_t k = i + 1; k < e->op_count; k++) {
        const CheatOp* op = &e->ops[k];
        if (op->kind == CHEAT_OP_IF && op->skip == CHEAT_SKIP_BLOCK) depth++;
        else if (op->kind == CHEAT_OP_ENDIF) {
            if (depth == 0) return k;
            depth--;
        } else if (op->kind == CHEAT_OP_ELSE && depth == 0 && want_else) {
            return k;
        }
    }
    return e->op_count;
}

static void run_entry(const CheatEntry* e, Bus* bus) {
    for (uint32_t i = 0; i < e->op_count; i++) {
        const CheatOp* op = &e->ops[i];
        switch (op->kind) {
        case CHEAT_OP_WRITE:
            if (op->flags & CHEAT_OPF_INDIRECT) mem_write(bus, mem_read(bus, op->addr, 4) + op->value2, op->width, op->value);
            else mem_write(bus, op->addr, op->width, op->value);
            break;
        case CHEAT_OP_ADD: mem_write(bus, op->addr, op->width, mem_read(bus, op->addr, op->width) + op->value); break;
        case CHEAT_OP_OR:  mem_write(bus, op->addr, op->width, mem_read(bus, op->addr, op->width) | op->value); break;
        case CHEAT_OP_AND: mem_write(bus, op->addr, op->width, mem_read(bus, op->addr, op->width) & op->value); break;
        case CHEAT_OP_SLIDE: {
            uint32_t a = op->addr, v = op->value;
            for (uint32_t n = 0; n < op->count; n++) {
                mem_write(bus, a, op->width, v);
                a += op->addr_step;
                v += (uint32_t)(int32_t)op->value_step;
            }
            break;
        }
        case CHEAT_OP_IF:
            if (cond_true(op, bus)) break;
            if (op->skip == CHEAT_SKIP_BLOCK) i = find_block_end(e, i, true);   /* lands on ELSE/ENDIF */
            else i += op->skip;
            break;
        case CHEAT_OP_ELSE:   /* reached from a true branch: skip the else part */
            i = find_block_end(e, i, false);
            break;
        default:   /* ENDIF, markers, unsupported */
            break;
        }
    }
}

void cheat_apply(CheatEngine* engine, Bus* bus) {
    for (uint32_t i = 0; i < engine->cheat_count; i++) {
        const CheatEntry* e = &engine->cheats[i];
        if (e->enabled && !e->broken) run_entry(e, bus);
    }
}
