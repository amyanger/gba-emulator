#!/usr/bin/env bash
# Fetch a pinned set of public test ROMs into the requested directory.
# Usage: tools/fetch_test_roms.sh <dest-dir>
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <dest-dir>" >&2
    exit 2
fi

DEST="$1"
mkdir -p "$DEST"

# Pinned jsmolka/gba-tests commit. Bump deliberately.
JSMOLKA_REF="a7113b67e63f83a9b321696ddd7042ccfad6c881"
BASE="https://raw.githubusercontent.com/jsmolka/gba-tests/${JSMOLKA_REF}"

curl -fsSL "${BASE}/arm/arm.gba"       -o "${DEST}/jsmolka_arm.gba"
curl -fsSL "${BASE}/thumb/thumb.gba"   -o "${DEST}/jsmolka_thumb.gba"
curl -fsSL "${BASE}/memory/memory.gba" -o "${DEST}/jsmolka_memory.gba"

echo "Fetched test ROMs into ${DEST}"
