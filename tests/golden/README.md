# Golden frames

Each `.hash` file holds one FNV1a framebuffer hash per frame from a headless
run (`--headless --frames N --hash-out FILE`). `tools/check_golden.sh` diffs a
fresh run against it. Every golden pins a screen that was checked by eye
against a reference before it was committed. Do not regenerate one to make a
failing check pass.

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
