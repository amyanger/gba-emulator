#!/usr/bin/env bash
# Smoke-check the actual target game: boot Pokemon Emerald headless and
# diff the first 300 frames against tests/golden/emerald.hash.
# The ROM is commercial and can't be fetched in CI, so this skips
# cleanly when it isn't present — run it locally before/after core
# CPU/PPU/timer changes.
# Usage: tools/smoke_emerald.sh [emulator] [rom]
set -euo pipefail

EMU="${1:-build/gba_emulator}"
ROM="${2:-roms/emerald.gba}"
GOLDEN="$(dirname "$0")/../tests/golden/emerald.hash"

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: $ROM not present (commercial ROM, not distributed)"
    exit 0
fi

# Boot against the frozen save snapshot when present. Emerald reads the
# save block during the intro, so booting with the player's live .sav
# shifts a few late intro frames and breaks the baseline every time the
# game is saved. Stage a temp dir with the ROM symlinked next to the
# frozen save; any autosave flush lands in the temp dir, not the real save.
SNAP="$(dirname "$0")/../tests/golden/emerald.sav"
if [[ -f "$SNAP" ]]; then
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    ln -s "$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")" "$WORK/emerald.gba"
    cp "$SNAP" "$WORK/emerald.gba.sav"
    ROM="$WORK/emerald.gba"
fi

exec "$(dirname "$0")/check_golden.sh" "$EMU" "$ROM" "$GOLDEN"
