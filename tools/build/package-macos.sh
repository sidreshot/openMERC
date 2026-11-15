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
