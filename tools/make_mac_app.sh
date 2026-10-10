#!/bin/bash
# Creates a double-clickable "GBA Emulator.app" (default: on the Desktop).
# The app rebuilds the emulator from this repo on every launch, so it always
# runs the latest code, then asks which ROM in roms/ to play.
#
# Usage: tools/make_mac_app.sh [destination dir]
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$HOME/Desktop}"
APP="$DEST/GBA Emulator.app"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cat > "$APP/Contents/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>GBA Emulator</string>
    <key>CFBundleIdentifier</key><string>local.gba-emulator.launcher</string>
    <key>CFBundleExecutable</key><string>launch</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

cp "$REPO/res/icon/app.icns" "$APP/Contents/Resources/AppIcon.icns"

# Unquoted REPO expands now; everything else is escaped to expand at launch.
cat > "$APP/Contents/MacOS/launch" <<EOF
#!/bin/bash
REPO="$REPO"
BUILD="\$REPO/cmake-build-release"
LOGDIR="\$HOME/Library/Logs/GBA Emulator"
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/sbin:\$PATH"
mkdir -p "\$LOGDIR"

alert() {
    osascript -e "display alert \"GBA Emulator\" message \"\$1\" as critical" >/dev/null
}

if [ ! -d "\$REPO" ]; then
    alert "Can't find the project at \$REPO."
    exit 1
fi

osascript -e 'display notification "Building latest code..." with title "GBA Emulator"'
if ! { cmake -S "\$REPO" -B "\$BUILD" -DCMAKE_BUILD_TYPE=Release &&
       cmake --build "\$BUILD" --target gba_emulator -j "\$(sysctl -n hw.ncpu)"; } \\
       > "\$LOGDIR/build.log" 2>&1; then
    alert "Build failed. Details are in ~/Library/Logs/GBA Emulator/build.log"
    open -a Console "\$LOGDIR/build.log"
    exit 1
fi

ROMS=\$(cd "\$REPO/roms" 2>/dev/null && ls *.gba 2>/dev/null)
if [ -z "\$ROMS" ]; then
    alert "No .gba files found in \$REPO/roms"
    exit 1
fi
DEFAULT=\$(echo "\$ROMS" | grep -m1 '^emerald.gba\$' || echo "\$ROMS" | head -1)
LIST=\$(echo "\$ROMS" | sed 's/.*/"&"/' | paste -sd, -)
ROM=\$(osascript -e "choose from list {\$LIST} with title \"GBA Emulator\" with prompt \"Pick a ROM:\" default items {\"\$DEFAULT\"}")
[ "\$ROM" = "false" ] && exit 0

ARGS=("\$REPO/roms/\$ROM" --scale 3)
[ -f "\$REPO/bios/gba_bios.bin" ] && ARGS+=(--bios "\$REPO/bios/gba_bios.bin")
cd "\$REPO"
exec "\$BUILD/gba_emulator" "\${ARGS[@]}" 2> "\$LOGDIR/emulator.log"
EOF

chmod +x "$APP/Contents/MacOS/launch"
# Make Finder pick up the new icon right away.
touch "$APP"
echo "Created $APP"
