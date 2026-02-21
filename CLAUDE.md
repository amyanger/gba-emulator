# GBA Emulator

A Game Boy Advance emulator in C targeting Pokemon Emerald. ARM7TDMI CPU interpreter with scanline-based PPU, SDL2 frontend.

## Build & Run

```bash
# Build (from project root)
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make

# Run
./gba_emulator <rom.gba> --bios <bios.bin> --scale 3

# Clean rebuild
rm -rf build && mkdir build && cd build && cmake .. && make
```

## Dependencies

- **SDL2**: `brew install sdl2`
- **CMake 3.16+**: `brew install cmake`
- No other external libraries. SDL2 is the only dependency.

## Project Structure

```
src/
  main.c              Entry point, CLI, main loop
  gba.h/c             Top-level system struct, frame orchestration
  cpu/                 ARM7TDMI processor
    arm7tdmi.h/c       CPU state, registers, mode switching, run loop
    arm_instr.h/c      ARM (32-bit) instruction decoder/executor
    thumb_instr.h/c    Thumb (16-bit) instruction decoder/executor
  memory/
    bus.h/c            Memory bus — ALL subsystem communication goes through here
    dma.h/c            4-channel DMA controller
    io_regs.h          I/O register address constants
  ppu/                 Picture Processing Unit (graphics)
    ppu.h/c            Scanline renderer, timing, VBlank/HBlank
    background.c       Tiled BG rendering (modes 0-2)
    bitmap.c           Bitmap modes 3-5
    sprites.c          OAM sprite rendering
    effects.c          Blending, windowing, mosaic
    affine.c           Rotation/scaling math
  apu/                 Audio Processing Unit
    apu.h/c            Mixer, FIFO management, sample buffer
    channel.c          Legacy GB sound channels (square, wave, noise)
    fifo.c             DirectSound FIFO A/B
  timer/timer.h/c      4 cascadable 16-bit timers
  interrupt/interrupt.h/c  IRQ controller (IE/IF/IME)
  cartridge/
    cartridge.h/c      ROM loading, save detection, file persistence
    flash.h/c          Flash 128K save (Macronix protocol — Pokemon Emerald)
    rtc.h/c            Real-time clock via GPIO pins
    sram.c             Battery-backed SRAM
    eeprom.c           EEPROM (stub, not needed for Emerald)
  input/input.h/c      Keypad registers (active-low)
  frontend/
    frontend.h/c       SDL2 window, rendering, input polling, audio
    debug.c            Register dump, instruction tracing (DEBUG builds only)
include/
  common.h             Fixed-width types, BIT/BITS macros, LOG macros, timing constants
tests/                 Unit tests and test ROM runner scripts
```

## Architecture Rules

- **Bus is the single integration point.** The CPU never calls PPU/APU directly. All interaction happens through memory-mapped I/O reads/writes via `bus.c`. This mirrors the real hardware.
- **`gba.c` orchestrates timing.** It tells each subsystem how many cycles to advance. The bus handles data routing only.
- **Scanline-based rendering.** The PPU renders one scanline at HBlank. Not cycle-accurate per-pixel. This is sufficient for Pokemon Emerald.
- **CPU runs in scanline chunks.** 960 cycles (HDraw) then 272 cycles (HBlank) = 1232 cycles per scanline.
- **No dynamic allocation** except for ROM loading in `cartridge_load()`. All subsystem memory is statically sized.
- **One file = one hardware component.** Do not merge unrelated functionality into a single file.

## Code Style

- **C11 standard.** Enforced by CMake (`CMAKE_C_STANDARD 11`).
- **4-space indentation**, no tabs. See `.clang-format`.
- **100-column line limit.**
- **Pointer alignment left**: `uint8_t* ptr` not `uint8_t *ptr`.
- **Fixed-width types everywhere**: `uint8_t`, `uint16_t`, `uint32_t`, `int8_t`, `int16_t`, `int32_t`. Never use `int` or `unsigned` for hardware state.
- **Use `common.h` macros**:
  - `BIT(val, n)` — extract single bit
  - `BITS(val, hi, lo)` — extract bit range
  - `SET_BIT(val, n)` / `CLR_BIT(val, n)`
  - `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`
- **Include guards**: `#ifndef FILENAME_H` / `#define FILENAME_H` / `#endif // FILENAME_H`
- **Forward declarations** over circular includes. See `bus.h` for the pattern: `typedef struct PPU PPU;`
- **Struct naming**: `typedef struct { ... } TypeName;` for opaque types. `struct Name { ... }; typedef struct Name Name;` when forward declaration is needed.
- **Function naming**: `module_action()` pattern. Examples: `bus_read32()`, `cpu_step()`, `ppu_render_scanline()`, `flash_write()`.
- **Constants**: `#define` with UPPER_SNAKE_CASE for hardware constants. Enums for related groups.
- **Compiler warnings are errors-in-spirit**: Code must compile cleanly with `-Wall -Wextra -Wpedantic`.

## Critical Hardware Details

These are non-obvious behaviors that MUST be correct. Verify against GBATEK when implementing.

- **KEYINPUT is active-LOW**: bit = 0 means pressed, bit = 1 means released. Initialize to `0x03FF`.
- **Writing 1 to IF clears that bit** (acknowledge interrupt). Opposite of most registers.
- **PC is ahead by 2 instructions** due to the 3-stage pipeline: ARM mode reads from `PC-8`, Thumb from `PC-4`.
- **8-bit writes to Palette RAM and VRAM** write both bytes of the halfword (duplicate the byte).
- **8-bit writes to OAM are ignored.**
- **VRAM mirroring**: 96KB VRAM maps into 128KB space. Addresses 0x06010000-0x06017FFF mirror back to 0x06008000-0x0600FFFF.
- **Flash save uses command sequences**: specific bytes must be written to specific addresses in order (0x5555=0xAA, 0x2AAA=0x55, then command). Pokemon Emerald uses Macronix MX29L010 (manufacturer=0xC2, device=0x09).
- **Timer cascade**: when timer N overflows and timer N+1 has cascade enabled, N+1 increments by 1 regardless of its prescaler.
- **DMA halts the CPU** during transfer. DMA channel 0 has highest priority.
- **FIFO audio chain**: Timer overflow -> pop FIFO sample -> if FIFO low, trigger DMA refill. This must work for Pokemon Emerald music.

## Testing

### Build and run tests
```bash
cd build && make gba_tests && ./gba_tests
```

### Test ROMs (place in `roms/` directory)
- **jsmolka/gba-tests**: ARM/Thumb instruction correctness. Screen shows pass/fail per group.
- **armwrestler**: Visual grid of instruction test results.
- **mgba test suite**: Timer, DMA, PPU timing validation.
- **tonc demos**: Visual PPU mode validation — compare against reference screenshots.

### Pokemon Emerald milestone checklist
1. BIOS intro plays (or skips cleanly if no BIOS)
2. Title screen renders with correct colors and animation
3. "New Game" -> Professor intro renders and accepts input
4. Overworld loads, player visible, can walk
5. Music plays correctly
6. Wild Pokemon battle renders and animates
7. Save -> close -> reload -> continue works
8. Play 30+ minutes without crash

### Debugging (DEBUG builds only)
- **F1**: Register dump to stderr
- **Instruction trace**: Enable in `debug.c`, compare against mGBA trace output to find divergence points. This is the most valuable debugging tool.

## Implementation Phases

Current target: **Pokemon Emerald from boot to credits.**

| Phase | Focus | Status |
|-------|-------|--------|
| 1 | CPU (ARM + Thumb instructions) + Memory Bus | In progress |
| 2 | PPU basics + SDL2 frontend (see pixels) | Not started |
| 3 | Full PPU + sprites (title screen renders) | Not started |
| 4 | Audio (timers + DMA + FIFO chain) | Not started |
| 5 | Flash 128K save + RTC | Not started |
| 6 | Polish + accuracy (full playthrough) | Not started |

## Key References

- **GBATEK** (primary hardware ref): https://problemkaputt.de/gbatek.htm
- **GBATEK Markdown fork**: https://mgba-emu.github.io/gbatek/
- **Copetti GBA Architecture**: https://www.copetti.org/writings/consoles/game-boy-advance/
- **ARM7TDMI Decoding Guide**: https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/
- **awesome-gbadev resource list**: https://github.com/gbadev-org/awesome-gbadev
- **mGBA source** (reference emulator): https://github.com/mgba-emu/mgba
- **Tonc (GBA hardware tutorial)**: https://www.coranac.com/tonc/text/hardware.htm

## Git Commit Rules

**CRITICAL: Never include any AI attribution in commits.** This applies to every commit for the entire lifetime of this project:
- Do NOT add `Co-Authored-By` lines mentioning Claude, Anthropic, or any AI
- Do NOT mention Claude, AI, or any AI tool in commit messages
- Do NOT add AI-related credits, attributions, or signatures of any kind
- Commit messages should read as if written by the developer — because they are

## Agent Workflow

IMPORTANT: Always use specialized agents when working on this project. Do not skip agent steps.

### Before implementing anything new
Use the **senior-architect** agent to analyze the GBA hardware behavior and design the approach. Cross-reference against GBATEK documentation. Do not write implementation code without understanding the hardware specification first.

### For all implementation work
Use the **precision-implementer** agent. It must follow the code style, architecture rules, and naming conventions defined in this file. Every implementation must include the correct bit-level hardware behavior per GBATEK.

### After every implementation
IMPORTANT: Always run the **quality-reviewer** agent after writing or modifying code. This is mandatory, not optional. The reviewer must check for:
- Incorrect bit manipulation (off-by-one in shifts, wrong mask widths)
- Missing edge cases in hardware emulation (overflow, underflow, wraparound)
- Memory safety issues (buffer overruns on mirrored regions, null pointer dereference)
- Incorrect register read/write behavior (write-only registers returning wrong values, read side effects)
- Integer overflow in cycle counting and timer arithmetic
- Broken subsystem wiring (bus dispatch routing to wrong handler)

### For codebase exploration and research
Use the **Explore** agent to search files and understand existing code without filling the main context window. Use this before making changes to unfamiliar modules.

### For complex multi-step tasks
Use the **general-purpose** agent when a task spans multiple modules or requires both research and implementation.

### For planning multi-file changes
Use **Plan** mode to design the approach before touching code. This is especially important for cross-cutting changes (e.g., adding a new I/O register that touches bus.c, the owning subsystem, and possibly DMA/interrupts).

### Workflow summary for every task
1. **Explore** the relevant code and hardware docs
2. **Architect** the approach (senior-architect agent)
3. **Implement** the code (precision-implementer agent)
4. **Review** the implementation (quality-reviewer agent)
5. **Build and test** — must compile with zero warnings, pass relevant test ROMs
