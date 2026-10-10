#!/bin/bash
# Downloads the official SDL2 macOS framework (pinned) for release bundles.
# Homebrew's SDL2 only runs on the macOS version it was built on; this one
# runs on macOS 11+. Prints the SDL2.framework path on success.
# Usage: tools/fetch_sdl2_framework.sh <dest dir>
set -euo pipefail

VERSION=2.32.10
SHA256=4a7ac31640d70214e848f994be8a12849c0f97918a7e6c2e27a40036166d1a7f
DEST="$1"
FW="$DEST/SDL2.framework"

if [ -d "$FW" ] && [ "$(cat "$DEST/VERSION" 2>/dev/null)" = "$VERSION" ]; then
    echo "$FW"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'hdiutil detach "$TMP/mnt" >/dev/null 2>&1 || true; rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/sdl.dmg" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL2-$VERSION.dmg"
echo "$SHA256  $TMP/sdl.dmg" | shasum -a 256 -c - >/dev/null
hdiutil attach -nobrowse -readonly -mountpoint "$TMP/mnt" "$TMP/sdl.dmg" >/dev/null 2>&1
rm -rf "$FW"
mkdir -p "$DEST"
cp -R "$TMP/mnt/SDL2.framework" "$FW"
echo "$VERSION" > "$DEST/VERSION"
echo "$FW"
