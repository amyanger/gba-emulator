# GBA Emulator

A Game Boy Advance emulator written from scratch in C, featuring **Hardware X-Ray Mode** — a real-time visualization of what the GBA hardware is actually doing while a game runs.

No other GBA emulator offers this. Every emulator is a black box. This one lets you see inside.

Built around an ARM7TDMI CPU interpreter, scanline-based PPU, and an SDL2 frontend. No external libraries beyond SDL2.

## Hardware X-Ray Mode

Press **F2** during gameplay to open a second window showing live hardware internals:

- **PPU Layer Decomposition** — Each background layer and sprites rendered separately, plus a color-coded overlay showing which layer produced every pixel on screen
- **Tile & Palette Inspector** — VRAM tile grids for all 6 charblocks and full BG/OBJ palette color displays
- **CPU State** — Live registers (R0-R15), CPSR flags (N/Z/C/V/I/F/T), CPU mode, pipeline state, and cycles-per-second counter
- **Audio Monitor** — Master output waveforms (L/R oscilloscope), FIFO A/B fill meters, and legacy channel status (duty, frequency, volume, LFSR)
- **DMA / Timer / IRQ Activity** — All 4 timers and DMA channels with live counters, plus named IRQ flags with red flash indicators on every event

X-Ray Mode adds zero overhead when disabled. It's compile-time gated (`ENABLE_XRAY`, default ON) and runtime gated (null-pointer checks on every hook). The game runs identically whether X-Ray is open or closed.

### Build without X-Ray (if you want a minimal binary)

```bash
cmake .. -DENABLE_XRAY=OFF
```

## Features

- **ARM7TDMI CPU** — Full ARM (32-bit) and Thumb (16-bit) instruction set
  - All 16 condition codes, barrel shifter, multiply/multiply-long
  - 3-stage pipeline emulation with proper PC offset handling
  - 7 CPU modes with banked register switching
  - HLE BIOS for running without a BIOS dump
- **PPU (Graphics)** — Scanline-based renderer
  - Tiled backgrounds: Mode 0 (4 regular), Mode 1 (2 regular + 1 affine), Mode 2 (2 affine)
  - Bitmap modes: Mode 3 (16-bit), Mode 4 (8-bit palettized), Mode 5 (16-bit small)
  - OAM sprites with priority, flipping, and affine transforms
  - Alpha blending, brightness fade, priority-based layer compositing
- **APU (Audio)** — Full audio pipeline
  - Legacy GB channels: 2 square (with sweep), wave table, noise (LFSR)
  - DirectSound FIFO A/B with timer-driven DMA refill chain
  - 32768 Hz stereo output via SDL2
- **DMA Controller** — 4-channel with immediate, VBlank, HBlank, and FIFO timing modes
- **Timers** — 4 cascadable 16-bit timers with prescaler and IRQ generation
- **Interrupts** — IE/IF/IME with write-1-to-clear semantics
- **Flash 128K Save** — Macronix MX29L010 protocol (Pokemon Emerald compatible)
- **Cartridge** — ROM loading (up to 32MB), auto save detection, file persistence
- **Input** — Active-low KEYINPUT register with SDL2 keyboard mapping
- **SDL2 Frontend** — Windowed rendering, configurable scale, audio-driven frame sync

## Building

### Dependencies

| Dependency | Version | Install |
|------------|---------|---------|
| SDL2 | 2.0+ | `brew install sdl2` (macOS) / `apt install libsdl2-dev` (Linux) |
| CMake | 3.16+ | `brew install cmake` (macOS) / `apt install cmake` (Linux) |

No other external libraries are required.

### Compile

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### Debug build

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```

### Clean rebuild

```bash
rm -rf build && mkdir build && cd build && cmake .. && make
```

## Usage

```bash
./build/gba_emulator <rom_file> [options]
```

### Options

| Flag | Description |
|------|-------------|
| `--bios <file>` | Path to GBA BIOS dump (optional, HLE fallback available) |
| `--scale <n>` | Window scale multiplier (default: 3) |

### Example

```bash
./build/gba_emulator roms/emerald.gba --bios bios/gba_bios.bin --scale 3
```

### Controls

| Key | GBA Button |
|-----|------------|
| Z | A |
| X | B |
| Enter | Start |
| Right Shift | Select |
| Arrow Keys | D-Pad |
| A | L Trigger |
| S | R Trigger |

| Key | Emulator Function |
|-----|-------------------|
| F1 | Dump CPU registers to stderr (debug builds) |
| F2 | Toggle Hardware X-Ray Mode |
| Escape | Quit |

## Architecture

```
+-----------+     +-------------------+     +-----------+
|           |     |                   |     |           |
|  ARM7TDMI |<--->|    Memory Bus     |<--->|    PPU    |
|    CPU    |     |     (bus.c)       |     | (scanline)|
|           |     |                   |     |           |
+-----------+     +---+---+---+---+---+     +-----------+
                  |   |   |   |   |
              +---+ +-+ +-+ +-+ +-+---+
              |     |   |   |   |     |
           +--+--+  | +-+-+ | +-+--+  |
           | DMA |  | |TMR| | |IRQ |  |
           +-----+  | +---+ | +----+  |
                     |       |         |
                  +--+--+ +--+--+ +---+---+
                  | APU | | I/O | |  Cart  |
                  +-----+ +-----+ +-------+
                                        |
                                  +-----+-----+
                                  |  X-Ray    |  (passive observer,
                                  |  Mode     |   reads all state)
                                  +-----------+
```

### Design Principles

- **Bus as integration point** — The CPU never directly calls PPU, APU, or other subsystems. All communication happens through memory-mapped I/O reads and writes via the bus, mirroring real GBA hardware.
- **Scanline-based rendering** — The PPU renders one complete scanline at each HBlank. Not cycle-accurate per-pixel, but sufficient for Pokemon Emerald and most commercial games.
- **CPU runs in scanline chunks** — 960 cycles (HDraw) + 272 cycles (HBlank) = 1,232 cycles per scanline, 228 scanlines per frame.
- **No dynamic allocation** — All subsystem memory is statically sized. The only heap allocation is ROM loading.
- **One file = one hardware component** — Each source file maps to a discrete piece of GBA hardware.
- **X-Ray is a passive observer** — It reads GBA state but never writes to it. Zero overhead when disabled.

## Project Structure

```
src/
  main.c                  Entry point, CLI argument parsing, main loop
  gba.c/h                 Top-level system struct, per-frame orchestration
  cpu/
    arm7tdmi.c/h           CPU state, registers, mode switching, step loop
    arm_instr.c/h          ARM (32-bit) instruction decoder and executor
    thumb_instr.c/h        Thumb (16-bit) instruction decoder and executor
    bios_hle.c             High-level BIOS emulation (SWI handlers)
  memory/
    bus.c/h                Memory bus, address decoding, I/O register dispatch
    dma.c/h                4-channel DMA controller
    io_regs.h              I/O register address constants
  ppu/
    ppu.c/h                Scanline renderer, timing, VBlank/HBlank
    background.c           Tiled background rendering (modes 0-2)
    bitmap.c               Bitmap mode rendering (modes 3-5)
    sprites.c              OAM sprite rendering
    effects.c              Alpha blending, windowing, mosaic
    affine.c               Rotation/scaling transform math
  apu/
    apu.c/h                Audio mixer, FIFO management, sample buffer
    channel.c              Legacy GB sound channels (square, wave, noise)
    fifo.c                 DirectSound FIFO A/B
  timer/
    timer.c/h              4 cascadable 16-bit timers
  interrupt/
    interrupt.c/h          IRQ controller (IE/IF/IME)
  cartridge/
    cartridge.c/h          ROM loading, save type detection, file persistence
    flash.c/h              Flash 128K save (Macronix protocol)
    rtc.c/h                Real-time clock via GPIO
    sram.c                 Battery-backed SRAM
    eeprom.c               EEPROM save (stub)
  input/
    input.c/h              Keypad registers
  frontend/
    frontend.c/h           SDL2 window, rendering, input polling, audio
    debug.c                Register dumps, instruction tracing (debug builds)
    xray/
      xray.h               X-Ray state struct, public API, notification hooks
      xray.c               SDL2 window lifecycle, panel layout, render dispatch
      xray_draw.h/c        Drawing primitives (text, rect, line, blit, bars)
      xray_font.h          Embedded 8x8 bitmap font (95 glyphs, no dependencies)
      xray_cpu.c           CPU register/flag/mode panel
      xray_ppu.c           PPU layer decomposition and overlay panel
      xray_tiles.c         Tile grid and palette inspector panel
      xray_audio.c         Audio waveform and FIFO monitor panel
      xray_activity.c      DMA/Timer/IRQ activity panel with flash indicators
include/
  common.h                 Fixed-width types, bit manipulation macros, logging
```

## Technical Details

### Memory Map

| Address Range | Size | Region |
|---------------|------|--------|
| `0x00000000 - 0x00003FFF` | 16 KB | BIOS (protected) |
| `0x02000000 - 0x0203FFFF` | 256 KB | EWRAM |
| `0x03000000 - 0x03007FFF` | 32 KB | IWRAM |
| `0x04000000 - 0x040003FE` | 1 KB | I/O Registers |
| `0x05000000 - 0x050003FF` | 1 KB | Palette RAM |
| `0x06000000 - 0x06017FFF` | 96 KB | VRAM |
| `0x07000000 - 0x070003FF` | 1 KB | OAM |
| `0x08000000 - 0x09FFFFFF` | 32 MB | Game ROM |
| `0x0E000000 - 0x0E00FFFF` | 64/128 KB | Save (SRAM/Flash) |

### CPU Pipeline

The ARM7TDMI uses a 3-stage pipeline (fetch-decode-execute). The PC is always 2 instructions ahead of the currently executing instruction:
- **ARM mode**: executing instruction was fetched from `PC - 8`
- **Thumb mode**: executing instruction was fetched from `PC - 4`

### Frame Timing

| Event | Cycles | Scanlines |
|-------|--------|-----------|
| HDraw | 960 | - |
| HBlank | 272 | - |
| Full scanline | 1,232 | 1 |
| Visible frame | - | 160 |
| VBlank | - | 68 |
| Full frame | 280,896 | 228 |
| Frame rate | 16.78 MHz / 280,896 | ~59.73 FPS |

## Roadmap

| Phase | Focus | Status |
|-------|-------|--------|
| 1 | CPU (ARM + Thumb) + Memory Bus | Done |
| 2 | PPU basics + SDL2 frontend | Done |
| 3 | Full PPU + sprites + effects | Done |
| 4 | Audio (timers + DMA + FIFO chain) | Done |
| 5 | Flash 128K save + RTC | Done |
| 6 | Hardware X-Ray Mode | Done |
| 7 | Polish + accuracy (full playthrough) | In progress |

**Target milestone**: Full Pokemon Emerald playthrough from title screen to credits.

## Testing

### Test ROMs

Place test ROMs in the `roms/` directory (not tracked by git):

- [**jsmolka/gba-tests**](https://github.com/jsmolka/gba-tests) — ARM/Thumb instruction correctness
- [**armwrestler**](https://github.com/mic-/armwrestler-gba-fixed) — Visual ARM instruction test grid
- [**mgba test suite**](https://github.com/mgba-emu/suite) — Timer, DMA, PPU timing validation
- [**tonc demos**](https://www.coranac.com/tonc/text/) — Visual PPU mode verification

### Pokemon Emerald Milestones

1. BIOS intro plays (or skips cleanly)
2. Title screen renders with correct colors
3. "New Game" -> Professor intro works
4. Overworld loads, player can walk
5. Music plays correctly
6. Wild battle renders and animates
7. Save/load cycle works
8. 30+ minutes without crash

## References

- [GBATEK](https://problemkaputt.de/gbatek.htm) — Primary GBA hardware reference
- [GBATEK (Markdown)](https://mgba-emu.github.io/gbatek/) — Searchable GBATEK mirror
- [Copetti — GBA Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/) — High-level architecture overview
- [ARM7TDMI Decoding Guide](https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/) — Instruction set decoding walkthrough
- [awesome-gbadev](https://github.com/gbadev-org/awesome-gbadev) — Curated GBA development resources
- [mGBA](https://github.com/mgba-emu/mgba) — Reference emulator source
- [Tonc](https://www.coranac.com/tonc/text/hardware.htm) — GBA hardware programming tutorial

## License

This project is open source. See [LICENSE](LICENSE) for details.

---

**Note**: You must supply your own GBA BIOS and ROM files. They are not included in this repository.
