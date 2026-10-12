# GBA Emulator

[![CI](https://github.com/amyanger/gba-emulator/actions/workflows/ci.yml/badge.svg)](https://github.com/amyanger/gba-emulator/actions/workflows/ci.yml)
[![Release](https://github.com/amyanger/gba-emulator/actions/workflows/release.yml/badge.svg?event=push)](https://github.com/amyanger/gba-emulator/actions/workflows/release.yml)
[![Latest release](https://img.shields.io/github/v/release/amyanger/gba-emulator?label=release)](https://github.com/amyanger/gba-emulator/releases/latest)

A Game Boy Advance emulator written from scratch in C, featuring **Hardware X-Ray Mode**: a real-time view of what the GBA hardware is actually doing while a game runs.

No other GBA emulator offers this. Every emulator is a black box. This one lets you see inside.

Built around an ARM7TDMI CPU interpreter, scanline-based PPU, and an SDL2 frontend. No external libraries beyond SDL2. Pokemon Emerald is fully playable end-to-end.

**Jump to:** [Download](#download) · [Features](#features) · [Screenshots](#screenshots) · [Quick start](#quick-start) · [Building](#building-from-source) · [Documentation](#documentation)

![Hardware X-Ray window on the Pokemon Emerald title screen: Rayquaza, clouds and the logo on separate background layers, live audio waveforms, CPU registers and the sound DMAs](docs/screenshots/xray.png)

## Download

Ready-to-run apps for **macOS** (Apple Silicon), **Windows** and **Linux** are on the [latest release](https://github.com/amyanger/gba-emulator/releases/latest). Download the one for your system, double-click it and pick a game from the library. There's nothing else to install.

| System | File |
|--------|------|
| macOS 11+ (Apple Silicon) | `gba_emulator-<version>-macOS-arm64.dmg` |
| Windows (64-bit) | `gba_emulator-<version>-Windows-AMD64.zip` |
| Linux (x86_64) | `gba_emulator-<version>-Linux-x86_64.AppImage` |

The apps aren't signed with a paid certificate, so macOS and Windows ask you to confirm the first time you open them. [Installing a pre-built binary](docs/usage.md#installing-a-pre-built-binary) has step-by-step instructions for each system.

## Features

### Every game

| Feature | Key | What it does |
|---------|-----|--------------|
| **[Hardware X-Ray Mode](docs/hardware-xray.md)** | F2 | A second window showing background layers, sprites, tiles, palettes, audio channels, CPU registers, timers, DMA and interrupts as the game runs. |
| **[Save states and rewind](docs/usage.md#controls)** | F5 / F8, Backspace | 10 save-state slots, plus hold Backspace to rewind up to 60 seconds. |
| **[Fast-forward](docs/usage.md#controls)** | Tab | Hold to speed up. Frame advance, screenshots and fullscreen are in the controls list. |
| **[Cheats](docs/usage.md#cheats)** | | Paste CodeBreaker, GameShark or Action Replay codes as found online. Your save is backed up before any cheat runs. |
| **[Link cable](docs/usage.md#link-cable)** | | Two windows on the same machine can trade and battle. |
| **[Screen filters](docs/usage.md#screen-filters)** | | Smooth out jagged pixel edges and match the colors of a real GBA screen. |
| **[HQ music](docs/hq-music.md)** | H | Game music re-mixed in high quality for games that use the standard MP2K sound driver. |

### Pokemon Emerald extras

These need the original, unmodified Pokemon Emerald (US/English) ROM. Every other game still runs normally.

| Feature | Key | What it does |
|---------|-----|--------------|
| **[Emerald Companion](docs/emerald-companion.md)** | F9 | A side panel with live info from your game. F10 switches pages. |
| &nbsp;&nbsp;[Battle](docs/emerald-companion.md#battle) | | The opponent's IVs, nature, ability and held item, move matchups both ways, and in wild battles the catch odds for each ball. |
| &nbsp;&nbsp;[Party](docs/emerald-companion.md#party) | | Your Pokemon's hidden stats: IVs, EVs, Hidden Power, friendship. |
| &nbsp;&nbsp;[Route](docs/emerald-companion.md#route) | | The wild encounter table for where you are, with caught Pokemon checked off. |
| &nbsp;&nbsp;[Mirage Island](docs/emerald-companion.md#mirage-island) | J, twice | Which of your Pokemon raises Mirage Island today and when it next will, then jump the clock to that day. |
| &nbsp;&nbsp;[Nuzlocke](docs/emerald-companion.md#nuzlocke) | N | Logs each area's first encounter, every death and any reload after a death. |
| &nbsp;&nbsp;[Berries](docs/emerald-companion.md#berries) | B | Every berry tree you've planted, with growth, watering and countdowns, plus a toast when berries are ripe, need water or are about to drop. |
| &nbsp;&nbsp;[Search](docs/emerald-companion.md#search) | / | Type a Pokemon's name to see every wild spot with levels and odds, or how to get it by evolution or breeding. |
| &nbsp;&nbsp;[Run stats](docs/emerald-companion.md#run) | F10 | Your playthrough's story: badge times, catches, faints, evolutions, an MVP and the game's own counters. |
| **[Overworld X-Ray](docs/emerald-companion.md#overworld-x-ray)** | F4 | Trainer sight lines, item names, NPC gifts and the hidden Feebas tiles drawn right on the map. |
| **[Following Pokemon](docs/usage.md#following-pokemon)** | F1 | Your lead Pokemon walks behind you and talks like it does in HeartGold and SoulSilver, using your own HeartGold or SoulSilver ROM. |

## Screenshots

<table>
<tr>
<td width="50%"><a href="docs/emerald-companion.md#battle"><img src="docs/screenshots/panel-battle.png" alt="Emerald Companion beside a wild battle against a Poochyena, showing your Swampert's moves and the Poochyena's moves"></a><br><b>Battle:</b> IVs, nature and matchups for a wild Poochyena</td>
<td width="50%"><a href="docs/emerald-companion.md#berries"><img src="docs/screenshots/panel-berries-ripe.png" alt="Route 102 with a ripe Pecha tree, and the Berries page listing it under Needs you"></a><br><b>Berries:</b> a ripe Pecha, 10 hours before it drops</td>
</tr>
<tr>
<td><a href="docs/emerald-companion.md#nuzlocke"><img src="docs/screenshots/panel-nuzlocke-routes.png" alt="Nuzlocke page listing every area used so far with what was met there"></a><br><b>Nuzlocke:</b> every area used and what you met there</td>
<td><a href="docs/emerald-companion.md#mirage-island"><img src="docs/screenshots/panel-mirage-forecast.png" alt="Route page in Lilycove City with the Mirage Island forecast"></a><br><b>Mirage Island:</b> which Pokemon raises it, and when</td>
</tr>
<tr>
<td><a href="docs/usage.md#following-pokemon"><img src="docs/screenshots/following-talk.png" width="100%" alt="Latios following the player in Lilycove City, saying LATIOS looks very happy! in the HeartGold text box"></a><br><b>Following Pokemon:</b> Latios in Lilycove City</td>
<td><a href="docs/emerald-companion.md#party"><img src="docs/screenshots/panel-party.png" alt="Party page showing a Torchic's stats, IVs and EVs"></a><br><b>Party:</b> hidden stats for each Pokemon</td>
</tr>
<tr>
<td><a href="docs/emerald-companion.md#search"><img src="docs/screenshots/panel-search-typing.png" alt="Search page with ra typed in, the list narrowed to matching names and Ralts picked, showing its Route 102 grass spot"></a><br><b>Search:</b> the list narrows as you type</td>
<td><a href="docs/emerald-companion.md#search"><img src="docs/screenshots/panel-search.png" alt="Search page for Gardevoir, showing it evolves from Kirlia at Lv 30 and Ralts at Lv 20, with Ralts caught on Route 102"></a><br><b>Search:</b> not wild? It shows the evolution path</td>
</tr>
</table>

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
