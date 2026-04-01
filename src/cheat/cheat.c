#include "cheat.h"
#include "memory/bus.h"
#include <string.h>

void cheat_init(CheatEngine* engine) {
    memset(engine, 0, sizeof(CheatEngine));
}

int32_t cheat_add(CheatEngine* engine, const char* name,
                  CheatFormat format, const CheatCode* codes,
                  uint32_t code_count) {
    if (engine->cheat_count >= CHEAT_MAX_CHEATS) return -1;
    if (code_count == 0 || code_count > CHEAT_MAX_LINES) return -1;

    CheatEntry* entry = &engine->cheats[engine->cheat_count];
    strncpy(entry->name, name, CHEAT_NAME_LEN - 1);
    entry->name[CHEAT_NAME_LEN - 1] = '\0';
    entry->format = format;
    memcpy(entry->codes, codes, code_count * sizeof(CheatCode));
    entry->code_count = code_count;
    entry->enabled = true;

    return (int32_t)engine->cheat_count++;
}

void cheat_remove(CheatEngine* engine, uint32_t index) {
    if (index >= engine->cheat_count) return;

    // Shift remaining entries down
    for (uint32_t i = index; i < engine->cheat_count - 1; i++) {
        engine->cheats[i] = engine->cheats[i + 1];
    }
    engine->cheat_count--;
}

void cheat_toggle(CheatEngine* engine, uint32_t index) {
    if (index >= engine->cheat_count) return;
    engine->cheats[index].enabled = !engine->cheats[index].enabled;
}

void cheat_clear_all(CheatEngine* engine) {
    engine->cheat_count = 0;
}

// ---------------------------------------------------------------------------
// GameShark v1/v2 / Action Replay decoder
// ---------------------------------------------------------------------------
// Code format: TTAAAAAA VVVVVVVV
//   T  = type nibble (bits 31-28 of address word)
//   A  = target address (bits 27-0)
//   V  = value
//
// Supported types:
//   0 = 8-bit constant write
//   1 = 16-bit constant write
//   2 = 32-bit constant write
//   3 = 8-bit if-equal (conditional: execute next code only if match)
//   D = 16-bit if-equal (conditional)
//   6 = 16-bit slide code (repeated writes)

static void apply_gameshark(CheatEntry* entry, Bus* bus) {
    bool skip_next = false;

    for (uint32_t i = 0; i < entry->code_count; i++) {
        CheatCode* code = &entry->codes[i];
        uint8_t type = (uint8_t)(code->address >> 28);
        uint32_t addr = code->address & 0x0FFFFFFF;
        uint32_t val = code->value;

        if (skip_next) {
            skip_next = false;
            continue;
        }

        switch (type) {
        case 0x0:
            // 8-bit constant write
            bus_write8(bus, addr, (uint8_t)(val & 0xFF));
            break;

        case 0x1:
            // 16-bit constant write
            bus_write16(bus, addr, (uint16_t)(val & 0xFFFF));
            break;

        case 0x2:
            // 32-bit constant write
            bus_write32(bus, addr, val);
            break;

        case 0x3:
            // 8-bit if-equal: skip next code if memory != value
            if (bus_read8(bus, addr) != (uint8_t)(val & 0xFF)) {
                skip_next = true;
            }
            break;

        case 0x6: {
            // Slide code: repeated 16-bit writes
            // Code format: 6AAAAAAA 0000VVVV
            // Next code:   NNNN0000 DDDDEEEE
            //   N = repeat count, D = address increment, E = value increment
            if (i + 1 >= entry->code_count) break;

            CheatCode* slide = &entry->codes[i + 1];
            uint16_t write_val = (uint16_t)(val & 0xFFFF);
            uint16_t count = (uint16_t)(slide->address >> 16);
            uint16_t addr_inc = (uint16_t)(slide->value >> 16);
            uint16_t val_inc = (uint16_t)(slide->value & 0xFFFF);

            uint32_t cur_addr = addr;
            uint16_t cur_val = write_val;
            for (uint16_t n = 0; n < count; n++) {
                bus_write16(bus, cur_addr, cur_val);
                cur_addr += addr_inc;
                cur_val += val_inc;
            }
            i++; // Consume the slide parameter line
            break;
        }

        case 0xD:
            // 16-bit if-equal: skip next code if memory != value
            if (bus_read16(bus, addr) != (uint16_t)(val & 0xFFFF)) {
                skip_next = true;
            }
            break;

        default:
            LOG_WARN("[CHEAT] Unknown GameShark type: 0x%X (code %08X %08X)",
                     type, code->address, code->value);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// CodeBreaker decoder
// ---------------------------------------------------------------------------
// Code format: TTAAAAAA VVVVVVVV
//   T  = type nibble (bits 31-28 of address word)
//   A  = target address (bits 27-0)
//   V  = value
//
// Supported types:
//   0 = 32-bit constant write
//   1 = 16-bit constant write
//   2 = 8-bit constant write
//   3 = 32-bit if-equal (conditional)
//   7 = 16-bit if-greater-than (conditional: skip next if memory <= value)

static void apply_codebreaker(CheatEntry* entry, Bus* bus) {
    bool skip_next = false;

    for (uint32_t i = 0; i < entry->code_count; i++) {
        CheatCode* code = &entry->codes[i];
        uint8_t type = (uint8_t)(code->address >> 28);
        uint32_t addr = code->address & 0x0FFFFFFF;
        uint32_t val = code->value;

        if (skip_next) {
            skip_next = false;
            continue;
        }

        switch (type) {
        case 0x0:
            // 32-bit constant write
            bus_write32(bus, addr, val);
            break;

        case 0x1:
            // 16-bit constant write
            bus_write16(bus, addr, (uint16_t)(val & 0xFFFF));
            break;

        case 0x2:
            // 8-bit constant write
            bus_write8(bus, addr, (uint8_t)(val & 0xFF));
            break;

        case 0x3:
            // 32-bit if-equal: skip next code if memory != value
            if (bus_read32(bus, addr) != val) {
                skip_next = true;
            }
            break;

        case 0x7:
            // 16-bit if-greater-than: skip next if memory <= value
            if (bus_read16(bus, addr) <= (uint16_t)(val & 0xFFFF)) {
                skip_next = true;
            }
            break;

        default:
            LOG_WARN("[CHEAT] Unknown CodeBreaker type: 0x%X (code %08X %08X)",
                     type, code->address, code->value);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Public apply function
// ---------------------------------------------------------------------------

void cheat_apply(CheatEngine* engine, Bus* bus) {
    for (uint32_t i = 0; i < engine->cheat_count; i++) {
        CheatEntry* entry = &engine->cheats[i];
        if (!entry->enabled) continue;

        switch (entry->format) {
        case CHEAT_FORMAT_GAMESHARK:
            apply_gameshark(entry, bus);
            break;
        case CHEAT_FORMAT_CODEBREAKER:
            apply_codebreaker(entry, bus);
            break;
        }
    }
}
