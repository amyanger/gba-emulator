# Golden frame hashes

Each `<name>.hash` holds one `<frame> <FNV1a>` line per frame from a
headless run, checked by `tools/check_golden.sh`. Do not regenerate a
golden to make a failing check pass; a mismatch means rendered output
changed and needs a look first.

The jsmolka, Emerald and tonc goldens pin screens that were checked by eye
against a reference. The mGBA suite goldens are the exception: they are
regression ratchets that pin current, partial pass counts (see below).

## mGBA test suite

`mgba_suite.gba` (fetched by `tools/fetch_test_roms.sh`) is menu driven,
so each golden comes with an input script (`mgba_suite_<name>.input`)
that selects one test group and runs it. The hash covers the run up to a
static results screen.

These goldens are regression ratchets. They pin the emulator's current
results, which are partial, not verified-correct screens. When a fix
raises a pass count, the golden will fail. Confirm the new count from a
`--screenshot-out` capture, then re-bake the golden and update the
table below in the same change.

| Golden | Group | Frames | Passed |
|--------|-------|--------|--------|
| `mgba_suite_timing` | Timing tests | 420 | 930/2020 |
| `mgba_suite_timers` | Timer count-up tests | 240 | 554/936 |
| `mgba_suite_timer_irq` | Timer IRQ tests | 120 | 70/90 |
| `mgba_suite_dma` | DMA tests | 240 | 1244/1244 |

Re-check one locally:

```bash
tools/check_golden.sh build/gba_emulator roms/mgba_suite.gba \
    tests/golden/mgba_suite_dma.hash "" tests/golden/mgba_suite_dma.input
```

## tonc demos

Prebuilt demos from tonc v1.4.2 (`tonc-bin.zip`, fetched and sha256-checked by
`tools/fetch_test_roms.sh` as `tonc_<name>.gba`). None of them need input.
Each final frame was compared against tonc's own screenshots or source art.

| Golden | Frames | Covers |
|--------|--------|--------|
| `tonc_m3_demo.hash` | 60 | Mode 3 bitmap BG2 (identity affine sampling). Pixel-exact vs tonc's m3_demo.png. |
| `tonc_pageflip.hash` | 150 | Mode 4 paletted bitmap and DISPCNT page flip every 60 frames (front, back, front). Both pages match the source art. |
| `tonc_win_demo.hash` | 60 | WIN0/WIN1/WINOUT layer masks over two tiled BGs. |
| `tonc_dma_demo.hash` | 60 | HBlank DMA rewriting WIN0H each line to cut a circular window. |
| `tonc_m7_ex.hash` | 120 | Mode 1 per-scanline affine BG2 (mode 7 floor), sprites, alpha blend fog. Pixel-exact vs tonc's m7_ex_00.png. |
| `tonc_oacombo.hash` | 60 | Double-size affine sprites; the three combos must render identically. |
| `tonc_prio_demo.hash` | 180 | Animated 96-sprite helix interleaved with three BG priorities. |
