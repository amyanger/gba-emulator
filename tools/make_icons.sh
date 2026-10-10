#!/bin/bash
# Regenerates the committed app icons in res/icon/ from tools/mac_app_icon.swift.
# macOS only (needs swift, sips, iconutil). Run after changing the drawing.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/res/icon"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"

MASTER="$TMP/icon_1024.png"
swift "$REPO/tools/mac_app_icon.swift" "$MASTER"

# macOS .icns: every size Finder and the Dock ask for.
ICONSET="$TMP/app.iconset"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z $s $s "$MASTER" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) "$MASTER" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$OUT/app.icns"

# Linux: one 256 px PNG (a standard hicolor size).
sips -z 256 256 "$MASTER" --out "$OUT/app.png" >/dev/null

# Windows .ico: PNG-compressed entries (valid since Vista), packed below.
ICO_SIZES="16 24 32 48 64 128 256"
for s in $ICO_SIZES; do
    sips -z $s $s "$MASTER" --out "$TMP/ico_$s.png" >/dev/null
done
python3 - "$OUT/app.ico" $(for s in $ICO_SIZES; do echo "$TMP/ico_$s.png"; done) <<'EOF'
import struct, sys
out, pngs = sys.argv[1], sys.argv[2:]
blobs = [open(p, "rb").read() for p in pngs]
header = struct.pack("<HHH", 0, 1, len(blobs))
offset = 6 + 16 * len(blobs)
entries = b""
for blob in blobs:
    w, h = struct.unpack(">II", blob[16:24])  # PNG IHDR width/height
    entries += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(blob), offset)
    offset += len(blob)
with open(out, "wb") as f:
    f.write(header + entries + b"".join(blobs))
EOF

echo "Wrote $OUT/app.icns, app.ico, app.png"
