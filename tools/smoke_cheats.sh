#!/usr/bin/env bash
# End-to-end cheat check on the real game: boot a temp copy of the Emerald
# test save with a CodeBreaker code that sets the first party Pokemon's level
# (unencrypted byte at gPlayerParty+0x54 = 0x02024540) to 100, then read the
# party back through the Emerald Companion's decoder (--game-dump). Also checks
# that the save was backed up before the cheat ran.
# Skips when the commercial ROM or test save is missing (CI).
# Usage: tools/smoke_cheats.sh [emulator] [rom] [save]
set -euo pipefail

EMU="${1:-build/gba_emulator}"
ROM="${2:-roms/emerald.gba}"
SAV="${3:-${ROM}.sav}"

if [[ ! -f "$ROM" || ! -f "$SAV" ]]; then
    echo "SKIP: $ROM or $SAV not present (commercial ROM, not distributed)"
    exit 0
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
ln -s "$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")" "$WORK/emerald.gba"
cp "$SAV" "$WORK/emerald.gba.sav"

# Title -> Continue -> overworld.
cat > "$WORK/boot.input" <<'EOF'
400 press START
404 release START
600 press A
604 release A
700 press A
704 release A
1000 press A
1004 release A
EOF

run() {
    "$EMU" "$WORK/emerald.gba" --headless --frames 1500 --input-script "$WORK/boot.input" \
        --game-dump "$WORK/$1" >/dev/null 2>&1
}

run before.txt
BEFORE="$(grep -m1 '^party 0 ' "$WORK/before.txt" | grep -o ' lv[0-9]*' || true)"
if [[ "$BEFORE" == " lv100" ]]; then
    echo "FAIL: party slot 0 is already level 100; pick another save"
    exit 1
fi

cat > "$WORK/emerald.gba.cht" <<'EOF'
[CodeBreaker]
Level 100 (first Pokemon)
32024540 0064
EOF
"$EMU" "$WORK/emerald.gba" --cheat-report | sed 's/^/  /'
run after.txt
AFTER="$(grep -m1 '^party 0 ' "$WORK/after.txt" | grep -o ' lv[0-9]*' || true)"

if [[ "$AFTER" != " lv100" ]]; then
    echo "FAIL: expected party 0 lv100 with the cheat, got '${AFTER# }' (before:${BEFORE})"
    exit 1
fi
if [[ ! -f "$WORK/emerald.gba.sav.cheatbak1" ]]; then
    echo "FAIL: no save backup (emerald.gba.sav.cheatbak1) was made before the cheat ran"
    exit 1
fi
if ! cmp -s "$SAV" "$WORK/emerald.gba.sav.cheatbak1"; then
    echo "FAIL: the backup differs from the save it was taken from"
    exit 1
fi
echo "OK: cheat set party 0 from${BEFORE} to lv100; save backed up first"
