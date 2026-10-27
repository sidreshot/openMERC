# CLEANUP: dead upstream and legacy code removed (branch `cleanup`)

Status: 27 Oct 2026, C team. Base main `f1a62b2`. Rule: remove what neither mode A (`D:\Games\OpenMerc`: our
`openMerc.exe` with the retail `gamex86.dll`/`player.dll` and our `OMsnd.dll`) nor mode B uses. Keep everything mode A
still loads or calls. See also `MODEB-GAPS.md` section 3.1.

## Removed

| part | why it was dead | check |
|---|---|---|
| `engine/src/client/refresh/soft` (`ref_soft`) | upstream software renderer, no SoF render path (no SPEC-002 exports, no Ghoul models) | `vid_renderer soft` falls back to `ref_gl3` (log: "Library ref_soft.so cannot be found!", then "Successfully loaded ref_gl3") |
| `engine/src/client/refresh/gl1` (`ref_gl1`, `ref_gles1`) | the same for GL1: an unchanged upstream import (one commit, the import). GL3 does everything it did plus the SoF path | `vid_renderer gl1` falls back to gl3; DM nycdm1 session in game |
| `gl1_stereo*` cvars and the 2-frame stereo loop in `SCR_UpdateScreen` | only GL1 implemented stereo; `GL3_BeginFrame` ignores the separation | one frame per update, as before with `gl1_stereo 0` (the default and the owner's config) |
| soft-only branches in `cl_cin.c` (palette reset, minimal colour) and `cl_screen.c` (damage-tracked tile clear) | ran only with `vid_renderer soft`; `cl_cin.c` also defaulted `vid_renderer` to "gl1" | the borders are redrawn every frame, the GL path that was already used |
| `engine/Makefile`, `engine/.github/` | upstream build and CI. The Makefile has none of the OpenMerc sources (Ghoul, SoF client/HUD, pmove, OMsnd) and cannot build OpenMerc; a nested `.github` never runs | CMake is the only build (`tools/build/build-msvc.cmd`, `docs/BUILD-linux.md`) |
| `engine/src/game` (Quake II baseq2 game, 21 files, monsters) and `Sys_GetGameAPI`/`Sys_UnloadGame` | built as `release/base/game.dll` but never loaded: `server/sv_game.c` loads the SoF `gamex86` itself and nothing called `Sys_GetGameAPI` | `release/base` is no longer created; SP tsr1 and DM sessions run |
| `engine/stuff/{fngen,mmgen,winbuild,misc}` | table generators for the removed Q2 game, MinGW release scripts, upstream Dockerfile/uncrustify | not referenced |

The legal scan exclusion for `engine/src/client/refresh/gl1/*` is gone with the directory.

## Kept, and why

| part | why it stays |
|---|---|
| `ref_gl3`, `ref_gles3` | gl3 is the renderer; GLES3 is the same source (ARM, Linux) |
| `ref_omgl` (`OM_SOFREF`: `gl3_sof.c`, `gl3_win32.c`, `gl3_ghoulgl.c`) | the oracle harnesses (`tests/I1c/run_orig.ps1`, `gl3/test/i4c/run_sof.ps1`, `ghltrace`) drive the original SoF.exe through it |
| `sof_exports.c` | OMsnd (mode A ships it) looks the engine functions up in the executable (`snd_engine.c`) |
| `snd_host.c` (SPEC-004 sound host) | loads OMsnd.dll in mode A |
| SPEC-001 game ABI (`sv_game.c`, `game_api.h`), `sv_imptrace.c`, pure checksums, `sof_locks.c`, force feedback | the retail `gamex86.dll`/`player.dll` of mode A use them |
| GameSpy (`gamespy.c`, `gamespy_chat.c`, `sv_gamespy.c`, `cl_serverlist.c`) | the server list and query protocol of SPEC-mandated MP; the client uses community masters (README) |
| `common/pmove.c` (Q2) next to `sof_pmove.c` | medium risk (MODEB-GAPS 3.1); needs its own change with movement tests |
| `engine/doc/` | upstream user docs; still mention `ref_soft`/`ref_gl1` (to be rewritten, not deleted) |
| `engine/stuff/icon`, `engine/stuff/cmake/modules` | the Windows icon resource and `FindSDL2.cmake` are used |

D:\Games\OpenMerc still has an old `ref_gl1.dll` from earlier deploys. It defaults to gl3 (`vid_renderer "gl3"` in its config),
so a new build does not need that file. It was left as it is (owner's install).

## Result

153 files, 89 452 lines deleted (`git diff --stat f1a62b2`). MSVC x86 and Linux x86_64 build; gates in the report.
Not changed: `tsr1` with `deathmatch 1` ends in "Index: sound overflow" also before this branch (SP maps in DM mode; fixed afterwards on branch `snd-robust`, see KNOWN_ISSUES).
