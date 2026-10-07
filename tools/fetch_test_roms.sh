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

# tonc demos
# Prebuilt tonc v1.4.2 binaries (J. Vijn, 2013-03-24). The zip has not
# changed since 2013, so the sha256 pins it; a mismatch aborts the fetch.
TONC_URL="https://www.coranac.com/files/tonc-bin.zip"
TONC_SHA256="f172fc9308eb030ae8d3479872e9f3521b7f0cae3a64b0cd5f526b7b4b1650ba"
TONC_DEMOS="m3_demo pageflip win_demo dma_demo m7_ex oacombo prio_demo"

TONC_TMP="$(mktemp -d)"
trap 'rm -rf "$TONC_TMP"' EXIT
curl -fsSL "$TONC_URL" -o "${TONC_TMP}/tonc-bin.zip"
if command -v sha256sum >/dev/null 2>&1; then
    TONC_GOT="$(sha256sum "${TONC_TMP}/tonc-bin.zip" | cut -d' ' -f1)"
else
    TONC_GOT="$(shasum -a 256 "${TONC_TMP}/tonc-bin.zip" | cut -d' ' -f1)"
fi
if [[ "$TONC_GOT" != "$TONC_SHA256" ]]; then
    echo "tonc-bin.zip sha256 mismatch: got $TONC_GOT, want $TONC_SHA256" >&2
    exit 1
fi
for demo in $TONC_DEMOS; do
    unzip -p "${TONC_TMP}/tonc-bin.zip" "bin/${demo}.gba" > "${DEST}/tonc_${demo}.gba"
done

echo "Fetched test ROMs into ${DEST}"
