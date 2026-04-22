# GBA Emulator

A Game Boy Advance emulator in C targeting Pokemon Emerald. ARM7TDMI CPU interpreter with scanline-based PPU, SDL2 frontend.

## Build & Run

```bash
# Build (from project root)
mkdir -p build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make

# Run
./gba_emulator <rom.gba> --bios <bios.bin> --scale 3

# Run without BIOS (HLE BIOS kicks in automatically)
./gba_emulator <rom.gba> --scale 3

# Run with cheat codes
./gba_emulator <rom.gba> --cheats path/to/cheats.cht

# Build without Hardware X-Ray Mode
mkdir -p build && cd build && cmake .. -DENABLE_XRAY=OFF && make

# Clean rebuild
rm -rf build && mkdir build && cd build && cmake .. && make
```

## Controls

| GBA Button | Keyboard |
|-----------|----------|
| A | Z |
| B | X |
| Start | Enter |
| Select | Right Shift |
| D-Pad | Arrow Keys |
| L | A |
| R | S |

### Emulator hotkeys
- **F1**: Register dump to stderr (DEBUG builds)
- **F2**: Toggle Hardware X-Ray Mode
- **F5 / F8**: Save / load state (current slot)
- **0–9**: Select save state slot (written next to ROM as `<rom>.ss<N>`)
- **Tab** (hold) / **`** (toggle): Fast-forward — skips audio, renders every Nth frame
- **F11**: Toggle fullscreen
- **Escape**: Quit

## Dependencies

- **SDL2**: `apt install libsdl2-dev` (Linux) / `brew install sdl2` (macOS)
- **CMake 3.16+**: `apt install cmake` (Linux) / `brew install cmake` (macOS)
- No other external libraries. SDL2 is the only dependency.

## Project Structure

```
src/
  main.c              Entry point, CLI argument parsing, main loop
  gba.h/c             Top-level system struct, frame orchestration
  cpu/                 ARM7TDMI processor
    arm7tdmi.h/c       CPU state, registers, mode switching, run loop
    arm_instr.h/c      ARM (32-bit) instruction decoder/executor
    thumb_instr.h/c    Thumb (16-bit) instruction decoder/executor
    bios_hle.h/c       High-Level Emulation of BIOS calls (no BIOS ROM required)
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
    cartridge.h/c      ROM loading, save type auto-detection, file persistence
    flash.h/c          Flash 64K/128K save (Macronix protocol — Pokemon Emerald)
    rtc.h/c            Real-time clock via GPIO pins (partially implemented)
    sram.c             Battery-backed SRAM
    eeprom.c           EEPROM (stub — not needed for Emerald)
  cheat/
    cheat.h/c          Cheat engine — GameShark/Action Replay + CodeBreaker
    cheat_file.h/c     Cheat file (.cht) parser and writer
  savestate/
    savestate.h/c      Save state serialization (versioned, magic-tagged, ROM-hash guarded)
  input/input.h/c      Keypad registers (active-low), KEYCNT for key IRQs
  frontend/
    frontend.h/c       SDL2 window, rendering, input polling, audio output
    debug.c            Register dump, instruction tracing (DEBUG builds only)
    xray/              Hardware X-Ray visualization overlay (toggled with F2)
      xray.h/c         X-Ray mode controller and main overlay
      xray_draw.h/c    Primitive drawing helpers for overlay
      xray_font.h      Built-in bitmap font for overlay text
      xray_cpu.c       CPU state viewer (registers, flags, PC, cycles)
      xray_ppu.c       PPU activity monitor (VCOUNT, DISPSTAT, scanline timing)
      xray_tiles.c     Tile/sprite preview tools
      xray_audio.c     Audio FIFO and channel visualization
      xray_activity.c  Activity heatmap for memory/register access
include/
  common.h             Fixed-width types, BIT/BITS macros, LOG macros, timing constants
bios/                  Place GBA BIOS ROM here (optional — HLE BIOS works without it)
roms/                  Place ROM files here for testing
saves/                 Save files are written here automatically
```

## Cheat File Format (.cht)

```ini
# Lines starting with # are comments
[GameShark]
Cheat Name
XXXXXXXX YYYYYYYY

[CodeBreaker]
Another Cheat
XXXXXXXX YYYY
```

Each block starts with `[GameShark]` or `[CodeBreaker]`, followed by the cheat name, then one or more code lines. Cheats are enabled by default.

## Git & GitHub

- **Claude must never appear as a contributor on GitHub.** This repo is public; it stays a human-authored project on the GitHub UI.
  - No `Co-Authored-By: Claude ...` trailers on commits.
  - No "Generated with Claude Code", "🤖", or other AI attribution in commit messages, PR titles, PR bodies, or issue comments.
  - No Claude/Anthropic identifiers in author/committer fields — always use the user's real git identity.
  - Applies to `gh pr create`, `gh issue comment`, `git commit`, amendments, rebases, and any other GitHub-visible surface.
- Only commit when explicitly asked. Write commit messages focused on the "why".

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

### Unit tests
A minimal unit test suite lives in `tests/` (`test_runner.c`, `test_bus.c`, `test_cpu.c`, `test_savestate.c`, shared `test_harness.h`). Wired into CMake as the `gba_tests` target.

```bash
cd build && cmake .. && make gba_tests && ctest --output-on-failure
# or run the binary directly:
./gba_tests
```

Test ROMs remain the primary validation path for full-system behavior.

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

### Debugging
- **F1**: Register dump to stderr (DEBUG builds only)
- **F2**: Toggle Hardware X-Ray Mode — real-time overlay showing CPU state, PPU activity, audio FIFOs, tile/sprite previews, and memory access heatmaps. Built with `ENABLE_XRAY=ON` (default).
- **Instruction trace**: Enable in `debug.c`, compare against mGBA trace output to find divergence points. This is the most valuable debugging tool.

## Monitor Tool — Streaming Error Detection

Use the Monitor tool to watch for errors in real-time during builds and testing:

```bash
# Stream build errors/warnings
cd build && cmake .. && make 2>&1 | grep --line-buffered -E "error:|warning:|undefined reference|fatal"

# Watch test ROM output for failures
./gba_emulator <rom.gba> 2>&1 | grep --line-buffered -E "ERROR|FAIL|WARN|assertion"

# Monitor debug trace divergence (compare against mGBA)
./gba_emulator <rom.gba> 2>&1 | grep --line-buffered -E "PC=|SIGABRT|segfault|Bus error"
```

- Set `persistent: true` for session-length watches (e.g., monitoring emulator output during test ROM runs)
- Use selective `grep` filters — instruction traces are extremely verbose
- Use `TaskStop` to cancel early

---

## Implementation Phases

Current target: **Pokemon Emerald from boot to credits.**

| Phase | Focus | Status |
|-------|-------|--------|
| 1 | CPU (ARM + Thumb instructions) + Memory Bus | **Complete** — full ARM/Thumb decoders, HLE BIOS, DMA, pipeline |
| 2 | PPU basics + SDL2 frontend (see pixels) | **Complete** — bitmap modes, tiled BG, scanline renderer |
| 3 | Full PPU + sprites (title screen renders) | **Complete** — sprites, affine, blending, windowing, mosaic |
| 4 | Audio (timers + DMA + FIFO chain) | **Complete** — FIFO playback, legacy channels, DAC filter, SDL audio |
| 5 | Flash 128K save + RTC | **Complete** — Flash 64K/128K + SRAM + S-3511A RTC over GPIO with persistent offset. EEPROM still stubbed. |
| 6 | Polish + accuracy (full playthrough) | In progress |

### Known gaps (not yet implemented)
- **EEPROM save protocol** — bit-serial protocol not implemented (not needed for Emerald)
- **WAITCNT register** — stubbed, no cartridge wait-state timing
- **CI pipeline** — no GitHub Actions / CI yet (local unit tests exist via `gba_tests`)

## Key References

- **GBATEK** (primary hardware ref): https://problemkaputt.de/gbatek.htm
- **GBATEK Markdown fork**: https://mgba-emu.github.io/gbatek/
- **Copetti GBA Architecture**: https://www.copetti.org/writings/consoles/game-boy-advance/
- **ARM7TDMI Decoding Guide**: https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/
- **awesome-gbadev resource list**: https://github.com/gbadev-org/awesome-gbadev
- **mGBA source** (reference emulator): https://github.com/mgba-emu/mgba
- **Tonc (GBA hardware tutorial)**: https://www.coranac.com/tonc/text/hardware.htm

## Agent Workflow (GBA-specific)

All agent rules from the parent CLAUDE.md apply. Additional GBA-specific guidance:

- **Before implementing**: Cross-reference the hardware behavior against GBATEK. Do not implement from memory alone.
- **Quality review checklist** (in addition to standard review):
  - Incorrect bit manipulation (off-by-one in shifts, wrong mask widths)
  - Missing edge cases in hardware emulation (overflow, underflow, wraparound)
  - Memory safety issues (buffer overruns on mirrored regions, null pointer dereference)
  - Incorrect register read/write behavior (write-only regs returning wrong values, read side effects)
  - Integer overflow in cycle counting and timer arithmetic
  - Broken subsystem wiring (bus dispatch routing to wrong handler)
- **Cross-cutting changes** (new I/O register, new DMA trigger): Use Plan mode first.
