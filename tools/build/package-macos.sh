#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Pack a macOS arm64 staging directory (the release/ folder from tools/build/build-macos.sh)
# into openMERC-<version>-macos-arm64.tar.gz + .sha256.
#
# The binaries stay linked to Homebrew (sdl3, openal-soft, libomp under /opt/homebrew).
# Those libraries are not copied into the archive. dist/README-macos.txt says so.
#
# usage: tools/build/package-macos.sh [release dir] [out dir]
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
REL=${1:-$REPO/out/release}
DEST=${2:-$REPO/dist-out}
[ -x "$REL/openMerc" ] || { echo "no build in $REL (run tools/build/build-macos.sh)" >&2; exit 1; }
[ -f "$REL/base/gamex86.dylib" ] || { echo "no base/gamex86.dylib in $REL" >&2; exit 1; }

VER=$(sed -n 's/^#define OM_VERSION "\(.*\)"/\1/p' "$REPO/engine/src/common/header/om_version.h")
NAME=openMERC-$VER-macos-arm64

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ROOT=$STAGE/$NAME
mkdir -p "$ROOT"
cp -R "$REL"/. "$ROOT/"
cp "$REPO/dist/README-macos.txt" "$ROOT/README.txt"
cp "$REPO/LICENSE" "$REPO/THIRD_PARTY.md" "$ROOT/"

mkdir -p "$DEST"
DEST=$(cd "$DEST" && pwd)
if tar --version 2>/dev/null | grep -q 'GNU tar'; then
  tar --owner=0 --group=0 --numeric-owner --sort=name -C "$STAGE" -czf "$DEST/$NAME.tar.gz" "$NAME"
else
  tar -C "$STAGE" -czf "$DEST/$NAME.tar.gz" "$NAME"
fi
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$DEST" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
else
  (cd "$DEST" && shasum -a 256 "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
fi
echo "Packed $DEST/$NAME.tar.gz"

# openMERC.app: the programs, ref_gl3 and OMsnd in Contents/MacOS, openMERC's base/ (modules, zz_openmerc.pak) in
# Contents/Resources (the launcher gives it as -basedir; nothing but code may sit in MacOS for codesign).
# Started by dist/macos/openMERC-launcher.sh, which asks for the game folder once and keeps everything openMERC
# writes in ~/Library/Application Support/openMERC.
# Packed as openMERC-<version>-macos-arm64-app.zip (ditto keeps the bundle intact) + .sha256.
APPNAME=$NAME-app
APP=$STAGE/app/openMERC.app
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$REL/openMerc" "$REL/openMerc-ded" "$REL/omlight" "$REL/ref_gl3.dylib" "$REL/OMsnd.dylib" "$APP/Contents/MacOS/"
cp -R "$REL/base" "$APP/Contents/Resources/"
cp "$REPO/dist/macos/openMERC-launcher.sh" "$APP/Contents/MacOS/openMERC-launcher"
chmod 755 "$APP/Contents/MacOS/openMERC-launcher"
MINOS=$(otool -l "$REL/openMerc" | awk '/LC_BUILD_VERSION/ { f = 1 } f && $1 == "minos" { print $2; exit }')
sed -e "s/@VERSION@/$VER/g" -e "s/@MINOS@/${MINOS:-13.0}/g" "$REPO/dist/macos/Info.plist.in" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" >/dev/null

# icon: the Windows icon's largest image (256 px) at every size iconutil takes
ICONSET=$STAGE/openMERC.iconset
mkdir -p "$ICONSET"
sips -s format png "$REPO/engine/stuff/icon/openmerc.ico" --out "$STAGE/icon.png" >/dev/null
for s in 16 32 128 256; do
  sips -z $s $s "$STAGE/icon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
  sips -z $((s * 2)) $((s * 2)) "$STAGE/icon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/openMERC.icns"

# ad-hoc signature over the whole bundle (arm64 needs one; the binaries are re-signed in place)
codesign --force --sign - "$APP/Contents/MacOS/"*.dylib "$APP/Contents/Resources/base/"*.dylib \
  "$APP/Contents/MacOS/openMerc" "$APP/Contents/MacOS/openMerc-ded" "$APP/Contents/MacOS/omlight"
codesign --force --sign - "$APP"

cp "$REPO/dist/README-macos.txt" "$STAGE/app/README.txt"
cp "$REPO/LICENSE" "$REPO/THIRD_PARTY.md" "$STAGE/app/"
(cd "$STAGE/app" && ditto -c -k --norsrc --noextattr . "$DEST/$APPNAME.zip")
(cd "$DEST" && shasum -a 256 "$APPNAME.zip" > "$APPNAME.zip.sha256")
echo "Packed $DEST/$APPNAME.zip"
