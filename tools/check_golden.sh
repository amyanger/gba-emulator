#!/usr/bin/env bash
# Run the emulator headless against a ROM and diff its hash output
# against a checked-in golden file.
# Usage: tools/check_golden.sh <emulator> <rom> <golden> [frames] [input-script]
# frames defaults to the golden's line count (pass "" to keep that default
# while giving an input script). input-script is passed as --input-script.
set -euo pipefail

if [[ $# -lt 3 || $# -gt 5 ]]; then
    echo "Usage: $0 <emulator> <rom> <golden> [frames] [input-script]" >&2
    exit 2
fi

EMU="$1"
ROM="$2"
GOLDEN="$3"
FRAMES="${4:-$(wc -l < "$GOLDEN" | tr -d ' ')}"

INPUT_ARGS=()
if [[ $# -eq 5 ]]; then
    INPUT_ARGS=(--input-script "$5")
fi

ACTUAL="$(mktemp)"
trap 'rm -f "$ACTUAL"' EXIT

# ${arr[@]+...} keeps an empty array safe under `set -u` on bash 3.2.
"$EMU" "$ROM" --headless --frames "$FRAMES" --hash-out "$ACTUAL" ${INPUT_ARGS[@]+"${INPUT_ARGS[@]}"}

if ! diff -u "$GOLDEN" "$ACTUAL"; then
    echo "GOLDEN MISMATCH: $ROM (golden=$GOLDEN)" >&2
    exit 1
fi

echo "OK: $ROM matches $GOLDEN ($FRAMES frames)"
