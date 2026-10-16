# Development

Building from source, what is emulated, how the code is organized, how it is tested, and the scripts in `tools/`.

## Building from source

### Dependencies

| Dependency | Version | Install |
|------------|---------|---------|
| SDL2 | 2.0+ | `brew install sdl2` (macOS) / `apt install libsdl2-dev` (Linux) |
| CMake | 3.21+ | `brew install cmake` (macOS) / `apt install cmake` (Linux) |
| Rust (cargo) | 1.77+ | [rustup.rs](https://rustup.rs) (any OS) or `brew install rust` (macOS) |

The HeartGold/SoulSilver parsers in `rust/hgss` are Rust. They use no crates, so the build downloads nothing. No other external libraries are required.

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

# Treat compiler warnings as errors (CI does this)
cmake .. -DENABLE_WERROR=ON

# AddressSanitizer + UBSan build
cmake .. -DENABLE_SANITIZERS=ON

# Build a self-contained macOS .app bundle (used for releases)
cmake .. -DMACOS_APP_BUNDLE=ON
```

## What's emulated

- **ARM7TDMI CPU**: Full ARM (32-bit) and Thumb (16-bit) instruction set
  - All 16 condition codes, barrel shifter, multiply/multiply-long
  - 3-stage pipeline emulation with proper PC offset handling
  - 7 CPU modes with banked register switching
  - HLE BIOS for running without a BIOS dump
  - GamePak wait states (WAITCNT) and prefetch buffer timing
- **PPU (Graphics)**: Scanline-based renderer
  - Tiled backgrounds: Mode 0 (4 regular), Mode 1 (2 regular + 1 affine), Mode 2 (2 affine)
  - Bitmap modes: Mode 3 (16-bit), Mode 4 (8-bit palettized), Mode 5 (16-bit small)
  - OAM sprites with priority, flipping, and affine transforms
  - Alpha blending, brightness fade, priority-based layer compositing
- **APU (Audio)**: Full audio pipeline
  - Legacy GB channels: 2 square (with sweep), wave table, noise (LFSR)
  - DirectSound FIFO A/B with timer-driven DMA refill chain
  - 32768 Hz stereo output via SDL2
- **DMA Controller**: 4-channel with immediate, VBlank, HBlank, and FIFO timing modes
- **Timers**: 4 cascadable 16-bit timers with prescaler and IRQ generation, with timer IRQs raised on the cycle they overflow
- **Interrupts**: IE/IF/IME with write-1-to-clear semantics
- **Flash 64K / 128K Save**: Macronix and SST/Atmel/Panasonic chip IDs (Pokemon Emerald, Ruby, Sapphire, FireRed, LeafGreen)
- **Real-Time Clock**: S-3511A serial RTC over GPIO (0x080000C4/C6/C8) with persistent offset stored in the `.sav` trailer
- **Cartridge**: ROM loading (up to 32MB), auto save detection, file persistence next to the ROM
- **Pokémon Companion**: for the original Pokemon Emerald, FireRed (1.0) or LeafGreen (1.1) US/English ROMs only. Press F9 for a side panel with Battle (including damage predictions), Party, Route, Nuzlocke, Search and Run pages, plus Berries, Hunt and the Mirage Island forecast on Emerald. On Emerald, F4 adds the Overworld X-Ray, and F1 turns on Following Pokemon using the player's own HeartGold or SoulSilver ROM. See [Pokémon Companion](pokemon-companion.md).
- **Save States**: 10 numbered slots (0–9), versioned and ROM-hash guarded, written next to the ROM as `<rom>.ss<N>`
- **Cheats**: CodeBreaker, GameShark v1/v2 and Action Replay v3 (GameShark v3/SP) codes pasted as found online, encrypted or raw; the device is detected automatically and the save is backed up before cheats run
- **Fast-Forward**: Hold Tab or toggle with `` ` `` at 2x, 3x, 4x or Max (skips audio, renders every Nth frame)
- **Rewind**: Hold Backspace to step back through the last 60 seconds (LZ4-compressed snapshots in RAM, capped at 256 MB, audio muted while rewinding)
- **Input**: Active-low KEYINPUT register, keyboard and game controller bindings that can be changed in Settings, and turbo A/B
- **Link Cable (local)**: Two-instance Multiplayer mode SIO over an AF_UNIX socket (`--link-master`/`--link-client`)
- **SDL2 Frontend**: windowed or fullscreen rendering, configurable scale, audio-driven frame sync, a game library at launch, a Settings window, screen filters and LCD color correction
- **HQ music**: re-mixes music from games using the MP2K sound driver at 32768 Hz (see [HQ music](hq-music.md))

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

- **Bus as integration point**: The CPU never directly calls PPU, APU, or other subsystems. All communication happens through memory-mapped I/O reads and writes via the bus, mirroring real GBA hardware.
- **Scanline-based rendering**: The PPU renders one complete scanline at each HBlank. Not cycle-accurate per-pixel, but sufficient for Pokemon Emerald and most commercial games.
- **CPU runs in scanline chunks**: 1,006 cycles up to the HBlank flag, then 226 more (1,232 per scanline, 228 scanlines per frame). A chunk also ends early at the next timer IRQ so the CPU sees it on time.
- **No dynamic allocation**: All subsystem memory is statically sized. The only heap allocation is ROM loading.
- **One file = one hardware component**: Each source file maps to a discrete piece of GBA hardware.
- **X-Ray is a passive observer**: It reads GBA state but never writes to it. Zero overhead when disabled.
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
    cpu_alu.h              Shared ALU and flag helpers
    bios_hle.c/h           High-level BIOS emulation (SWI handlers)
  memory/
    bus.c/h                Memory bus, address decoding, I/O register dispatch
    dma.c/h                4-channel DMA controller
    io_regs.h              I/O register address constants
  ppu/
    ppu.c/h                Scanline renderer, timing, VBlank/HBlank
    background.c           Tiled and affine background rendering (modes 0-2)
    bitmap.c               Bitmap mode rendering (modes 3-5)
    sprites.c              OAM sprite rendering
    effects.c              Alpha blending, windowing, mosaic
  apu/
    apu.c/h                Audio mixer, FIFO management, sample buffer
    channel.c              Legacy GB sound channels (square, wave, noise)
    fifo.c                 DirectSound FIFO A/B
  mp2k/
    mp2k_detect.c/h        Finds the MP2K sound driver in a ROM
    mp2k_mixer.c/h         HQ music: re-mixes the driver's channels at 32768 Hz
  timer/
    timer.c/h              4 cascadable 16-bit timers
  interrupt/
    interrupt.c/h          IRQ controller (IE/IF/IME)
  sio/
    sio.c/h                Serial port registers (multiplayer mode)
    sio_link.c/h           Link cable transport over an AF_UNIX socket
  cartridge/
    cartridge.c/h          ROM loading, save type detection, file persistence
    flash.c/h              Flash 64K/128K save (Macronix / SST / Atmel / Panasonic)
    gpio.c/h               Cartridge GPIO at 0x080000C4/C6/C8 (data/dir/control)
    rtc.c/h                S-3511A real-time clock state machine
    sram.c                 Battery-backed SRAM
    eeprom.c/h             EEPROM save (bit-serial, DMA-driven)
  cheat/
    cheat.c/h              Cheat list and per-frame runner (RAM direct, no bus timing)
    cheat_types.h          Shared cheat types
    cheat_parse.c/h        Pulls codes out of pasted text
    cheat_decode.c/h       Device detection and decoding into device-neutral ops
    cheat_crypto.c/h       GameShark / Action Replay TEA and CodeBreaker ciphers
    cheat_file.c/h         `.cht` file reader and writer
    cheat_session.c/h      Turns cheats on, backing up the save first
    cheat_backup.c/h       Save backup before cheats run
    cheat_report.c/h       `--cheat-report` output
  savestate/
    savestate.c/h          Versioned save state serialization (ROM-hash guarded)
  rewind/
    rewind.c/h             Ring of in-memory snapshots for hold-to-rewind
    rewind_lz4.c/h         Vendored LZ4 compressor for the snapshots
  screenshot/
    screenshot.c/h         PNG screenshots (vendored stb_image_write.h)
  headless/
    headless.c/h           Windowless run loop for --headless
    fb_hash.c/h            FNV-1a framebuffer hash for --hash-out
    input_script.c/h       --input-script parser
  trace/
    trace.c/h              Per-instruction execution trace (--trace)
  input/
    input.c/h              Keypad registers
    keymap.c/h             Keyboard and controller bindings, keymap.ini
  game/
    game.c/h               Builds a snapshot of party, battle and encounter state
    game_mem.c/h           Read-only view of EWRAM, IWRAM and ROM
    game_profile.c/h       ROM detection and the profile table
    emerald_profile.c      Every ROM and RAM address for US Emerald rev 0, plus its GameRules
    frlg_profile.c         Addresses for US FireRed 1.0 and LeafGreen 1.1, plus the shared GameRules
    pokemon.c/h            Party Pokemon decryption, stats, IVs, nature, Hidden Power
    battle_info.c/h        Catch odds and move matchups for the current battle
    damage.c/h             Damage prediction (pokeemerald's damage formula)
    damage_read.c/h        Collects the damage inputs from battle memory
    encounters.c/h         Wild encounter tables for the current map
    gen3_text.c/h          Gen 3 character set to ASCII
    mirage.c/h             Mirage Island check and forecast
    nuzlocke.c/h           Nuzlocke rules and event log
    nuzlocke_read.c/h      Per-frame facts for the Nuzlocke tracker
    berry.c/h              Berry trees, growth and countdowns
    species_search.c/h     Name matching and wild locations for the Search page
    run_track.c/h          Turns game state into Run page events
    runlog.c/h             Run page event log
    runlog_read.c/h        Per-frame facts and game counters for the Run page
    overworld.c/h          Overworld X-Ray: trainers, sight lines and hidden items on screen
    script_walk.c/h        Finds NPC item gifts by walking their event scripts
    feebas.c/h             The Route 119 tiles that give Feebas for the current seed
    follower.c/h           Following Pokemon movement from Emerald RAM
    follower_ctx.c/h       Talk context (place, weather, mood) for the follower
    follower_places.c/h    Maps Emerald places to HeartGold/SoulSilver ones
  hgss/
    nds_fs.c/h             Opens and reads the user's HeartGold/SoulSilver ROM
    hgss_assets.c/h        Loads follower sprites, text box, font and messages
    hgss_gfx.h             Texture, frame and font decoders (rust/hgss/src/gfx.rs)
    hgss_msg.h             Message decryption and text variables (rust/hgss/src/msg.rs)
    hgss_rs.h              Layout probe that keeps the Rust structs in step with C
    hgss_talk.c/h          Follower talk rules
    hgss_model_lut.h       Species to follower model table
  ui/
    ui_canvas.c/h          Software canvas: rects, rounded shapes, alpha blending
    ui_font.c/h            Text measuring and drawing from the embedded Inter font
    ui_widgets.c/h         Pills, bars, tabs and other panel widgets
    ui_controls.c/h        Buttons, toggles, text fields and other Settings controls
    ui_input.c/h           Mouse and keyboard input for the widgets
    ui_theme.h             Colors and spacing for the panel
    ui_stb.c/h             Static-arena allocation wrapper for stb_truetype
    font_inter.h           Embedded Inter subset (generated by tools/embed_font.sh)
    stb_truetype.h         Vendored stb_truetype (public domain)
  frontend/
    frontend.c/h           SDL2 window, rendering, input polling, hotkeys, audio
    audio_out.c/h          Host audio clean-up and resampling before SDL
    frame_pacer.c/h        Frame timing for normal speed and fast-forward
    frame_advance.c/h      Frame advance (`\`)
    input_display.c/h      F3 input display
    overlay_draw.c/h       Pixel overlay primitives (input display, slot picker)
    slot_picker.c/h        F6 slot label editor and F7 slot list
    toast.c/h              Toasts and status badges
    hud_draw.c/h           Paints toasts and badges over the game
    settings.c/h           Settings window and its pages
    settings_controls.c    Controls page
    settings_general.c     General page
    settings_video.c       Video page
    settings_audio.c       Audio page
    settings_cheats.c      Cheats page
    prefs.c/h              prefs.ini for settings remembered between launches
    library.c/h            Game library data and library.txt
    library_view.c/h       Game library screen
    library_run.c/h        Runs the library until a game is picked
    rom_picker.c/h         Native file and folder dialogs (_mac.m, _win.c, _linux.c)
    macos_menu.h/.m        Settings entry in the macOS menu bar
    game_panel.c/h         Pokémon Companion pages, drawn with src/ui
    run_panel.c/h          Run page dashboard and Details sheet
    search_view.c/h        Search page state and input
    panel_layout.c/h       Window, game and panel rectangles from display density
    nuzlocke_session.c/h   Nuzlocke run file next to the ROM
    run_session.c/h        Run page log file next to the ROM
    berry_alerts.c/h       Berry alert toasts
    overworld_draw.c/h     Overworld X-Ray painter (cones, labels, sparkles), drawn with src/ui
    follower_glue.c/h      Wires Following Pokemon into each frame
    follower_draw.c/h      Follower sprites and effects
    follower_talk_ui.c/h   Follower talk sequence and HGSS text box
    cry_player.c/h         Decodes and plays Emerald cries for the follower
    video_filter.c/h       Screen filters and LCD color correction for the game image
    xbr.c/h                xBR 2x/3x/4x scaler (ported from xBRjs, MIT)
    super_xbr.c/h          Super xBR 2x scaler (Hyllian, MIT)
    debug.c                Register dumps (not built by CMake)
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
rust/
  hgss/                    Rust static library behind nds_fs.h, hgss_gfx.h, hgss_msg.h
    src/nds.rs             NDS file system and NARC archive walking
    src/gfx.rs             BTX0/BMD0/NCGR/NCLR/font decoding
    src/msg.rs             Message banks and Gen 3 to HGSS text
    src/ffi.rs             The C ABI
tests/                     Unit tests (gba_tests) and golden hashes in tests/golden/
tools/                     Build, packaging and test scripts (see below)
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
| 7 | Polish + accuracy (full playthrough) | Done, Pokemon Emerald playable end-to-end |

**Target milestone**: Full Pokemon Emerald playthrough from title screen to credits, **achieved**.

### Game compatibility

| Game | Status |
|------|--------|
| Pokemon Emerald (BPEE) | Fully playable |
| Pokemon FireRed (BPRE) | Boots through the Game Freak intro to the title screen. Save type auto-detected (Flash 128K). Full playthrough not yet verified. |

## Testing

### Unit tests

A C unit test suite lives in `tests/` (CMake target `gba_tests`):

```bash
cd build && cmake .. && make gba_tests && ctest --output-on-failure
# or run the binary directly
./gba_tests
```

`gba_tests` writes save files to the current directory, so run parallel copies from separate directories.

### Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request:

- `build-and-test`: Debug build with `-DENABLE_WERROR=ON` on Linux, macOS and Windows, then the unit tests.
- `sanitizers`: the unit tests on Linux under AddressSanitizer and UBSan.
- `golden-frame`: a Release build checked against the golden hashes (below).

`.github/workflows/release.yml` runs on `v*` tags. It builds and tests on all three systems, packages the macOS `.dmg`, Windows `.zip` and Linux AppImage (with update info and a `.zsync` for AppImageUpdate), checks the macOS bundle and the AppImage, and creates the GitHub release.

### Test ROMs

Place test ROMs in the `roms/` directory (not tracked by git). `tools/fetch_test_roms.sh roms` downloads the jsmolka tests, the tonc demos and `mgba_suite.gba`:

- [**jsmolka/gba-tests**](https://github.com/jsmolka/gba-tests): ARM/Thumb instruction correctness
- [**armwrestler**](https://github.com/mic-/armwrestler-gba-fixed): Visual ARM instruction test grid
- [**mgba test suite**](https://github.com/mgba-emu/suite): Timer, DMA, PPU timing validation
- [**tonc demos**](https://www.coranac.com/tonc/text/): Visual PPU mode verification

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

The mGBA suite is menu driven, so each of its goldens has an input script (`tests/golden/mgba_suite_<name>.input`) that picks a test group. To run every golden locally:

```bash
tools/fetch_test_roms.sh roms
for g in tests/golden/{jsmolka,tonc}_*.hash; do n=$(basename $g .hash); tools/check_golden.sh build/gba_emulator roms/$n.gba $g | tail -1; done
for n in timing timers timer_irq dma; do tools/check_golden.sh build/gba_emulator roms/mgba_suite.gba tests/golden/mgba_suite_$n.hash "" tests/golden/mgba_suite_$n.input | tail -1; done
```

`check_golden.sh <emulator> <rom> <golden> [frames] [input-script]` runs the frame count from the golden unless you pass one, and `""` keeps that default while giving an input script. Don't regenerate a golden to make a failing check pass: a mismatch means the rendered output changed and needs a look first.

**To add a new golden ROM:**

1. Place or fetch the ROM somewhere local (e.g. `tools/fetch_test_roms.sh /tmp/gba-test-roms`).
2. Bake the golden hash from a clean Release build:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target gba_emulator
   build/gba_emulator path/to/rom.gba \
       --headless --frames 240 --hash-out tests/golden/<name>.hash
   ```
3. Manually verify the ROM actually renders correctly. Interactive mode
   is the simplest way (`./gba_emulator path/to/rom.gba`).
4. Add a new step in `.github/workflows/ci.yml`'s `golden-frame` job
   calling `tools/check_golden.sh build/gba_emulator
   /tmp/gba-test-roms/<rom>.gba tests/golden/<name>.hash`.
5. Commit the new `.hash` file and CI step together so they land in the
   same change.

### Pokemon Emerald smoke check (local only)

`tests/golden/emerald.hash` pins the first 300 frames of an Emerald boot
(Game Freak logo + intro movie). The boot uses the frozen save in
`tests/golden/emerald.sav`, copied to a temp folder, so in-game saves
don't change the result. The ROM is commercial and never fetched
in CI, so the check runs locally and skips cleanly when the ROM is
absent:

```bash
tools/smoke_emerald.sh              # uses build/gba_emulator + roms/emerald.gba
tools/smoke_cheats.sh               # a CodeBreaker code on a temp copy of the save
```

Run `smoke_emerald.sh` before and after changes to the CPU, PPU, DMA, or timers.
`smoke_cheats.sh` sets the first party Pokemon to level 100 with a cheat,
reads it back with `--game-dump`, and checks that the save was backed up first.

## Tools

| Script | What it does |
|--------|--------------|
| `tools/check_golden.sh` | Runs a ROM headless and diffs its frame hashes against a golden |
| `tools/fetch_test_roms.sh` | Downloads the pinned public test ROMs |
| `tools/smoke_emerald.sh` | Emerald boot check against `tests/golden/emerald.hash` (local only) |
| `tools/smoke_cheats.sh` | End-to-end cheat check on Emerald (local only) |
| `tools/make_mac_app.sh` | Makes a double-clickable `GBA Emulator.app` (on the Desktop by default) that rebuilds from this repo on every launch |
| `tools/check_mac_bundle.sh` | Checks that a release `.app` is self-contained and targets macOS 11 |
| `tools/fetch_sdl2_framework.sh` | Downloads the pinned SDL2 framework for macOS release bundles |
| `tools/make_appimage.sh` | Packs a Linux build into an AppImage with SDL2 bundled |
| `tools/make_icons.sh` | Regenerates the app icons in `res/icon/` from `tools/mac_app_icon.swift` (macOS only) |
| `tools/embed_font.sh` | Regenerates `src/ui/font_inter.h` from Inter 4.1 |

## References

- [GBATEK](https://problemkaputt.de/gbatek.htm): Primary GBA hardware reference
- [GBATEK (Markdown)](https://mgba-emu.github.io/gbatek/): Searchable GBATEK mirror
- [Copetti: GBA Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/): High-level architecture overview
- [ARM7TDMI Decoding Guide](https://www.gregorygaines.com/blog/decoding-the-arm7tdmi-instruction-set-game-boy-advance/): Instruction set decoding walkthrough
- [awesome-gbadev](https://github.com/gbadev-org/awesome-gbadev): Curated GBA development resources
- [mGBA](https://github.com/mgba-emu/mgba): Reference emulator source
- [Tonc](https://www.coranac.com/tonc/text/hardware.htm): GBA hardware programming tutorial
- [Inter](https://rsms.me/inter/) 4.1: Font for Pokémon Companion, under the SIL Open Font License 1.1 (`fonts/Inter-LICENSE.txt`). `tools/embed_font.sh` regenerates `src/ui/font_inter.h` from it.
- [stb_truetype](https://github.com/nothings/stb): Glyph rasterizer for the panel font, public domain
- [xBRjs](https://github.com/joseprio/xBRjs) by Josep del Rio: the xBR scaler (algorithm by Hyllian) ported to `src/frontend/xbr.c`, MIT License (notice in the file)
- Super xBR by Hyllian, via [pixelscalers](https://github.com/janert/pixelscalers): ported to `src/frontend/super_xbr.c`, MIT License (notice in the file)
- [gba-color](https://github.com/libretro/glsl-shaders/blob/master/handheld/shaders/color/gba-color.glsl) by Pokefan531 and hunterk: the LCD color correction math in `src/frontend/video_filter.c`, public domain
