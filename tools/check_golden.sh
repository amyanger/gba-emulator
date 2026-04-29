#!/usr/bin/env bash
# Run the emulator headless against a ROM and diff its hash output
# against a checked-in golden file.
# Usage: tools/check_golden.sh <emulator> <rom> <golden> [frames]
set -euo pipefail

if [[ $# -lt 3 || $# -gt 4 ]]; then
    echo "Usage: $0 <emulator> <rom> <golden> [frames]" >&2
    exit 2
fi

EMU="$1"
ROM="$2"
GOLDEN="$3"
FRAMES="${4:-$(wc -l < "$GOLDEN")}"

ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

"$EMU" "$ROM" --headless --frames "$FRAMES" --hash-out "$ACTUAL"

if ! diff -u "$GOLDEN" "$ACTUAL"; then
    echo "GOLDEN MISMATCH: $ROM (golden=$GOLDEN)" >&2
    exit 1
fi

echo "OK: $ROM matches $GOLDEN ($FRAMES frames)"
