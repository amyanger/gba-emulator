# GBA Emulator

A Game Boy Advance emulator written from scratch in C, with the goal of running **Pokemon Emerald** from boot to credits.

Built around an ARM7TDMI CPU interpreter, scanline-based PPU, and an SDL2 frontend. No external libraries beyond SDL2.

## Features

### Implemented
- **ARM7TDMI CPU** - Full ARM (32-bit) and Thumb (16-bit) instruction set decoding and execution
  - All 16 condition codes, barrel shifter with edge cases (RRX, zero-amount shifts)
  - Data processing, multiply/multiply-long, halfword transfers, block transfers
  - Branch/exchange, SWP, PSR operations, software interrupts
  - 3-stage pipeline emulation with proper PC offset handling
  - 7 CPU modes with banked register switching (USR, FIQ, SVC, ABT, IRQ, UND, SYS)
- **Memory Bus** - Full GBA memory map with proper mirroring and access rules
  - BIOS protection (read-only when PC is in BIOS region)
  - EWRAM (256KB), IWRAM (32KB) with address mirroring
  - VRAM 96KB mapped into 128KB space with correct mirror behavior
  - Palette RAM and VRAM 8-bit write duplication, OAM 8-bit write rejection
  - I/O register dispatch to hardware subsystems
- **Interrupt Controller** - IE/IF/IME with write-1-to-clear IF semantics
- **Timers** - 4 cascadable 16-bit timers with prescaler and IRQ generation
- **Input** - Active-low KEYINPUT register with SDL2 keyboard mapping
- **Flash 128K Save** - Macronix MX29L010 protocol (command sequences, sector erase, bank switching) for Pokemon Emerald compatibility
- **Cartridge** - ROM loading (up to 32MB), automatic save type detection, save file persistence
- **SDL2 Frontend** - Windowed rendering with configurable scale, keyboard input, audio device init

### In Progress
- **DMA Controller** - Register handling implemented, transfer execution in progress
- **PPU** - Timing and VBlank/HBlank signals working, scanline rendering being built out

### Planned
- **PPU Rendering** - Tiled backgrounds (modes 0-2), bitmap modes (3-5), sprite/OBJ layer, priority/blending/windowing
- **Audio** - Legacy GB channels (square, wave, noise), DirectSound FIFO A/B, DMA-driven playback
- **RTC** - Real-time clock via GPIO serial protocol

## Building

### Dependencies

| Dependency | Version | Install |
|------------|---------|---------|
| SDL2 | 2.0+ | `brew install sdl2` (macOS) / `apt install libsdl2-dev` (Linux) |
| CMake | 3.16+ | `brew install cmake` (macOS) / `apt install cmake` (Linux) |

No other external libraries are required.

### Compile

```bash
# Standard build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make

# Debug build (enables register dumps, instruction tracing)
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```

### Clean Rebuild

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
| `--bios <file>` | Path to GBA BIOS dump (optional, recommended) |
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

### Debug Controls (Debug builds only)

| Key | Action |
|-----|--------|
| F1 | Dump CPU registers to stderr |

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
```

### Design Principles

- **Bus as integration point** - The CPU never directly calls PPU, APU, or other subsystems. All communication happens through memory-mapped I/O reads and writes via the bus, mirroring real GBA hardware.
- **Scanline-based rendering** - The PPU renders one complete scanline at each HBlank. Not cycle-accurate per-pixel, but sufficient for Pokemon Emerald and most commercial games.
- **CPU runs in scanline chunks** - 960 cycles (HDraw) + 272 cycles (HBlank) = 1,232 cycles per scanline, 228 scanlines per frame (160 visible + 68 VBlank).
- **No dynamic allocation** - All subsystem memory is statically sized. The only heap allocation is ROM loading.
- **One file = one hardware component** - Each source file maps to a discrete piece of GBA hardware.

## Project Structure

```
src/
  main.c                  Entry point, CLI argument parsing, main loop
  gba.c/h                 Top-level system struct, per-frame orchestration
  cpu/
    arm7tdmi.c/h           CPU state, registers, mode switching, step loop
    arm_instr.c/h          ARM (32-bit) instruction decoder and executor
    thumb_instr.c/h        Thumb (16-bit) instruction decoder and executor
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
| 1 | CPU (ARM + Thumb) + Memory Bus | In progress |
| 2 | PPU basics + SDL2 frontend | Up next |
| 3 | Full PPU + sprites | Planned |
| 4 | Audio (timers + DMA + FIFO) | Planned |
| 5 | Flash 128K save + RTC | Planned |
| 6 | Polish + accuracy | Planned |

**Target milestone**: Full Pokemon Emerald playthrough from title screen to credits.

## Testing

### Test ROMs

Place test ROMs in the `roms/` directory (not tracked by git):

- [**jsmolka/gba-tests**](https://github.com/jsmolka/gba-tests) - ARM/Thumb instruction correctness
- [**armwrestler**](https://github.com/mic-/armwrestler-gba-fixed) - Visual ARM instruction test grid
- [**mgba test suite**](https://github.com/mgba-emu/suite) - Timer, DMA, PPU timing validation
- [**tonc demos**](https://www.coranac.com/tonc/text/) - Visual PPU mode verification

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

- [GBATEK](https://problemkaputt.de/gbatek.htm) - Primary GBA hardware reference
- [GBATEK (Markdown)](https://mgba-emu.github.io/gbatek/) - Searchable GBATEK mirror
- [Copetti - GBA Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/) - High-level architecture overview
- [ARM7TDMI Decoding Guide](https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/) - Instruction set decoding walkthrough
- [awesome-gbadev](https://github.com/gbadev-org/awesome-gbadev) - Curated GBA development resources
- [mGBA](https://github.com/mgba-emu/mgba) - Reference emulator source
- [Tonc](https://www.coranac.com/tonc/text/hardware.htm) - GBA hardware programming tutorial

## License

This project is open source. See [LICENSE](LICENSE) for details.

---

**Note**: You must supply your own GBA BIOS and ROM files. They are not included in this repository.
