#!/bin/bash
# Fails unless a built "GBA Emulator.app" is self-contained: SDL2 comes from
# inside the bundle, nothing points at Homebrew, it targets macOS 11, the
# signature verifies, and the plist declares .gba files.
# Usage: tools/check_mac_bundle.sh "<path>/GBA Emulator.app" [rom.gba]
set -euo pipefail

APP="$1"
ROM="${2:-}"
BIN="$APP/Contents/MacOS/GBA Emulator"
fail() { echo "FAIL: $*"; exit 1; }

[ -x "$BIN" ] || fail "missing executable $BIN"
[ -f "$APP/Contents/Resources/app.icns" ] || fail "missing icon"
[ -f "$APP/Contents/Frameworks/SDL2.framework/Versions/A/SDL2" ] || fail "SDL2.framework not bundled"
[ -L "$APP/Contents/Frameworks/SDL2.framework/SDL2" ] || fail "framework symlinks were flattened"
if otool -L "$BIN" | tail -n +2 | grep -E "/opt/homebrew|/usr/local"; then fail "links outside the bundle"; fi
otool -L "$BIN" | grep -q "@rpath/SDL2.framework" || fail "SDL2 not linked through @rpath"
otool -l "$BIN" | grep -A2 LC_RPATH | grep -q "@executable_path/../Frameworks" || fail "rpath missing"
minos=$(otool -l "$BIN" | awk '/LC_BUILD_VERSION/{f=1} f && /minos/{print $2; exit}')
[ "$minos" = "11.0" ] || fail "binary targets macOS $minos, want 11.0"
codesign --verify --deep --strict "$APP" || fail "signature does not verify"
plutil -lint "$APP/Contents/Info.plist" >/dev/null || fail "Info.plist invalid"
/usr/libexec/PlistBuddy -c "Print :CFBundleDocumentTypes:0:CFBundleTypeExtensions:0" \
    "$APP/Contents/Info.plist" | grep -qx gba || fail ".gba document type missing"

if [ -n "$ROM" ]; then
    loaded=$(DYLD_PRINT_LIBRARIES=1 "$BIN" "$ROM" --headless --frames 1 2>&1 >/dev/null | grep "SDL2.framework" || true)
    echo "$loaded" | grep -q "$APP/Contents/Frameworks" || fail "SDL2 loaded from elsewhere: $loaded"
fi
echo "OK: $APP"
