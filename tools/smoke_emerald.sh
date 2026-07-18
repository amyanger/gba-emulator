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

exec "$(dirname "$0")/check_golden.sh" "$EMU" "$ROM" "$GOLDEN"
