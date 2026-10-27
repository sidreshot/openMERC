#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the fully open OpenMerc on Linux with CMake + Ninja (docs/BUILD-linux.md):
#   engine (openMerc, openMerc-ded, ref_gl3/ref_gles3, Ghoul, sofpm_test, omghoul_test),
#   game module (gamex86.so, g3tests), player module (player.so, pm_unit), sound module (OMsnd.so on
#   OpenAL Soft), OpenMerc's data archive (zz_openmerc.pak), staged into one directory, then the unit tests.
#
# usage: tools/build/build-linux.sh [x86_64|i386] [Release|Debug]
# env:   OM_BUILD_DIR  build root (default <repo>/build-linux-<arch>-<cfg>)
#        OM_NO_TESTS=1 skip the unit tests
#        OM_JOBS       parallel jobs (default: nproc)
# Output: <build root>/release/ - openMerc, openMerc-ded, ref_*.so, OMsnd.so, base/gamex86.so, base/player.so,
#         base/zz_openmerc.pak. Add the game data (pak files) yourself; none is shipped.
set -euo pipefail

ARCH=${1:-x86_64}
CFG=${2:-Release}
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT=${OM_BUILD_DIR:-$REPO/build-linux-$ARCH-$CFG}
JOBS=${OM_JOBS:-$(nproc)}

case "$ARCH" in
  x86_64|amd64) ARCH=x86_64; FLAGS="" ;;
  i386|x86)
    # SSE2 maths like MSVC's x86 default (/arch:SSE2); x87 excess precision changes Ghoul results
    ARCH=i386; FLAGS="-m32 -msse2 -mfpmath=sse"
    export PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR:-/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig} ;;
  *) echo "unknown arch '$ARCH' (x86_64 or i386)" >&2; exit 2 ;;
esac

for t in cmake ninja pkg-config python3; do
  command -v "$t" >/dev/null || { echo "missing tool: $t (see docs/BUILD-linux.md)" >&2; exit 1; }
done

configure() { # <source> <build> [extra args]
  local src=$1 bld=$2; shift 2
  cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE="$CFG" \
    -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$FLAGS" -DCMAKE_SHARED_LINKER_FLAGS="$FLAGS" "$@"
  cmake --build "$bld" -j "$JOBS"
}

echo "== engine";  configure "$REPO/engine"        "$OUT/engine" -DCURL_SUPPORT=OFF -DGLES3_RENDERER=ON
echo "== game";    configure "$REPO/game"          "$OUT/game"
echo "== player";  configure "$REPO/player"        "$OUT/player"
echo "== sound";   configure "$REPO/modules/sound" "$OUT/sound"

echo "== staging"
REL=$OUT/release
mkdir -p "$REL/base"
cp -f "$OUT/engine/release/openMerc" "$OUT/engine/release/openMerc-ded" "$REL/"
for r in ref_gl3.so ref_gles3.so; do
  [ -f "$OUT/engine/release/$r" ] && cp -f "$OUT/engine/release/$r" "$REL/"
done
cp -f "$OUT/sound/OMsnd.so" "$REL/"
cp -f "$OUT/game/gamex86.so" "$OUT/player/player.so" "$REL/base/"
python3 "$REPO/tools/mkpak.py" "$REPO/data/base" "$REL/base/zz_openmerc.pak"

if [ "${OM_NO_TESTS:-0}" != "1" ]; then
  echo "== unit tests"
  rc=0
  "$OUT/engine/sofpm_test" || rc=1
  "$OUT/player/pm_unit" || rc=1
  # the next two need fixtures from the game data and skip (G3) or report data-only failures (Ghoul) without it
  if [ -x "$OUT/game/g3tests" ]; then "$OUT/game/g3tests" ${OM_G3_FIXTURES:-} || rc=1; fi
  if [ -n "${OM_GHOUL_DATA:-}" ]; then
    "$OUT/engine/src/ghoul/omghoul_test" "$OM_GHOUL_DATA" "$OUT/ghoul-renders" || rc=1
  else
    echo "omghoul_test: set OM_GHOUL_DATA=<extracted ghoul data> to run it (docs/BUILD-linux.md)"
  fi
  [ $rc -eq 0 ] || { echo "unit tests FAILED" >&2; exit 1; }
fi
echo "Built into $REL"
