#!/bin/bash
# Packs a Linux build into a single-file AppImage with SDL2 bundled.
# Downloads pinned linuxdeploy/appimagetool (checked by SHA-256).
# Usage: tools/make_appimage.sh <gba_emulator binary> <version> <out dir>
set -euo pipefail

BIN="$1"
VERSION="$2"
OUT="$3"
REPO="$(cd "$(dirname "$0")/.." && pwd)"

LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_SHA256=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
curl -fsSL -o "$WORK/linuxdeploy" "$LINUXDEPLOY_URL"
curl -fsSL -o "$WORK/appimagetool" "$APPIMAGETOOL_URL"
echo "$LINUXDEPLOY_SHA256  $WORK/linuxdeploy" | sha256sum -c -
echo "$APPIMAGETOOL_SHA256  $WORK/appimagetool" | sha256sum -c -
chmod +x "$WORK/linuxdeploy" "$WORK/appimagetool"

# CI runners have no FUSE; run the tools by extracting them.
export APPIMAGE_EXTRACT_AND_RUN=1

# linuxdeploy wants the icon file named after the desktop entry's Icon= key.
cp "$REPO/res/icon/app.png" "$WORK/gba_emulator.png"
"$WORK/linuxdeploy" --appdir "$WORK/AppDir" \
    --executable "$BIN" \
    --desktop-file "$REPO/res/gba_emulator.desktop" \
    --icon-file "$WORK/gba_emulator.png"
mkdir -p "$WORK/AppDir/usr/share/doc/gba_emulator"
cp "$REPO/LICENSE" "$REPO/fonts/Inter-LICENSE.txt" "$WORK/AppDir/usr/share/doc/gba_emulator/"

mkdir -p "$OUT"
ARCH=x86_64 VERSION="$VERSION" "$WORK/appimagetool" "$WORK/AppDir" \
    "$OUT/gba_emulator-$VERSION-Linux-x86_64.AppImage"
