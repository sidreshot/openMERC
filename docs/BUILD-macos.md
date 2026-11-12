# Building OpenMerc on macOS

Status 2026-11-12 (0.2.3): the fully open build compiles and links on Apple silicon (arm64), the unit tests pass,
and the client runs a game session with the stock paks on the OpenGL renderer `ref_gl3`. That is the engine, the
dedicated server, `ref_gl3`, the game and player modules, the OMsnd sound module on OpenAL Soft, omlight and Ghoul.

| Check | arm64 (macOS 27.2, Apple clang 21, SDL 3.4, OpenAL Soft 1.25) |
|---|---|
| `tools/build/build-macos.sh` from a clean tree | ok |
| `sofpm_test`, `pm_unit`, `omghoul_test` (no stock data), `g3tests` (fixtures skipped) | 360/0, all passed, 142/0, skipped |
| symbol table of `gamex86.dylib` (savegame callbacks) against `nm` | 3978 symbols, every address agrees |
| omlight with Homebrew's libomp 23.1: `-map maps/tsr1.bsp`, 17 threads | ok, 14 s (170 s of processor time), 1.3 MB of lighting written |
| client `+map tsr1` with `ref_gl3` in a 2560x1440 window: level loads (2.5 s), 1100 frames, two screenshots, `quit` | ok, exit 0; the pictures show the level, the weapon and the HUD |

That one session is all that was run: one level, no input, about 14 seconds. Not verified: play with keyboard and
mouse, sound heard by a person, saving and loading, the other levels, full screen, the frame rate, the 0.2.3
effects one by one, a re-bake started by the game itself (`r_om_relight`), omlight built without OpenMP on a
map (it compiles; one thread), the dedicated server, Intel Macs, a package for players. Only mode B
(OpenMerc's own modules) can run on a Mac: the original game and player modules are 32-bit Windows DLLs.

macOS has no OpenGL newer than 4.1 and Apple marks OpenGL as deprecated. `ref_gl3` asks for a 3.2 core context
and GLSL 1.50; macOS gives it `4.1 Metal` (Apple's OpenGL on top of Metal). The driver logged one warning during the
level load: `unit 9 GLD_TEXTURE_INDEX_2D is unloadable and bound to sampler type (Float) - using zero texture`
(a shader samples a texture unit that has no complete texture at that moment); nothing was visibly wrong.

## Running

The game data is not part of the build. Give the folder that holds `base/pak0.pak`:

```sh
cd build-macos-arm64-Release/release
./openMerc +set basedir "/path/to/Soldier of Fortune" +set vid_renderer gl3
```

`-basedir "/path/to/Soldier of Fortune"` does the same.

## Dependencies

Xcode's command line tools (`xcode-select --install`) and, from [Homebrew](https://brew.sh):

```sh
brew install cmake ninja sdl3 openal-soft libomp
```

`libomp` is optional. Apple's clang has no OpenMP runtime of its own; without `libomp`, omlight (the lightmap
re-bake) is built for one thread and a bake takes longer. cURL is not needed, because the script configures
with `CURL_SUPPORT=OFF` like the Windows and Linux builds.

## Build

```sh
tools/build/build-macos.sh                 # arm64 Release
tools/build/build-macos.sh arm64 Debug
OM_BUILD_DIR=/tmp/om OM_NO_TESTS=1 tools/build/build-macos.sh
```

The script configures and builds four CMake trees under `build-macos-arm64-<cfg>/` (`engine`, `game`, `player`,
`sound`), stages the result in `release/` and then runs the unit tests, like `tools/build/build-linux.sh`:

| File | From |
|---|---|
| `openMerc`, `openMerc-ded`, `omlight`, `ref_gl3.dylib` | `engine/` |
| `OMsnd.dylib` | `modules/sound/` |
| `base/gamex86.dylib` | `game/` |
| `base/player.dylib` | `player/` |
| `base/zz_openmerc.pak` | `data/base/` packed by `tools/mkpak.py` |

`openMerc` and `OMsnd.dylib` use the SDL 3 and OpenAL Soft libraries of Homebrew where they are installed; a
package that brings its own copies does not exist yet.

## What differs from Linux

- **Module names.** The engine loads `gamex86.dylib`, `player.dylib` and `OMsnd.dylib` on macOS (`.so` on Linux).
- **Engine functions for OMsnd.** OMsnd looks up engine functions by name. A Mach-O executable exports its
  functions of default visibility without a list, so the client only needs `OM_SOF_ELF_EXPORTS` (which gives them
  that visibility) and not the ELF dynamic list.
- **Savegame symbol table.** `game/tools/om_symtab.py --ld64` reads the map of Apple's linker (`-map`). Writing
  the table into the linked module ends the linker's signature, and macOS on Apple silicon does not load a module
  whose signature does not match, so the build signs `gamex86.dylib` again (ad hoc, no identity).
- **Player movement on arm64.** clang joins a multiplication and the following addition or subtraction into one
  instruction with one rounding on arm64. SPEC-314 rounds after every operation, so `sof_pmove.c` switches this
  off (`#pragma STDC FP_CONTRACT OFF`); `sofpm_test` W15b fails without it.
- **omlight without OpenMP.** When the compiler has no OpenMP, omlight is built for one thread.

## Continuous integration

The `macos` job of `.github/workflows/build.yml` runs `tools/build/build-macos.sh` with the unit tests on every
push and pull request and keeps `release/` as a workflow artifact. It makes no release package.
