# G6b — Script host, script_runner, cinematic state, script savegame wiring (game/src/scriptglue/)

Implementation journal (C team). Specs: SPEC-750 §1, §3.1; SPEC-751 §1, §2; SPEC-752 §1.2 steps 4–5, §4.4;
SPEC-AMEND-003 (host contract: most recent MOVE destination, vehicle done without AI). The VM is G6a's om_script
library (`game/src/script/`, not changed here).

## State (2026-10-22 09:50)
- Done and linked into gamex86 (`game\build.cmd` in `build-G6b-x86` builds the whole tree at 09:45): Host on G18's
  entities, `script_runner`, worldspawn runner used at ClientBegin, frame hook, cinematic byte sync (exports
  0x54/0x58 stay G18's `game.cinematic`), freeze byte, AI → G2 (`om_ai::*`, `g_report_ai_done`), HELI/VEHICLE → G4,
  script chunk entry points for the level save.
- In-game comparison with the original (single player through the proxy, see Test): identical where the script
  system alone decides; divergences come from other packages (below).
- Not done: savegame round trip (G18's WriteLevel/ReadLevel are still empty); the damage → camera-off hook is not
  placed (damage is G3's file now).

## Results (original 1.07f module vs OpenMerc, same map, headless client, frames relative to an anchor)
| Run | Anchor | Result |
|---|---|---|
| `vec_main` (own test program as `tsr1/tsr1pre`, `test/g6b_vectors.py`) | "G6B start" | **36/36 lines identical**: V2 (wait 0.33 → +3 frames, wait 0/-1 no yield), V3 (MOVE 96 u in 3 s: signal after 31 frames), V8 (early signal kept, 0x80 clears), V7 (int 0 / float 2.5 from a float field), SPEC-751 ROF (25 frames, end signal +25), SOUND variants (channel/volume/default channel 2, one frame after the print), caption 0x6417, PRECACHE_SOUND configstring, MUSIC 3 → byte 6, SET_VIEW_YAW 45 → delta_angles 8192, CONSOLE `say`, cinematic freeze 1 for 10 frames, V5 fn8(2,15) range 3…14 over 300 draws, V4 (use while waiting consumed, after SUSPEND continues, after EXIT nothing), AMEND-003 2e (first done event at +11 frames, signal 2) |
| `vec_runaway` | — | both: `Game Error: Runaway loop for script OR more than 1000 instructions` (V1) |
| `tsr1` stock intro | caption 0x6418 | **250 frames identical** (remote view type/entity, remote origin/angles every frame of the ROF-driven `introcam1`, captions 0x6417…0x641f at the same frames). Then the original continues at +245 (camera `introcam6`); OpenMerc waits: G2 never reports the AI-done of `ai(swatleader, 51, (-88 0 0), signal)` (G6b passes done id 1, `g6b_debug 1` log). Also missing: the train horn of `use(introltrain)` (`misc_generic_train`, G7) |
| `trn1` stock intro | caption 0x7500 | **757/757 lines identical** (≈38 s of camera path and captions) |
| `arm1` start | first frame | the scripted car (entity 68, `arm/cardrive.os` MOVEs) identical frame by frame; entity 40 (an AI character) differs (AI) |
| `dm/jpndm2` | — | runs, no script errors |
Captures: `openmerc-data\g6b\{orig,om,omfull}_*\` (`cap.jsonl`, `timeline.txt`, `rel.txt`, `sof.log`, `proxy.log`).

## Files
| File | What |
|---|---|
| `g_scriptglue.h` | public interface: spawn functions, frame/level/client hooks, cinematic state, savegame entry points |
| `sg_local.h` | internal: `sg::GameHost` (om_script::Host), entity helpers |
| `sg_host.cpp` | Host on G18's entities: engine services, lookups, the 21 entity fields of SPEC-750 4.4, built-in spawn (8.1), MOVE destination/snap (11, AMEND-003 2e), triggers (0x1F/0x20), view yaw (0x25), music (0x2A), sounds, prints/captions (SPEC-751 4), AI → G2, HELI/VEHICLE → G4, cinematic side effects (SPEC-751 1.2/1.4) |
| `sg_glue.cpp` | the level's ScriptSystem; `script_runner` (SPEC-750 1.1–1.3), worldspawn runner, hooks, cinematic byte sync, freeze byte, camera end-of-frame (SPEC-751 2.6), damage → camera off, script chunks through imports 85/86, `g6b_debug` |
| `sg_shims.cpp` | weak fallbacks (MSVC x86 `/alternatename`) for the G2/G4 functions; used only when those are not linked |
| `test/g6b_run.py` | G18's harness (`game/test/runtest.py`) on `D:\Games\SoF-test-G6b`, port 27961, output `openmerc-data\g6b\<tag>\`; `--sp` single player through the proxy; `--files DIR` copies a tree into the run's user directory (loose files there override the paks) |
| `test/spproxy/` | single-player proxy module (own test code): loads the module under test, force-sets `deathmatch 0`, `maxclients 1` (import #90) before its Init, logs cinematic state changes (export 0x54) and engine SetCinematicState calls (0x58) |
| `test/g6b_timeline.py` | capture → frame-keyed timeline (entity moves/appear/remove, sounds with names, captions, prints, configstrings set in play, stufftext, player-state delta angles, remote view/origin/angles, fov, cinematic freeze, music, pm_type); `--rel REGEX` frames relative to an anchor, `--noents`, `--drop`, `--diff A B` |
| `test/os_asm.py`, `test/g6b_vectors.py` | assembler for test `.os` programs and the SPEC-750/751 vector programs (`main`, `runaway`) |
| `test/g6b_linkany.py` | test-only link of gamex86 from the objects that compile while siblings are mid-edit (no-op fallbacks for what is missing; `--exclude PKG`) |

## Hook calls placed in shared files (each marked `/* G6b */`)
- `g_spawn.cpp`: include; spawn-table entry `script_runner`; `ScriptGlue_LevelStart()` after `SaveClientData()`;
  SP_worldspawn's runner → `SG_WorldScriptRunner(s)` (was `SP_Placeholder(s)`).
- `g_main.cpp`: include; `ScriptGlue_RunFrame()` after `ItemsFrame()`, before `CheckDMRules()`; `ScriptGlue_Shutdown()`.
- `p_client.cpp`: include; `ScriptGlue_ClientBegin(ent)` at the start of ClientBegin (all modes; it only queues a use).
- `g_local.h`: edict_t block `/* G6b */ int sg_script; vec3_t sg_movedest; /* end G6b */`.
- `game/CMakeLists.txt`: `set(OM_SCRIPT_NO_TESTS ON)`, `add_subdirectory(src/script)`, `target_link_libraries(gamex86
  PRIVATE om_script)`.
- `docs/DEVIATIONS.md`: script text print row; `docs/specs/OPEN_QUESTIONS.md`: section "G6b script wiring".

## Needs from G18
1. **Savegames**: in WriteLevel/ReadLevel call `ScriptGlue_WriteLevel()` / `ScriptGlue_ReadLevel()` after the entity
   chunks and before the AI chunks (SPEC-752 1.2 steps 4–5). The EDCT callback encoding must know `runner_use`
   (sg_glue.cpp, static; G6b exports it when G18 publishes how package callbacks are registered).
2. `skip_stubs.cpp` (OM_GAME_SKIP_PACKAGES) is behind G18's own hooks (GM_*, GG_*, Roster_CallSpawn …); it still
   lists the G6b functions correctly.
3. Observations from the comparison runs (G18's areas): the original's first player state has music byte 2, OpenMerc
   0; RunFrame returns `framenum × 0.1`, the original 0.1 at frame 0 and 4.3 at frame 41 (proxy log); the original
   registers `dm_generic` in single player too; one crash at 09:34 in PutClientInServer → player.dll (read of
   NULL), not reproduced in the later runs.
4. `func_remote_camera` is G18's (g_sofents.cpp). Black box: identical to the original on the tsr1/trn1 intro cameras
   (aim at activation even with spawnflag 8, ROF-driven origin/angles every frame, view type 2, effects 0x4000 on the
   watcher). Left from SPEC-751 §2: damage deactivation (2.6; `ScriptGlue_ClientDamaged` does it through the
   camera's use), the "camera counter < 1" rule of 2.6 (`ScriptGlue_ClientEndFrame`; G18's end-of-frame uses
   `client->camera` instead), 2.3 step 2 finds the other camera by the remote view entity number.
5. **Harness**: since ~09:00 `+set developer 1` makes the original show a modal "Developer Mode" box on this PC (hidden
   window, the server never starts; SPEC-301 B1 step 7). `game/test/runtest.py` passes developer 1; `g6b_run.py`
   rewrites it to 0.

## Needs from G2
- tsr1: `ai(swatleader, 51, (-88 0 0), signal=sig1)` (kind 1 move, flags 0x0c, done id 1 at 28.5 s) is never reported
  done, so `nycintro` waits forever; the original continues about 2 s later. (`--set "g6b_debug 1"` prints commands,
  done ids and reports.)

## Needs from G3
- Call `ScriptGlue_ClientDamaged(targ)` when a client takes damage (SPEC-751 2.6: the camera the client looks through
  is switched off). G3's own type-4 camera damage rule is already in g3_damage.cpp.

## Build
`set OM_BUILD_DIR=<repo>\build-G6b-x86` then `game\build.cmd Release` (cmd.exe).
While a sibling package does not compile: `python game\src\scriptglue\test\g6b_linkany.py [--exclude roster]` →
`build-G6b-x86\any\gamex86.dll` (test only). Proxy: `game\src\scriptglue\test\spproxy\build_spproxy.cmd`.

## Test
```
python game\src\scriptglue\test\g6b_vectors.py main <openmerc-data>\g6b\vec_main
python game\src\scriptglue\test\g6b_run.py --sp [--files <openmerc-data>\g6b\vec_main] --dll orig|om|<dll> --map tsr1 --tag NAME --seconds 45 [--set "g6b_debug 1"]
python game\src\scriptglue\test\g6b_timeline.py <capdir>\cap.jsonl --out rel.txt --rel "G6B start" --noents --drop "^ps start"
python game\src\scriptglue\test\g6b_timeline.py --diff orig\rel.txt om\rel.txt
```

## Deviations
- Script PRINT of a text prints it literally; the original uses it as a format string (DEVIATIONS.md).
- Unspecified points, implemented as noted (OPEN_QUESTIONS G6b): move-done snap relinks; instant MOVE/ROTATE moves no
  follower (SPEC-750 OQ 3); a SOUND without an entity plays nothing (as SPEC-001 #22 with NULL).

## SPEC-AMEND-005 (2026-10-25)
Applied by package F5; item-by-item results, code changes and tests in `docs/impl/F5.md` (section G6b).
