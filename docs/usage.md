# Using the emulator

Installing the downloads, command-line options, controls, headless mode, the link cable and cheats.

## Installing a pre-built binary

Pre-built binaries for Linux (x86_64), macOS (Apple Silicon), and Windows (x86_64) are attached to every tagged release on the [Releases page](https://github.com/amyanger/gba-emulator/releases/latest).

Each download includes everything it needs, SDL2 included.

### macOS (Apple Silicon, macOS 11 or newer)

1. Download `gba_emulator-0.5.0-macOS-arm64.dmg` from the Releases page and open it.
2. Drag **GBA Emulator** into **Applications**.
3. Double-click **GBA Emulator** and choose a `.gba` file. You can also drop a ROM onto the app icon.

The app isn't signed with a paid Apple certificate, so the first launch is blocked with a "can't be opened" message. Open **System Settings > Privacy & Security**, scroll down and click **Open Anyway**. You only need to do this once. From Terminal, this does the same:

```bash
xattr -dr com.apple.quarantine "/Applications/GBA Emulator.app"
```

Saves are written next to the ROM. To use command-line options, run the binary inside the app:

```bash
"/Applications/GBA Emulator.app/Contents/MacOS/GBA Emulator" path/to/rom.gba --scale 4
```

### Linux (x86_64)

1. Download `gba_emulator-0.5.0-Linux-x86_64.AppImage` from the Releases page.
2. Make it executable: right-click it, open **Properties** and allow running it as a program, or run `chmod +x gba_emulator-0.5.0-Linux-x86_64.AppImage`.
3. Double-click it and choose a `.gba` file.

The file picker uses `zenity` (GNOME and most desktops) or `kdialog` (KDE). If neither is installed, pass the ROM on the command line:

```bash
./gba_emulator-0.5.0-Linux-x86_64.AppImage path/to/rom.gba --scale 3
```

### Windows (x86_64)

1. Download `gba_emulator-0.5.0-Windows-AMD64.zip` from the Releases page and extract it.
2. Double-click `gba_emulator.exe` in the `bin` folder and choose a `.gba` file. Dragging a ROM onto the `.exe` also works.

Windows SmartScreen may warn that the app is from an unknown publisher, because it isn't signed with a paid certificate. Click **More info**, then **Run anyway**.

Command-line options still work from a terminal:

```
gba_emulator-0.5.0-Windows-AMD64\bin\gba_emulator.exe path\to\rom.gba --scale 3
```

### Other platforms

No pre-built binaries for Intel Macs yet. Build from source, see [Building from source](development.md#building-from-source).

## Running from the command line

```bash
./build/gba_emulator <rom_file> [options]
```

## Options

| Flag | Description |
|------|-------------|
| `--bios <file>` | Path to GBA BIOS dump (optional, HLE fallback available) |
| `--scale <n>` | Window scale multiplier (default: 3) |
| `--cheats <file>` | Cheat file to use instead of `<rom>.cht` |
| `--cheat-report` | Print how each cheat was read (device, decoded codes, problems) and exit |
| `--keymap <file>` | Path to a keymap `.ini` for custom keyboard bindings |
| `--link-master <path>` | Listen for a peer GBA at AF_UNIX socket path (host side) |
| `--link-client <path>` | Connect to a peer GBA at AF_UNIX socket path (client side) |
| `--mute` | Start with audio muted (toggle back with `M`) |
| `--trace <file>` | Write a per-instruction execution trace to `file` (expect 5-10x slowdown) |
| `--trace-from <hex>` | Only trace instructions at PC >= this address (hex) |
| `--trace-to <hex>` | Only trace instructions at PC <= this address (hex) |
| `--trace-frames <n>` | Stop tracing after `n` frames |

## Example

```bash
./build/gba_emulator roms/emerald.gba --bios bios/gba_bios.bin --scale 3
```

## Controls

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
| F3 | Toggle input display HUD (mini-GBA overlay showing held buttons) |
| F4 | Show or hide Overworld X-Ray: trainer sight lines and hidden items on the map (original Pokemon Emerald US/English ROM only, remembered between launches) |
| F9 | Show or hide Emerald Companion (original Pokemon Emerald US/English ROM only) |
| F10 | Cycle the Emerald Companion page: Auto, Party, Route, Nuzlocke (Auto shows the battle page during battles; a battle switches to Auto and the old page returns afterwards) |
| [ and ] | Select the previous or next Pokemon on the Party page, or switch Nuzlocke views (unless bound to a GBA button) |
| N | Start a Nuzlocke run, on the Nuzlocke page (unless bound to a GBA button) |
| F5 | Save state to current slot |
| F6 | Edit label of current save-state slot |
| F7 | Open save-state slot picker |
| F8 | Load state from current slot |
| 0–9 | Select save state slot |
| Tab (hold) | Fast-forward |
| `` ` `` | Toggle fast-forward |
| `\` | Frame advance — auto-pauses if running; ignored during fast-forward / rewind |
| Backspace (hold) | Rewind (60s window, audio muted) |
| M | Toggle audio mute |
| Space | Pause / resume emulation |
| F12 | Screenshot (saved next to ROM) |
| F11 | Toggle fullscreen |
| Escape | Quit |

On macOS, F-keys need **fn** held (for example fn+F9) unless the keyboard is set to use them as standard function keys.

Save files (`<rom>.sav`) and save states (`<rom>.ss<N>`) are written next to the ROM.

## Headless mode

Run the emulator without an SDL window or audio output, writing one
FNV-1a hash of the 240×160 framebuffer per frame:

```bash
./gba_emulator <rom.gba> --headless --frames 240 --hash-out hashes.txt
```

| Flag | Meaning |
|------|---------|
| `--headless` | Skip SDL, audio, link cable, X-Ray (unless `--xray-out` is given), and rewind. |
| `--frames <n>` | Run exactly `n` frames then exit. Required with `--headless`. |
| `--hash-out <file>` | Write per-frame `<N> <FNV1a-hex>` lines. Defaults to stdout. |
| `--screenshot-out <file>` | After the run, save the final framebuffer as PNG. |
| `--input-script <file>` | Replay scripted keypad input. Headless only. Format below. |
| `--panel-out <file>` | After the run, render the Emerald Companion panel to a PNG. Requires `--headless`. |
| `--panel-density <f>` | Panel pixels per point, 0.5 to 4.0 (default 1.0). Requires `--headless`. |
| `--panel-page <p>` | Panel page: `auto`, `party`, `route` or `nuzlocke` (default `auto`). Requires `--headless`. |
| `--panel-select <n>` | Selected party slot, 0 to 5, for the party page, or the Nuzlocke view: 0 routes, 1 team, 2 graveyard (default 0). Requires `--headless`. |
| `--nuzlocke-file <file>` | Track a Nuzlocke run in `file` during the run, starting one if the file has none. `--game-dump` then adds `nuz` lines. Requires `--headless`. |
| `--xray-out <file>` | After the run, render the Hardware X-Ray window to a 2400x1640 PNG. Requires `--headless` and an X-Ray build. |
| `--overworld-out <file>` | After the run, write the final frame at 3x (720x480 PNG) with the Overworld X-Ray drawn on it. Requires `--headless`; original Pokemon Emerald (US) only. |
| `--game-dump <file>` | After the run, write the decoded Emerald game state (party, battle, encounters, and an `[overworld]` section matching the final frame) to `file`. Requires `--headless`. |

An input script has one event per line, `<frame> <press|release> <KEY>`,
where `<frame>` is the 0-based frame index (same as in `--hash-out`) and
`KEY` is one of `A B SELECT START RIGHT LEFT UP DOWN R L`. An event
applies before its frame runs. Frames must not decrease from one line to
the next. `#` starts a comment and blank lines are ignored. A malformed
line aborts the run with an error. Example:

```
# open the second menu entry
30 press DOWN
32 release DOWN
36 press A
38 release A
```

Headless mode is incompatible with `--link-master` / `--link-client`
(they would block the dispatch waiting for a peer).

## Link Cable

Two emulator instances on the same machine can connect over a UNIX domain socket and exchange GBA SIO multiplayer-mode packets — enough for Pokemon trade and battle between local windows.

```bash
# Terminal 1 (host) — opens the socket and waits for a peer.
./build/gba_emulator roms/emerald.gba --link-master /tmp/gba.sock

# Terminal 2 (client) — connects to the host's socket.
./build/gba_emulator roms/emerald.gba --link-client /tmp/gba.sock
```

The host blocks at startup until the client connects. After both sides are up, in-game link interactions (Cable Club, trades, battles) cross between the two windows.

**Limitations:** Multiplayer 16-bit mode only, 2 players. Normal 8/32-bit and UART SIO modes are not implemented. There is no internet-netplay support — the socket path must be local. The two emulators run lockstep at the SIO transfer point: one will briefly stall waiting for the other to reach the same `sio_tick` if they fall out of sync.

## Cheats

Cheats for a game live in `<rom>.cht` next to the ROM (for example `emerald.gba.cht`) and load automatically. Copy codes from a cheat site into it as they are written there, under a device line and a name:

```ini
# Lines starting with # are comments
[CodeBreaker]
Level 100 (first Pokemon, Emerald US)
32024540 0064

[ActionReplay]
Code name from the site
XXXXXXXX YYYYYYYY
XXXXXXXX YYYYYYYY
```

- **Devices:** `[CodeBreaker]`, `[GameShark]` (v1/v2), `[ActionReplay]` (v3, also sold as GameShark v3/SP), plus `[GameSharkRaw]` and `[ActionReplayRaw]` for unencrypted codes. Encrypted codes are decrypted for you.
- **Detection:** when a block has no `device=` line, the emulator works out the device from the codes themselves (a master code's game ID line settles it). If it can't tell, the cheat is not run; add `device=override` under the name to force the section's device.
- **On/off:** `enabled=false` under the name turns a cheat off.
- **Master codes** ("must be on") are recognized and skipped; the emulator applies codes directly every frame, so no hook is needed. ROM patch codes, cheat-device button codes and `DEADFACE` seed changes are not supported and are reported as such.
- **Safety:** codes may only touch RAM, palette, VRAM, OAM and I/O. Before any cheat runs, the battery save is copied to `<rom>.sav.cheatbak1` (the last 5 copies are kept, `cheatbak1` is the newest). If that copy can't be made, cheats stay off.
- **Checking codes:** `--cheat-report` prints the detected device and the decoded operations for every cheat, which helps when a code doesn't seem to work.
