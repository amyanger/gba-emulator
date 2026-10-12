# GBA Emulator

[![CI](https://github.com/amyanger/gba-emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/amyanger/gba-emulator/actions/workflows/ci.yml)
[![Release](https://github.com/amyanger/gba-emulator/actions/workflows/release.yml/badge.svg?event=push)](https://github.com/amyanger/gba-emulator/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/amyanger/gba-emulator?label=release)](https://github.com/amyanger/gba-emulator/releases/latest)

A Game Boy Advance emulator written from scratch in C, featuring **Hardware X-Ray Mode**: a real-time view of what the GBA hardware is actually doing while a game runs.

No other GBA emulator offers this. Every emulator is a black box. This one lets you see inside.

Built around an ARM7TDMI CPU interpreter, scanline-based PPU, and an SDL2 frontend. No external libraries beyond SDL2. Pokemon Emerald is fully playable end-to-end.

![Hardware X-Ray window on the Pokemon Emerald title screen: Rayquaza, clouds and the logo on separate background layers, live audio waveforms, CPU registers and the sound DMAs](docs/screenshots/xray.png)

## Download

Ready-to-run apps for **macOS** (Apple Silicon), **Windows** and **Linux** are on the [latest release](https://github.com/amyanger/gba-emulator/releases/latest). Download the one for your system, double-click it and pick a game from the library. There's nothing else to install.

| System | File |
|--------|------|
| macOS 11+ (Apple Silicon) | `gba_emulator-<version>-macOS-arm64.dmg` |
| Windows (64-bit) | `gba_emulator-<version>-Windows-AMD64.zip` |
| Linux (x86_64) | `gba_emulator-<version>-Linux-x86_64.AppImage` |

The apps aren't signed with a paid certificate, so macOS and Windows ask you to confirm the first time you open them. [Installing a pre-built binary](docs/usage.md#installing-a-pre-built-binary) has step-by-step instructions for each system.

## Highlights

- **[Hardware X-Ray Mode](docs/hardware-xray.md)** (F2): a second window showing background layers, sprites, tiles, palettes, audio channels, CPU registers, timers, DMA and interrupts as the game runs.
- **[Emerald Companion](docs/emerald-companion.md)** (F9): a side panel for Pokemon Emerald with wild battle IVs, natures and catch odds, move matchups both ways, your party's hidden stats and the current route's encounter table.
- **[Nuzlocke tracker](docs/emerald-companion.md#nuzlocke)**: a Companion page that keeps your Nuzlocke run honest, logging each area's first encounter, every death and any reload after a death.
- **[Overworld X-Ray](docs/emerald-companion.md#overworld-x-ray)** (F4): trainer sight lines, item names and NPC gifts drawn right on the map in Pokemon Emerald.
- **[Following Pokemon](docs/usage.md#following-pokemon)** (F1): your lead Pokemon walks behind you in Pokemon Emerald and talks like it does in HeartGold and SoulSilver, using your own HeartGold or SoulSilver ROM.
- **Save states and rewind**: 10 save-state slots, plus hold Backspace to rewind up to 60 seconds.
- **[Cheats](docs/usage.md#cheats)**: paste CodeBreaker, GameShark or Action Replay codes as found online. Your save is backed up before any cheat runs.
- **[Link cable](docs/usage.md#link-cable)**: two windows on the same machine can trade and battle.
- **[Screen filters](docs/usage.md#screen-filters)**: smooth out jagged pixel edges and match the colors of a real GBA screen.
- **[HQ music](docs/hq-music.md)** (H): game music re-mixed in high quality for games that use the standard MP2K sound driver.
- **Fast-forward**, frame advance, screenshots and fullscreen.

Emerald Companion, Overworld X-Ray and Following Pokemon only work with the original, unmodified Pokemon Emerald (US/English) ROM. Every other game still runs normally.

![Emerald Companion beside a wild battle against a Poochyena, showing your Swampert's moves and the Poochyena's moves](docs/screenshots/panel-battle.png)

## Quick start

Open the app and pick a game from the library, or run it from a terminal:

```bash
./build/gba_emulator path/to/rom.gba --scale 3
```

| Key | GBA button |
|-----|------------|
| Z / X | A / B |
| Enter / Right Shift | Start / Select |
| Arrow keys | D-Pad |
| A / S | L / R |

| Key | Does |
|-----|------|
| F2 | Hardware X-Ray Mode |
| F4 | Overworld X-Ray (Pokemon Emerald) |
| F9 | Emerald Companion (Pokemon Emerald) |
| F5 / F8 | Save / load state |
| Tab (hold) | Fast-forward |
| Backspace (hold) | Rewind |
| Space | Pause |
| F11 | Fullscreen |

On macOS, hold **fn** with the F-keys unless your keyboard is set to use them as standard function keys. Saves are written next to the ROM. The [full controls list and every command-line option](docs/usage.md) are in the usage guide.

## Building from source

You need SDL2 2.0+ and CMake 3.21+ (`brew install sdl2 cmake` on macOS, `apt install libsdl2-dev cmake` on Linux).

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

Build options, the architecture, the test suite and the golden-frame checks are in the [development guide](docs/development.md).

## Documentation

- [Using the emulator](docs/usage.md): installing, command-line options, all controls, headless mode, link cable, cheats
- [Emerald Companion and Overworld X-Ray](docs/emerald-companion.md)
- [Hardware X-Ray Mode](docs/hardware-xray.md)
- [Development](docs/development.md): building, what's emulated, architecture, project structure, testing, roadmap, references

## License

This project is open source. See [LICENSE](LICENSE) for details.

---

**Note**: You must supply your own GBA BIOS and ROM files. They are not included in this repository.
