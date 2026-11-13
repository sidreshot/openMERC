#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build the fully open OpenMerc on macOS (Apple silicon) with CMake + Ninja (docs/BUILD-macos.md):
#   engine (openMerc, openMerc-ded, ref_gl3, omlight, Ghoul, sofpm_test, omghoul_test),
#   game module (gamex86.dylib, g3tests), player module (player.dylib, pm_unit), sound module (OMsnd.dylib on
#   OpenAL Soft), OpenMerc's data archive (zz_openmerc.pak), staged into one directory, then the unit tests.
# The same steps as tools/build/build-linux.sh; the modules are named .dylib here.
#
# usage: tools/build/build-macos.sh [arm64] [Release|Debug]
# env:   OM_BUILD_DIR      build root (default <repo>/build-macos-<arch>-<cfg>)
#        OM_NO_TESTS=1     skip the unit tests
#        OM_JOBS           parallel jobs (default: the number of cores)
#        OM_SDL3_PREFIX    SDL3 (default: Homebrew's, brew install sdl3)
#        OM_OPENAL_PREFIX  OpenAL Soft (default: Homebrew's, brew install openal-soft)
#        OM_LIBOMP_PREFIX  LLVM's OpenMP runtime for omlight (default: Homebrew's, brew install libomp);
#                          without it omlight is built for one thread
# Output: <build root>/release/ - openMerc, openMerc-ded, omlight, ref_gl3.dylib, OMsnd.dylib, base/gamex86.dylib,
#         base/player.dylib, base/zz_openmerc.pak. Add the game data (pak files) yourself; none is shipped.
set -euo pipefail

ARCH=${1:-arm64}
CFG=${2:-Release}
REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT=${OM_BUILD_DIR:-$REPO/build-macos-$ARCH-$CFG}
JOBS=${OM_JOBS:-$(sysctl -n hw.ncpu)}

case "$ARCH" in
  arm64|aarch64) ARCH=arm64 ;;
  *) echo "unknown arch '$ARCH' (arm64 only; Intel Macs are not built or tested)" >&2; exit 2 ;;
esac

for t in cmake ninja python3 codesign; do
  command -v "$t" >/dev/null || { echo "missing tool: $t (see docs/BUILD-macos.md)" >&2; exit 1; }
done

brew_prefix() { command -v brew >/dev/null && brew --prefix "$1" 2>/dev/null || true; }
SDL3_PREFIX=${OM_SDL3_PREFIX:-$(brew_prefix sdl3)}
OPENAL_PREFIX=${OM_OPENAL_PREFIX:-$(brew_prefix openal-soft)}
LIBOMP_PREFIX=${OM_LIBOMP_PREFIX:-$(brew_prefix libomp)}
[ -d "$SDL3_PREFIX/lib/cmake/SDL3" ] || {
  echo "SDL3 not found (brew install sdl3, or set OM_SDL3_PREFIX)" >&2; exit 1; }
[ -d "$OPENAL_PREFIX/lib/cmake/OpenAL" ] || {
  echo "OpenAL Soft not found (brew install openal-soft, or set OM_OPENAL_PREFIX)" >&2; exit 1; }
# Homebrew keeps OpenAL Soft out of the default search paths (macOS has its own, old OpenAL framework)
export CMAKE_PREFIX_PATH="$SDL3_PREFIX:$OPENAL_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

OMP=()
if [ -f "$LIBOMP_PREFIX/include/omp.h" ]; then
  # CMake does not find Homebrew's libomp for Apple's clang by itself
  OMP=("-DOpenMP_C_FLAGS=-Xclang -fopenmp" "-DOpenMP_C_LIB_NAMES=omp" "-DOpenMP_C_INCLUDE_DIR=$LIBOMP_PREFIX/include"
       "-DOpenMP_CXX_FLAGS=-Xclang -fopenmp" "-DOpenMP_CXX_LIB_NAMES=omp" "-DOpenMP_CXX_INCLUDE_DIR=$LIBOMP_PREFIX/include"
       "-DOpenMP_omp_LIBRARY=$LIBOMP_PREFIX/lib/libomp.dylib")
else
  echo "note: no libomp (brew install libomp) - omlight, the lightmap re-bake, is built for one thread"
fi

configure() { # <source> <build> [extra args]
  local src=$1 bld=$2; shift 2
  cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE="$CFG" -DCMAKE_OSX_ARCHITECTURES="$ARCH" "$@"
  cmake --build "$bld" -j "$JOBS"
}

echo "== engine";  configure "$REPO/engine"        "$OUT/engine" -DCURL_SUPPORT=OFF ${OMP[@]+"${OMP[@]}"}
echo "== game";    configure "$REPO/game"          "$OUT/game"
echo "== player";  configure "$REPO/player"        "$OUT/player"
echo "== sound";   configure "$REPO/modules/sound" "$OUT/sound"

echo "== staging"
REL=$OUT/release
mkdir -p "$REL/base"
cp -f "$OUT/engine/release/openMerc" "$OUT/engine/release/openMerc-ded" "$OUT/engine/release/omlight" "$REL/"
cp -f "$OUT/engine/release/ref_gl3.dylib" "$REL/"
cp -f "$OUT/sound/OMsnd.dylib" "$REL/"
cp -f "$OUT/game/gamex86.dylib" "$OUT/player/player.dylib" "$REL/base/"
python3 "$REPO/tools/mkpak.py" "$REPO/data/base" "$REL/base/zz_openmerc.pak"

if [ "${OM_NO_TESTS:-0}" != "1" ]; then
  echo "== unit tests"
  rc=0
  "$OUT/engine/sofpm_test" || rc=1
  "$OUT/engine/cvar_test" || rc=1
  "$OUT/player/pm_unit" || rc=1
  # the next two need fixtures from the game data and skip (G3) or skip their data checks (Ghoul) without it
  if [ -x "$OUT/game/g3tests" ]; then "$OUT/game/g3tests" ${OM_G3_FIXTURES:-} || rc=1; fi
  # without OM_GHOUL_DATA=<extracted ghoul data> the checks of stock models skip
  "$OUT/engine/src/ghoul/omghoul_test" "${OM_GHOUL_DATA:-$OUT/no-ghoul-data}" "$OUT/ghoul-renders" || rc=1
  [ $rc -eq 0 ] || { echo "unit tests FAILED" >&2; exit 1; }
fi
echo "Built into $REL"
