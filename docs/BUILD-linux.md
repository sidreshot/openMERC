# Building OpenMerc on Linux

Status 2026-10-27: branch `linux-build`, **not merged into `main`**, rebased onto `main`. Both x86_64 and
i386 build, the unit tests pass, and both run game sessions with the stock paks (Debian 13, Mesa llvmpipe
under Xvfb, OpenAL Soft 1.24.2 wave writer):

| Check | x86_64 | i386 |
|---|---|---|
| `openMerc-ded +map tsr1` (then named q2ded), `map kos1`/`nyc1` from the console, `quit` | ok, exit 0 | ok, exit 0 |
| client `+map tsr1` (intro cinematic, 50-60 s) | ok | ok, output WAV 49.7 s: 0 drops, 0 gaps, 0 clicks |
| client `+map trn1` | ok | ok |
| client `+set deathmatch 1 +map dm/nycdm1`: spawn, fire, walk | ok (pistol, HUD) | ok (pistol, HUD) |

Fixed on the way (each its own commit): q2ded aborted with a double free on every quit; i386 builds reported
`x86_64`; GCC release clients aborted on the first inventory event; x86_64 clients crashed on the first
frame (32-bit player-state offsets); i386 GCC corrupted the stack on every movement trace; GCC i386
release builds crashed as soon as a player moved (undefined pointer difference in `SV_SurfaceFromQ2`).
Not yet verified: a real display and sound device, a full campaign map with combat and saves, network play
between machines, the OMsnd focus mute (always off on Linux), and ARM or macOS. Only mode B (OpenMerc's own
modules) can run on Linux: the original game and player modules are 32-bit Windows DLLs.

The fully open build works on Linux with CMake and Ninja. That is the engine, the game and player modules,
the OMsnd sound module on OpenAL Soft, and Ghoul. x86_64 is the main target. i386 builds too, and is the
only Linux target that can check the 32-bit SoF layout asserts. The Windows MSVC x86 build is unchanged
(`tools/build/build-msvc.cmd`, `game/build.cmd`, `player/build.cmd`, `modules/sound/build.cmd`).

## Dependencies

These are the Debian 13 / Ubuntu 24.04+ package names. Other distributions use similar names.

```sh
sudo apt install build-essential cmake ninja-build pkg-config python3 \
    libsdl3-dev libopenal-dev libgl-dev zlib1g-dev
# optional, i386:
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install gcc-multilib g++-multilib libsdl3-dev:i386 libopenal-dev:i386 libgl-dev:i386 zlib1g-dev:i386
```

SDL 3 and OpenAL Soft 1.2x are required. cURL is not needed, because the script configures with `CURL_SUPPORT=OFF` like the Windows build.

## Build

```sh
tools/build/build-linux.sh                 # x86_64 Release
tools/build/build-linux.sh x86_64 Debug
tools/build/build-linux.sh i386            # -m32 -msse2 -mfpmath=sse (SSE2 maths like MSVC x86)
OM_BUILD_DIR=/tmp/om OM_NO_TESTS=1 tools/build/build-linux.sh
```

The script configures and builds four CMake trees under `build-linux-<arch>-<cfg>/` (`engine`, `game`,
`player`, `sound`). It stages the result in `release/` and then runs the unit tests:

| File | From |
|---|---|
| `release/openMerc`, `release/openMerc-ded` | engine (client, dedicated server) |
| `release/ref_gl3.so`, `ref_gles3.so` | engine renderers (`vid_renderer gl3` or `gles3`) |
| `release/OMsnd.so` | `modules/sound` (the engine loads it from its own directory) |
| `release/base/gamex86.so`, `release/base/player.so` | `game`, `player` |
| `release/base/zz_openmerc.pak` | `data/base` via `tools/mkpak.py` |

To build a single tree by hand, configure it and build it like this:

```sh
cmake -S engine -B build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DCURL_SUPPORT=OFF
cmake --build build/engine
```

## Running

Copy the stock game data (`base/pak0.pak` ... from your own installation) into `release/base/`. No game
data comes with OpenMerc. Then start the game:

```sh
cd build-linux-x86_64-Release/release
./openMerc -portable +set vid_renderer gl3   # client
./openMerc-ded -portable +map <map>          # dedicated server
```

`-basedir <dir>` points at another directory that holds `base/`. Loose files are found case-insensitively
(`src/common/filesystem.c`), so stock names in mixed case work.

## Unit tests

| Test | Data | Notes |
|---|---|---|
| `engine/sofpm_test` | none | SPEC-314 player movement |
| `player/pm_unit` | none | the SPEC-601 32-bit object-layout checks run only where `sizeof(void*) == 4` |
| `game/g3tests [fixture dir]` | fixtures from `gen_mockmodels.py` | skips without fixtures (`OM_G3_FIXTURES=<dir>` in the script) |
| `engine/src/ghoul/omghoul_test [data dir] [render dir]` | extracted Ghoul data | checks of stock models skip without data (`OM_GHOUL_DATA=<dir>` in the script) |

Without data, `omghoul_test` reports 142 passed, 0 failed: the network replication of the stock MESO
character and the player model and shadow checks print `SKIP` instead. On i386 without SSE2 maths, x87 excess precision makes two checks
fail (`test_runtime.cpp:250`, `test_savenet.cpp:116`). That is why the script builds i386 with `-msse2 -mfpmath=sse`.

## Notes on the port

- `engine/src/common/header/om_platform.h` provides `OM_EXPORT`, `OM_ABI32_ASSERT` and the MSVC CRT names
  (`_stricmp`, `_snprintf`, ...) on other systems. game, player and sound include it.
- On ELF systems the modules export only their `.def` names, through a version script that
  `tools/build/OMModuleExports.cmake` generates. They link with `-Bsymbolic` and `--no-undefined`.
- OMsnd looks up engine functions by name with `dlsym(RTLD_DEFAULT, ...)`. The client executable exports
  them through `src/common/sof_exports.dynlist`. Because the engine is built with
  `-fvisibility=hidden`, `src/common/header/shared.h` redeclares them with default visibility when
  `OM_SOF_ELF_EXPORTS` is defined (client target only). The wrapped ones use their `SoFExp_*` names.
- The SoF binary layout asserts (`game_api.h`, `refapi.h`, `ghoul_api.h`, `sof_layout.h`, `sof_game.h`,
  `player_api.h`, `g_main.cpp`, `om_player_api.h`, `pm_local.h`) apply to 32-bit x86 with MSVC or GCC/Clang
  (`__i386__`). On x86_64 the engine and the open modules use the natural layout of the same declarations,
  so module code must use `offsetof` on a member list, never a fixed 32-bit offset (`PS_*` in
  `player/src/pm_local.h`, `EDICT_*` in `om_player_api.h`).
- i386 GCC/Clang: a function returning a struct pops the hidden return pointer itself (MSVC does not).
  Callbacks that return a struct must be declared by value on that target (`sof_pmove_t.trace`).
- The tools that need Windows are not built on Linux: the interface probes, the tracing proxies, `omptrace`
  and `omscript_test`. Neither are the OpenAL32.dll copy steps.
- OMsnd's focus mute (`s_focusmute`) is Windows-only for now. On Linux the window counts as always visible.
