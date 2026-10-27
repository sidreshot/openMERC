# I1c — Engine client: the Ghoul client system (work log)

Package: SPEC-010/011 (interfaces, who owns the system), SPEC-012 (runtime), SPEC-013 (client side of svc 26/27),
SPEC-002 (renderer `Init` slot, Ghoul entities), SPEC-201/203 (svc 26/27 in the stream and in the connection
sequence), SPEC-310 (view weapon), SPEC-800/803 (menu models), SPEC-AMEND-002.
Directory: `engine/src/client/cl_ghoul.c`, `engine/src/client/header/cl_ghoul.h` (new); minimal hooks marked
`/* I1c */` in other packages' files (listed below).
Build tree: `build-I1c-x86` (x86), `build-I1c-x64` (x64). Test copy: `D:\Games\SoF-test-I1c` (paks hard-linked from
the oracle, executables and DLLs copied).

## State
Summary (2026-10-25 08:4x): done except the items under *Open*. The client owns the client Ghoul system, feeds it
svc 26/27 (original 1.07f servers, OpenMerc's listen and dedicated server, demos), hands it to the renderer, draws
Ghoul entities, the viewer's own shadow model and the view weapon with sway, and the menus draw their Ghoul models
including the `<gpm>` player preview. x86 and x64 build; runtime tested on x86 (x64 stops at sound start: no 64-bit
sound module yet). SPEC-AMEND-006 I1c.1-9 applied the same day (table below). Open: SMG brightness/colour under
hardware lighting (`ghl_light_method 3`; two-pass material now per I1c.9, fading/emulated GL lighting unchecked);
the ref_gl3 front end does not run `drivers/*.cfg`, so OpenMerc's `ghl_light_method` default (2) differs from the
original's configured 3 on this PC; the predicted view weapon (needs client weapon prediction); software menu layer
(`om_menusoft 1`) draws no models.

- 2026-10-22 (session 1): read the specs, journals (I3, I4b, I6m, I6h, I1a, I1b) and the client code; journal
  started. Interrupted before any code.
- 2026-10-25 (session 2): resumed; re-read specs and code; baseline build of the shared tree in `build-I1c-x86`
  OK (reconfigure after the reboot needed nothing).
- 06:0x black-box study of the original client through I4a's capture shim (`tests/I1c/run_orig.ps1`, a copy of
  I6h's harness with timed one-frame call traces `trace:<name>@<s>`): demo playback of I6h's `orig1.dm2`
  (`openmerc-data/I1c/dt1/g00..g24.trace.txt`) and the main menu (`mt1/m1.trace.txt`). Findings in *Observed*.
- 06:2x core implemented and built: `cl_ghoul.c`, `cl_ghoulsys.cpp`, `header/cl_ghoul.h`; hooks in cl_sof.c,
  cl_main.c, cl_entities.c, vid/vid.c, fx/fx_scene.c, CMakeLists. First run (original demo in OpenMerc,
  `shots/I1c/om1_om_199.png`): the view weapon (pistol + hand) is drawn where the original has it. Open: the
  engine now ends with heap corruption (0xC0000374) at quit (I6h's build in the same copy exits 0) - debugging
  with a RelWithDebInfo tree `build-I1c-x86dbg`.
- 06:3x quit crash found (cdb on a hidden desktop, `tests/I1c/run_omdbg.ps1`): static destructors at exit()
  destroy the Ghoul library's file map after its decoded-chunk cache (`~GhbFile` -> `ForgetChunksOf` on freed
  memory). It shows now because the client system holds model files at exit. Fix on the engine side:
  `CL_Shutdown` -> `CL_Ghoul_Shutdown` -> `SV_Ghoul_Shutdown` (OMGhoul_Shutdown while the engine runs; SPEC-010
  "process exit"), after `M_Free`.
- 06:4x menu models drawn (`m_model.c` -> `CL_Ghoul_DrawMenuModel`, `m_draw.c` `MR_ModelBegin/End` = slots
  0xb8/0xb4, `om_menusoft` default now 0). Two Ghoul-library findings from a GL-table trace of the original
  Ghoul system (`ref_omgl` in the original SoF.exe, colours added to I4b's `r_om_ghltrace`,
  `D:\Games\SoF-test-I1c\omgl_ghltrace.txt`), fixed in `ghoul_draw.cpp` (marked I1c): (1) model omni lights of
  an instance light the instances drawn after it in the same draw call (M_BODY_MAIN's four omni lights light the
  globe and rings; without it they were black); (2) every instance draw sets ShadeModel(SMOOTH) first and
  ShadeModel(FLAT) last (the globe was faceted). Main menu pair `shots/I1c/pair_main.png`: mean |diff| 1.3 over
  the frame, 6.9 in the centre (globe rotation phase).
- 06:5x third library finding (answers OPEN_QUESTIONS I3-9): a light node lights only while its visibility
  track shows it (in-view weapons carry a `WORLD_OMNI` muzzle light visible in 4-8 of ~210 frames; without the
  test it lit the walls around the player permanently). Fixed in `ghoul_draw.cpp` (I1c). Demo pairs (I6h's
  original captures of `orig1.dm2` vs OpenMerc at the same demo frames, `shots/I1c/demo1/pair_1..7.png`):
  mean |diff| over the frame 0.5 / 1.8 / 1.7 / 1.6 / 0.8 / 0.8 / 0.8 (I6h's pairs without Ghoul: 6.5-11.5).
  Open: the SMG (frame 253/298) is greyer/bluer than the original's yellow-green (weapon box diff 7.9; pistol
  frames 2.3); not specular (`ghl_specular 0` does not remove the cast) - suspect the library's world-light
  colours (never compared: I4b's checks used the original Ghoul system).
- 07:0x single player on our engine: `map tsr1` in the OpenMerc engine (listen server of I1b, original
  `gamex86.dll` + `player.dll` of the test copy) - the intro cinematic runs with its characters, the street lamp,
  credits and typewriter text (`shots/I1c/sp1_l2.png`, `sp1_l3.png`). The `om_entdump` entity list equals the
  original client's renderer trace of the same scene (I4a's `orig_basic_tsr1.trace.txt`: same origins, angles,
  handles 105/111/176/177, flags 0x401000 / 0x400000). A 68 s run reaches the second cinematic (subway gang,
  Hawk and a soldier, captions: `sp2_t50.png`, `sp2_t60.png`); the player's view weapon (handle 460) exists.
- 07:1x live: the OpenMerc client joined an original 1.07f dedicated server (`tests/I1c/run_omrec.ps1`,
  developer 0, dm/jpndm2), recorded `i1c1.dm2`, captures `shots/I1c/live1_r_1..7.png` (pistol raise, knife,
  firing, scoreboard). That demo played in the original client (timed captures) and in OpenMerc (frame
  captures): pairs `shots/I1c/demo2/pair_1..7.png`, mean |diff| 1.7 / 1.4 / 2.2 / 2.4 / 0.8 / 0.9 / 0.8
  (remaining: animation phase inside the 100 ms frame, world lighting of I4c's renderer).
- 07:2x x64 tree `build-I1c-x64` builds (no warnings in the I1c files). `omghoul_test` x86 after the three
  library fixes: 1225 checks passed, 0 failed. OPEN_QUESTIONS section I1c written (8 items).

- 07:3x `vid_restart` during demo playback (textures released by method 3 before the old renderer goes, uploaded
  again by method 1), the in-game main menu over the level and back: `shots/I1c/vr_sheet.png`.
- 07:4x SMG colour cast investigated (no fix in I1c's scope): with `ghl_light_method 1` OpenMerc matches the
  original (`lm1_*`); own-light counting variants and `gserver LIGHT_SMOOTH 0` change nothing. Root of the
  comparison problem: the original client runs with `ghl_light_method 3`, `ghl_shadows 2` (written by its
  renderer's `exec drivers/*.cfg`; the hosted `ref_gl3` does not exec them), and a Ghoul GL-table trace of the
  original at frame ~250 (new test aid: `omghl.req` request file read by `ref_omgl`, harness entry
  `ghl:<name>@<s>`; `openmerc-data/I1c/gt6/f253.ghl.txt`) shows its hardware-light two-pass material. With the
  same cvars OpenMerc's SMG keeps a blue cast (pistols match): library / renderer material work for I3/I4c,
  recorded as OPEN_QUESTIONS I1c-9. The pairs with the original's cvars: `shots/I1c/demo1lm3/` (frame diff
  0.3-2.3).
- 07:5x `<gpm>` player preview (SPEC-803 §2 Rebuilding): `MG_LoadPlayer` (m_ghoul.cpp: description, menu
  sequence looping, skins per part, `_poff` list, part switches, bolt-on descriptions at their bolts with their
  scale) and the gpm widget in m_model.c keeps its tag options, rebuilds the model on every model/team change,
  attaches it to its `bgbolt` parent (drawn as its bolt-on, one Set3DInMenu pair like the original's trace
  `openmerc-data/I1c/mp1/p1.trace.txt`) and frees it with the widget. Pair `shots/I1c/pair_player.png`: Mullins
  walking with the pistol at the original's place and size (differences: the spin phase of `rotate`, text).
- 08:0x I6m's menu test vectors with the I1c build (`om_menusoft` now 0): 34/34 pass. `om_entdump` of
  `orig1.dm2` against the original's renderer trace of the same demo (`dt1/g00`): own entity, props, the view
  weapon (origin, angles, handle, flags, alpha) identical; one correction from it - an auto-rotating model keeps
  its roll as sent (the original draws the dropped weapon with roll +90, OpenMerc had -90). Both trees rebuilt.
- 08:2x SPEC-AMEND-005 (approved today) I3.9: a light node also needs its part shown - added to the visibility
  test in `ghoul_draw.cpp`. (I3.7 hardware two-pass specular, I3.8 shadow placement: library items, not done here.)
- 08:3x the whole OpenMerc stack in multiplayer: OpenMerc `q2ded` (I1b, original gamex86) + OpenMerc client,
  same scripted session: `shots/I1c/live4_r_1..7.png` / `live3_sheet.png` (pistol, SMG, knife, firing). Harness:
  `run_omrec.ps1 -OmServer` (server started like I1b's svtest.py: own hidden console; under DeskRun's hidden
  desktop `q2ded` stopped after start-up without loading the map), `tests/I1c/omded.py` (server alone).
- RelWithDebInfo tree `build-I1c-x86dbg` (cdb diagnosis) kept; it can be deleted.
- Committed by the coordinator as a584427.

### SPEC-AMEND-006 (approved 2026-10-25), items I1c.1-I1c.9 - applied 2026-10-25 11:xx
| Item | Result | Code | Verify |
|---|---|---|---|
| I1c.1 game clock | changed: advance by the **unclamped** frame time (`renderdelta`, not `cls.rframetime`), frozen also on `freezeworld` and when not active; pull `0.95 e + 0.05 t` on every svc_frame; frame record 0x48/0x4c/0x50 written only when the view is rebuilt (`CL_Ghoul_ViewRebuild` from `CL_AddEntities`, read by fx_scene.c through `CL_Ghoul_FrameRecordClock`) | cl_ghoul.c, cl_main.c, cl_entities.c, fx_scene.c | `orig1.dm2` at `vid_maxfps 10`, `cl_minfps 30` (`tests/I1c/clock_demo_om.cfg`, log `shots/I1c/clk5.log`): clock = game time + 0.100 every dump over 14 s, no pull-backs. tsr1 listen at 10 fps (`clock_om.cfg`, `clk6.log`): the game itself leaves `freezeworld 1` from the level start there - the clock stays at the frozen game time 0.300 (frame time 0.1 s each frame), and after `freezeworld 0` it runs on with the game time (1.443 at 1.4). (In DM demos `freezeworld` is forced 0 by the cheat protection, so the jump test needs a game that is not frozen by itself.) |
| I1c.2 entity values | changed: one helper `CL_Ghoul_EntityValues` for **every** packet entity (cl_sof.c): angles interpolated; auto-rotate keeps pitch/roll (not negated) with the real-time yaw; otherwise, with a secondary model index, pitch/roll negated and yaw + the state's yaw offset quantised to 1/65536 turn in [0, 360); frame with the auto-animation bits 0x1078; old frame = previous state's frame; back-lerp; alpha 1; flags = renderfx (brush entities too). Own Ghoul entity at the view origin the previous rendered frame left (captured before the view is computed) | cl_ghoul.c, cl_sof.c | `rotate_om.cfg` (`rot1.log`): dropped weapon `ang(0 260.997 90)` (roll as sent), props `(-0 180 -0)` (yaw quantised), own entity at the nudged previous view origin. Not done: the per-entity light-output pointer (0x58; Yamagi `entity_t` has no field, nothing reads it) |
| I1c.3 model lights | changed (I3 library, `ghoul_draw.cpp`): a gather pass before the draw (called instance, then bolt-ons depth-first; visited when on, with a sequence and its model or a direct bolt-on's model has light nodes) collects world-flag-0 lights of the whole hierarchy into one shared list (WORLD_* exported there); each instance takes the shared lights whose range reaches its model-space box. World/dynamic lights stay per instance as before | ghoul_draw.cpp | main menu pair `shots/I1c/a6/pair_main.png` 1.2 over the frame (centre 6.9: globe phase). A first version tested the range in world space with the unscaled end range and darkened the scaled menu models (lower-right panel 5.8 instead of 1.9) - the test is now in the receiving instance's model space |
| I1c.4 ShadeModel | matched (the pair is skipped for an off / sequence-less instance: Draw returns before it) | - | - |
| I1c.5 light activity | matched | - | - |
| I1c.6 method-24 specular | changed: `ghl_specular > 0` and `ghl_light_method > 0` read per chunk | cl_ghoul.c | code |
| I1c.7 auto-rotate clock | changed: real-time milliseconds / 10, quantised (`CL_Ghoul_AutoRotateYaw`) | cl_ghoul.c, cl_sof.c | `rot1.log`: 260.997 -> 0.9998 -> 200.995 at real 9817 / 10817 / 12817 ms = 100 degrees per real second (demo at `timescale 0.5`); every rotating entity of a frame has the same yaw |
| I1c.8 one menu pass | matched | - | - |
| I1c.9 hardware two-pass | changed (I3 library): with `ghl_light_method` > 2.5, a specular material with a specular map: pass 1 the map, opaque, black ambient/diffuse, SPECULAR = colour x level, SHININESS = 4 x exponent (kept above 128), negated polygon offset; pass 2 the base texture (ONE, ONE), depth writes off, ambient/diffuse = base, emission, no specular. Without a map one pass with SPECULAR/SHININESS; non-specular materials SPECULAR 0 / SHININESS 0. Also I3.7's level >= 0.051 | ghoul_draw.cpp | default lighting (2) unaffected: `shots/I1c/a6/pair_1..7.png` 0.5 / 1.7 / 1.5 / 1.4 / 0.6 / 0.6 / 0.6. With the original's driver settings (`play_om_lm3.cfg`, `lm3b_*`) the SMG frames are now brighter and bluer than the original (weapon box 13-15 vs 7.9 before; pistol frames 0.4 / 2.5): the original's world GL lights at frame ~250 carry 0.4 x their colour, which matches a light fading in (SPEC-411 6.3 e/T), and OpenMerc's emulated OpenGL lighting of the specular pass is not checked - left for F5 (I3.7) / I4c with the trace `openmerc-data/I1c/gt6/f253.ghl.txt` |

Harness changes: ports 29540 (server), 29541 (client), 29542 (listen server of `run_om.ps1`); original dedicated
servers start with `+exec i1cdev.cfg` (`set developer 1` in `D:\Games\SoF-test-I1c\base`), per the corrected rule.
The OpenMerc engine takes at most 50 command-line words (Quake II `MAX_NUM_ARGVS`): longer `-Extra` lists end
the process before the log opens - put settings into the script.
Observation for I1b / G6b: on `tsr1` with `vid_maxfps 10` the (original) game module leaves `freezeworld 1`
from the level start, so the level time stays at 0.3 until it is cleared; at the normal frame rate it runs.

## Design
| File | Content |
|---|---|
| `client/cl_ghoul.c` | engine side: system ownership, svc 26 -> method 24 (specular flag = `ghl_specular` > 0 and `ghl_light_method` > 0, AMEND-006 I1c.6), svc 27 -> method 25 with the svc_frame number and game time of the packet, methods 23 + 7 at the end of the precache step, 8 in `CL_ClearState`; game clock (SPEC-202 §5 / AMEND-006 I1c.1: 5 % pull per svc_frame, jump beyond 0.5 s, + unclamped frame time per rendered frame, frozen while paused, `freezeworld` or not active; frame-record values set when the view is rebuilt); renderer hand-off (SPEC-002 `Init(NULL, NULL, system)` of `OM_GetSoFRefExports()` after every renderer start, `Init(NULL, NULL, NULL)` before the renderer goes); Ghoul entities and the view weapon (below); menu-model drawing entry; process-exit release of the library |
| `client/cl_ghoulsys.cpp` | C layer over `ghoul_api.h`: methods 23/24/25/7/8/9, instance transform get/set (methods 3/1), menu render-parameter block (SPEC-803 §1: time = menu clock, light (0,0,0), direction (x+50, y+50, 200), specular byte 1, `ghl_no_texture_stack`, fog visibility 1, entity matrix = translation (x, y, 0) and view origin (x, y, -200), or the `forced_camera` part's bolt matrix (mode 7, through parents) and view origin 0; never culled) |
| `client/header/cl_ghoul.h` | interface for the engine (and I1b/I5/I7) |

Entities (SPEC-AMEND-006 I1c.2, `CL_Ghoul_EntityValues` for every packet entity): angles interpolated; auto-rotating (effects 0x2000): pitch and roll as sent, the real-time auto-rotate yaw (I1c.7); otherwise, with a secondary model index, pitch and roll negated and the yaw plus the yaw offset quantised to 1/65536 turn; frame (auto-animation bits 0x1078), old frame = previous state's frame, back-lerp, alpha 1, flags = renderfx. Ghoul entities: frame = the instance handle, | 0x400000; the viewer's own Ghoul entity gets 0x2 and the view origin the previous rendered frame left (shadow only); the view weapon (player-state field 1; `cl_gun`, fov <= 95, remote view type 0, instance exists) is a zeroed entity at the view origin, angles (-pitch, yaw, -roll), flags 0x400015, with the SPEC-310 §13 sway translation applied to the instance transform (re-captured whenever svc 27 changed it). Without a SoF renderer (no `OM_GetSoFRefExports`) or with `om_clghoul 0` the old placeholders are drawn.

Test aids (OpenMerc only): `om_entdump 1` (prints real time, the game clock, frame time, state, `paused`, `freezeworld`, the frame-record clock and the Ghoul entities of the next frame, the
format of the shim traces), `om_ghoulinfo`, `om_clghoul 0`.

## Hooks in other packages' files (all marked `I1c`)
- `cl_sof.c`: svc 26/27 feed, svc_frame -> `CL_Ghoul_FrameParsed`, precache end -> `CL_Ghoul_LoadingDone`,
  Ghoul entities -> `CL_Ghoul_AddEntity` (own entity no longer skipped when Ghoul drawing is active); angles /
  frame / old frame / back-lerp / alpha / flags of every packet entity -> `CL_Ghoul_EntityValues` (AMEND-006).
- `cl_main.c`: `CL_Ghoul_Init` before `VID_Init`, `CL_Ghoul_ClearState` in `CL_ClearState`, `CL_Ghoul_RunFrame`
  before `SCR_UpdateScreen` (unclamped `renderdelta`), `CL_Ghoul_Shutdown` after `M_Free`.
- `cl_entities.c`: `CL_Ghoul_ViewRebuild` before the SoF view is computed, `CL_Ghoul_AddViewWeapon` after the SoF
  packet entities.
- `vid/vid.c`: `CL_Ghoul_RendererStarted` after a renderer's Init, `CL_Ghoul_RendererStopping` before Shutdown.
- `fx/fx_scene.c`: frame record 0x48/0x4c/0x50 = the game-clock values of the last view rebuild (was the client time).
- menus: `m_model.c` (`MM_Draw` for root background models and placed `<ghoul>` widgets), `m_draw.c`
  (`MR_ModelBegin/End`; `om_menusoft` default 0 - the software layer stays selectable and draws no models),
  `m_ghoul.cpp`/`m_ghoul.h` (`MG_Instance`).
- Ghoul library `ghoul_draw.cpp` (I3): model-light gather pass (AMEND-006 I1c.3), ShadeModel(SMOOTH/FLAT), light
  activity (visibility track, part shown), hardware two-pass specular-map material (I1c.9), level >= 0.051.
- `gl3_ghoulgl.c` (I4b, test aid only): `r_om_ghltrace` also logs Color3f/Color4f/Color3fv.
- `gl3_sof.c` (`ref_omgl` only, test aid): a request file `omghl.req` (first line = output path) starts the same
  one-frame Ghoul GL trace (the original's console cannot be reached during demo playback).
- `engine/CMakeLists.txt`: the two new sources.

## For other packages
- I1b / I7: `CL_Ghoul_System()` is the client Ghoul system for the player module's client side (SPEC-003 client
  import 0). A predicted view weapon (SPEC-310 §13 "or the predicted one") needs a setter here once client
  weapon prediction exists. The library is released at client exit by `SV_Ghoul_Shutdown` (I1b's); a dedicated
  server exits without it - the same static-destructor crash can hit `q2ded` when model files are cached at exit
  (I3: `ghoul::G()` file map vs. the chunk cache in `ghoul_codec.cpp`).
- I5: the effects' Ghoul shim (`fx/fx_ghoul.c`) can use `CL_Ghoul_System()` / `CLG_FindInstance(handle)` and the
  game clock `CL_Ghoul_GameClock()`.
- I4c: frame record 0x50 now carries the game clock (0.29-0.3 s ahead of the client time on original servers);
  the renderer's game-clock animations follow it as in the original.

## Build / test
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I1c-x86&& tools\build\build-msvc.cmd x86 Release"
powershell -File tests\I1c\run_om.ps1 -Script tests\I1c\play_om.cfg -Tag om3 -Extra '+set snd_dll OMsnd.dll +set cl_omshotframes "199 253"'
powershell -File tests\I1c\run_orig.ps1 -ClientScript tests\I1c\play_orig.cfg -Tag dt1 -NoServer -TimedShots "trace:g00@8.5,p1@8.51" -QuitAfter 12
powershell -File tests\I1c\run_omrec.ps1 -Script tests\I1c\omrec.cfg -Tag live1      (original server + OpenMerc client)
powershell -File tests\I1c\run_om.ps1 -Script tests\I1c\menu_om.cfg -Tag menu3        (main menu, om_menutour)
powershell -File tests\I1c\run_om.ps1 -Script tests\I1c\listen_long.cfg -NoScriptExec -Extra '+set cl_omexecactive omtest.cfg +map tsr1'
python -I tests\I6h\compare.py <out> <orig.png> <om.png> ...;  python -I tests\I1c\regcmp.py <orig> <om> x0 y0 x1 y1
```
Windows PowerShell 5.1 (`powershell.exe`) for DeskRun.cs. All OpenMerc runs: `-portable`, basedir
`D:\Games\SoF-test-I1c`, user `openmerc-data\I1c\home*\user`, hidden desktop. Original servers: `+set developer 0`.
