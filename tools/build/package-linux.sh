#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Pack a Linux build (tools/build/build-linux.sh) into openMERC-<version>-linux-<arch>.tar.gz + .sha256.
# The archive holds one folder, openMERC/, to unpack inside the Soldier of Fortune folder (next to base/).
#
# usage: tools/build/package-linux.sh [build root] [out dir]
# env:   OM_SDL3_LIB    libSDL3.so.0 to bundle (default: the system one; best an SDL build that loads its
#                       X11/Wayland/audio back ends at run time)
#        OM_OPENAL_LIB  libopenal.so.1 to bundle (default: the system one)
#        OM_NO_BUNDLE=1 bundle neither; the system libraries are used
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT=${1:-$REPO/build-linux-x86_64-Release}
DEST=${2:-$OUT}
REL=$OUT/release
[ -x "$REL/openMerc" ] || { echo "no build in $REL (run tools/build/build-linux.sh)" >&2; exit 1; }

VER=$(sed -n 's/^#define OM_VERSION "\(.*\)"/\1/p' "$REPO/engine/src/common/header/om_version.h")
case "$(readelf -h "$REL/openMerc" | sed -n 's/^ *Machine: *//p')" in
  *X86-64*) ARCH=x86_64 ;;
  *80386*)  ARCH=i386 ;;
  *) echo "unknown architecture of $REL/openMerc" >&2; exit 1 ;;
esac
NAME=openMERC-$VER-linux-$ARCH

find_lib() { # <soname>: path of the system library
  "$(command -v ldconfig || echo /sbin/ldconfig)" -p | awk -v n="$1" -v a="$ARCH" '$1 == n && (a == "i386" ? $0 !~ /x86-64/ : $0 ~ /x86-64/) { print $NF; exit }'
}

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
P=$STAGE/openMERC
mkdir -p "$P/base"
cp "$REL/openMerc" "$REL/openMerc-ded" "$REL/OMsnd.so" "$P/"
for r in ref_gl3.so ref_gles3.so; do [ -f "$REL/$r" ] && cp "$REL/$r" "$P/"; done
cp "$REL/base/gamex86.so" "$REL/base/player.so" "$REL/base/zz_openmerc.pak" "$P/base/"
cp "$REPO/LICENSE" "$REPO/THIRD_PARTY.md" "$P/"
cp "$REPO/dist/README-linux.txt" "$P/README.txt"
install -m 755 "$REPO/dist/openMERC.sh" "$P/openMERC.sh"
strip --strip-unneeded "$P/openMerc" "$P/openMerc-ded" "$P"/*.so "$P"/base/*.so

if [ "${OM_NO_BUNDLE:-0}" != "1" ]; then
  mkdir -p "$P/lib"
  sdl=${OM_SDL3_LIB:-$(find_lib libSDL3.so.0)}
  oal=${OM_OPENAL_LIB:-$(find_lib libopenal.so.1)}
  [ -f "$sdl" ] && [ -f "$oal" ] || { echo "libSDL3.so.0 or libopenal.so.1 not found" >&2; exit 1; }
  cp -L "$sdl" "$P/lib/libSDL3.so.0"
  cp -L "$oal" "$P/lib/libopenal.so.1"
  strip --strip-unneeded "$P"/lib/*.so.*
  cat > "$P/lib/LICENSE-SDL3.txt" <<'TXT'
libSDL3.so.0: Simple DirectMedia Layer 3, https://github.com/libsdl-org/SDL

Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
TXT
  cat > "$P/lib/LICENSE-OpenAL-Soft.txt" <<'TXT'
libopenal.so.1: OpenAL Soft, https://github.com/kcat/openal-soft
Copyright (C) Chris Robinson and the OpenAL Soft contributors.

OpenAL Soft is free software under the GNU Library General Public License,
version 2 or (at your option) any later version (LGPL-2.0.txt next to this
file, also https://www.gnu.org/licenses/old-licenses/lgpl-2.0.txt). The source code of
this unmodified library is at https://github.com/kcat/openal-soft (also in the
openal-soft package of your distribution). It is a separate shared library:
replace lib/libopenal.so.1 with any compatible build, or delete it to use the
system one.
TXT
  lgpl=${OM_LGPL_TEXT:-/usr/share/common-licenses/LGPL-2}
  [ -f "$lgpl" ] || { echo "LGPL-2 text not found (OM_LGPL_TEXT)" >&2; exit 1; }
  cp "$lgpl" "$P/lib/LGPL-2.0.txt"
fi

mkdir -p "$DEST"
tar -C "$STAGE" --owner=0 --group=0 --numeric-owner --sort=name -czf "$DEST/$NAME.tar.gz" openMERC
(cd "$DEST" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
echo "Packed $DEST/$NAME.tar.gz"
cat "$DEST/$NAME.tar.gz.sha256"
