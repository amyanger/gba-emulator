# Development

Building from source, what is emulated, how the code is organized, and how it is tested.

## Building from source

### Dependencies

| Dependency | Version | Install |
|------------|---------|---------|
| SDL2 | 2.0+ | `brew install sdl2` (macOS) / `apt install libsdl2-dev` (Linux) |
| CMake | 3.21+ | `brew install cmake` (macOS) / `apt install cmake` (Linux) |

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

### Optional build flags

```bash
# Strip Hardware X-Ray Mode (no F2 overlay)
cmake .. -DENABLE_XRAY=OFF

# Strip rewind support (no Backspace, no LZ4)
cmake .. -DENABLE_REWIND=OFF
```

## What's emulated

- **ARM7TDMI CPU** — Full ARM (32-bit) and Thumb (16-bit) instruction set
  - All 16 condition codes, barrel shifter, multiply/multiply-long
  - 3-stage pipeline emulation with proper PC offset handling
  - 7 CPU modes with banked register switching
  - HLE BIOS for running without a BIOS dump
  - GamePak wait states (WAITCNT) and prefetch buffer timing
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
- **Timers** — 4 cascadable 16-bit timers with prescaler and IRQ generation, with timer IRQs raised on the cycle they overflow
- **Interrupts** — IE/IF/IME with write-1-to-clear semantics
- **Flash 64K / 128K Save** — Macronix and SST/Atmel/Panasonic chip IDs (Pokemon Emerald, Ruby, Sapphire, FireRed, LeafGreen)
- **Real-Time Clock** — S-3511A serial RTC over GPIO (0x080000C4/C6/C8) with persistent offset stored in the `.sav` trailer
- **Cartridge** — ROM loading (up to 32MB), auto save detection, file persistence next to the ROM
- **Emerald Companion**: for the original Pokemon Emerald (US/English) ROM only. Press F9 for a side panel with wild battle IVs, natures, catch odds per ball, move matchups both ways (your moves and the opponent's), your party's IVs/EVs/Hidden Power, and the current location's name and encounter table, in a dark dashboard layout.
- **Save States** — 10 numbered slots (0–9), versioned and ROM-hash guarded, written next to the ROM as `<rom>.ss<N>`
- **Cheats** — CodeBreaker, GameShark v1/v2 and Action Replay v3 (GameShark v3/SP) codes pasted as found online, encrypted or raw; the device is detected automatically and the save is backed up before cheats run
- **Fast-Forward** — Hold Tab or toggle with `` ` `` (skips audio, renders every Nth frame)
- **Rewind** — Hold Backspace to step back through the last 60 seconds (LZ4-compressed snapshots in RAM, capped at 256 MB, audio muted while rewinding)
- **Input** — Active-low KEYINPUT register with SDL2 keyboard mapping
- **Link Cable (local)** — Two-instance Multiplayer mode SIO over an AF_UNIX socket (`--link-master`/`--link-client`)
- **SDL2 Frontend** — Windowed or fullscreen rendering, configurable scale, audio-driven frame sync

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
- **CPU runs in scanline chunks** — 1,006 cycles up to the HBlank flag, then 226 more (1,232 per scanline, 228 scanlines per frame). A chunk also ends early at the next timer IRQ so the CPU sees it on time.
- **No dynamic allocation** — All subsystem memory is statically sized. The only heap allocation is ROM loading.
- **One file = one hardware component** — Each source file maps to a discrete piece of GBA hardware.
- **X-Ray is a passive observer** — It reads GBA state but never writes to it. Zero overhead when disabled.
- **The game layer is a passive observer too**: `src/game/` reads RAM and ROM arrays directly, never through the bus, and never writes to emulated memory.

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
    flash.c/h              Flash 64K/128K save (Macronix / SST / Atmel / Panasonic)
    gpio.c/h               Cartridge GPIO at 0x080000C4/C6/C8 (data/dir/control)
    rtc.c/h                S-3511A real-time clock state machine
    sram.c                 Battery-backed SRAM
    eeprom.c               EEPROM save (bit-serial, DMA-driven)
  cheat/
    cheat.c/h              Cheat list and per-frame runner (RAM direct, no bus timing)
    cheat_parse.c/h        Pulls codes out of pasted text
    cheat_decode.c/h       Device detection and decoding into device-neutral ops
    cheat_crypto.c/h       GameShark / Action Replay TEA and CodeBreaker ciphers
    cheat_file.c/h         `.cht` file reader and writer
    cheat_backup.c/h       Save backup before cheats run
    cheat_report.c/h       `--cheat-report` output
  savestate/
    savestate.c/h          Versioned save state serialization (ROM-hash guarded)
  game/
    game.c/h               Builds a snapshot of party, battle and encounter state
    game_mem.c/h           Read-only view of EWRAM, IWRAM and ROM
    emerald_profile.c/h    Every ROM and RAM address for US Emerald rev 0
    pokemon.c/h            Party Pokemon decryption, stats, IVs, nature, Hidden Power
    battle_info.c/h        Catch odds and move matchups for the current battle
    encounters.c/h         Wild encounter tables for the current map
    gen3_text.c/h          Gen 3 character set to ASCII
    overworld.c/h          Overworld X-Ray: trainers, sight lines and hidden items on screen
  ui/
    ui_canvas.c/h          Software canvas: rects, rounded shapes, alpha blending
    ui_font.c/h            Text measuring and drawing from the embedded Inter font
    ui_widgets.c/h         Pills, bars, tabs and other panel widgets
    ui_theme.h             Colors and spacing for the panel
    ui_stb.c/h             Static-arena allocation wrapper for stb_truetype
    font_inter.h           Embedded Inter subset (generated by tools/embed_font.sh)
    stb_truetype.h         Vendored stb_truetype (public domain)
  input/
    input.c/h              Keypad registers
  frontend/
    frontend.c/h           SDL2 window, rendering, input polling, audio
    game_panel.c/h         Emerald Companion pages (battle, party, route), drawn with src/ui
    panel_layout.c/h       Window, game and panel rectangles from display density
    overworld_draw.c/h     Overworld X-Ray painter (cones, labels, sparkles), drawn with src/ui
    prefs.c/h              prefs.ini for settings remembered between launches
    debug.c                Register dumps, instruction tracing (debug builds)
    xray/
      xray.h               X-Ray state struct, public API, notification hooks
      xray.c               SDL2 window: HiDPI, fit to screen, present
      xray_draw.c          Card layout, fit-to-screen maths, drawing helpers (no SDL)
      xray_font.h          Embedded 8x8 bitmap font used by the in-game overlays
      xray_ppu.c           "What you see": layer capture, thumbnails, layer table
      xray_tiles.c         "Tiles and colors": tile sheets and palettes
      xray_audio.c         "What you hear": audio capture, waveforms, FIFOs, retro channels
      xray_cpu.c           "Processor": registers, state, flags, pipeline
      xray_rewind.c        Rewind status line in the Processor card
      xray_activity.c      "Behind the scenes": timers, DMA, interrupts
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
| 5 | Flash 64K/128K save + S-3511A RTC | Done |
| 6 | Hardware X-Ray Mode | Done |
| 7 | Polish + accuracy (full playthrough) | Done — Pokemon Emerald playable end-to-end |

**Target milestone**: Full Pokemon Emerald playthrough from title screen to credits — **achieved**.

### Game compatibility

| Game | Status |
|------|--------|
| Pokemon Emerald (BPEE) | Fully playable |
| Pokemon FireRed (BPRE) | Boots through the Game Freak intro to the title screen. Save type auto-detected (Flash 128K). Full playthrough not yet verified. |

## Testing

### Unit tests

A minimal C unit test suite lives in `tests/` and runs on every CI build (Linux, macOS and Windows via GitHub Actions, plus an ASan/UBSan build):

```bash
cd build && cmake .. && make gba_tests && ctest --output-on-failure
```

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

### Golden-frame regression testing

The CI job `golden-frame` (in `.github/workflows/ci.yml`) re-runs the
emulator on pinned test ROMs ([jsmolka/gba-tests](https://github.com/jsmolka/gba-tests),
tonc demos, and the [mGBA test suite](https://github.com/mgba-emu/suite))
and diffs the per-frame hash output against `tests/golden/<rom>.hash`.
Any change that alters rendering output fails the workflow. The mGBA
suite goldens pin the current pass counts, so they also catch accuracy
regressions. The counts are listed in `tests/golden/README.md`.

**To add a new golden ROM:**

1. Place or fetch the ROM somewhere local (e.g. `tools/fetch_test_roms.sh /tmp/gba-test-roms`).
2. Bake the golden hash from a clean Release build:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target gba_emulator
   build/gba_emulator path/to/rom.gba \
       --headless --frames 240 --hash-out tests/golden/<name>.hash
   ```
3. Manually verify the ROM actually renders correctly — interactive mode
   is the simplest way (`./gba_emulator path/to/rom.gba`).
4. Add a new step in `.github/workflows/ci.yml`'s `golden-frame` job
   calling `tools/check_golden.sh build/gba_emulator
   /tmp/gba-test-roms/<rom>.gba tests/golden/<name>.hash`.
5. Commit the new `.hash` file and CI step together so they land in the
   same change.

### Pokemon Emerald smoke check (local only)

`tests/golden/emerald.hash` pins the first 300 frames of an Emerald boot
(Game Freak logo + intro movie). The ROM is commercial and never fetched
in CI, so the check runs locally and skips cleanly when the ROM is
absent:

```bash
tools/smoke_emerald.sh              # uses build/gba_emulator + roms/emerald.gba
```

Run it before and after changes to the CPU, PPU, DMA, or timers.

## References

- [GBATEK](https://problemkaputt.de/gbatek.htm) — Primary GBA hardware reference
- [GBATEK (Markdown)](https://mgba-emu.github.io/gbatek/) — Searchable GBATEK mirror
- [Copetti — GBA Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/) — High-level architecture overview
- [ARM7TDMI Decoding Guide](https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/) — Instruction set decoding walkthrough
- [awesome-gbadev](https://github.com/gbadev-org/awesome-gbadev) — Curated GBA development resources
- [mGBA](https://github.com/mgba-emu/mgba) — Reference emulator source
- [Tonc](https://www.coranac.com/tonc/text/hardware.htm) — GBA hardware programming tutorial
- [Inter](https://rsms.me/inter/) 4.1: Font for Emerald Companion, under the SIL Open Font License 1.1 (`fonts/Inter-LICENSE.txt`). `tools/embed_font.sh` regenerates `src/ui/font_inter.h` from it.
- [stb_truetype](https://github.com/nothings/stb): Glyph rasterizer for the panel font, public domain
