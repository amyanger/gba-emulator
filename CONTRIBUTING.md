# Contributing

Thanks for wanting to help. Bug fixes, accuracy fixes and test ROM results are always welcome. For a new feature or anything that touches several parts of the emulator, open an issue first so we can agree on the approach before you spend time on it.

## Ground rules

- **No ROMs, BIOS files, saves or game data in the repo.** That includes tables copied or extracted from a ROM (names, stats, moves, encounters). The Pokémon Companion reads all of this from the user's own ROM at runtime, and the source only holds addresses and struct offsets. A PR that adds game data will be closed.
- **You must have built and run your change yourself.** Say what you tested it on (game, OS). PRs that clearly were never compiled or run will be closed without review, whether a person or an AI wrote them.
- **Keep PRs small and focused.** One fix or feature per PR. No drive-by reformatting or refactoring of code you didn't need to touch.
- **Hardware behaviour must match the references.** Check [GBATEK](https://problemkaputt.de/gbatek.htm) or the [mGBA source](https://github.com/mgba-emu/mgba) and link the relevant section in the PR.

## Code style

The design rules are in [docs/development.md](docs/development.md#design-principles). The ones that come up most in review:

- The CPU never calls the PPU, APU or other hardware directly. Everything goes through memory-mapped I/O in `bus.c`.
- No dynamic allocation in the emulated core (cpu, bus, ppu, apu, timers, dma, cartridge) or in `src/game/`, `src/ui/`, `src/mp2k/`, `src/hgss/`, `src/rando/`.
- `src/game/` only observes: it reads RAM and ROM arrays, never goes through the bus and never writes emulated memory.
- Use fixed-width types (`uint32_t`, not `int`) for hardware state, and the `BIT`/`BITS` macros from `common.h`.
- One file per hardware component.
- Formatting is in `.clang-format`, but only format the lines you changed. Running it on a whole file creates a noisy diff.

## Before opening a PR

Build with warnings as errors and run the tests (see [docs/development.md](docs/development.md#testing)):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_WERROR=ON
cmake --build build
cd build && ctest --output-on-failure && cd ..
```

Then run the golden-frame checks:

```bash
tools/fetch_test_roms.sh roms
for g in tests/golden/{jsmolka,tonc}_*.hash; do n=$(basename $g .hash); tools/check_golden.sh build/gba_emulator roms/$n.gba $g | tail -1; done
for n in timing timers timer_irq dma; do tools/check_golden.sh build/gba_emulator roms/mgba_suite.gba tests/golden/mgba_suite_$n.hash "" tests/golden/mgba_suite_$n.input | tail -1; done
```

CI runs all of this on Linux, macOS and Windows, plus ASan/UBSan and the Rust tests. A PR can only be merged once CI is green.

**Do not regenerate or edit `tests/golden/*.hash` files.** They pin screens that were checked by hand. If your change is supposed to alter a golden (for example an accuracy fix that makes more mGBA suite tests pass), say so in the PR with a screenshot (`--screenshot-out`) and the maintainer will re-pin it.

## License

By contributing you agree that your work is released under the [MIT License](LICENSE).
