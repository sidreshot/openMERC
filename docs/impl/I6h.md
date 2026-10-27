# I6h — Client view, HUD, input, demos (work log)

Specs: SPEC-310 (view/HUD), SPEC-311 (demos), SPEC-312 (input); SPEC-302 for cvars/commands.
Files (owner I6h): `engine/src/client/cl_view.c`, `cl_screen.c`, `cl_input.c`, `cl_keyboard.c`, `cl_inventory.c`,
`engine/src/client/input/` (sdl3.c, sdl2.c, gyro.c, winkeys.c), new: `cl_demo.c`, `cl_hud.c`, `cl_sofscreen.c`,
`cl_sofview.c`, `cl_strings.c`, headers `header/keyboard.h`, `header/sofclient.h`, `input/header/winkeys.h`.
Tests: `tests/I6h/`. Test copy: `D:\Games\SoF-test-I6h` (paks hard-linked from the oracle; OpenMerc install in
`om\`, user dir `<openmerc-data>\I6h\home\user`).

## State (2026-10-22 09:40)
Built and tested in the shared tree (build dir `build-I6h-x86`).
- **Input (SPEC-312)**: key numbers = Windows VK codes (`keyboard.h`, Yamagi K_ names kept as aliases), the
  full key table (names, 4 layout columns by sp_language, flags M/C/T); key event rules 1–7; console line
  editing; chat line; bindings (`unbind` = empty string, no single-char names, menu-bound keys locked in menus);
  Windows keyboard read from the window messages through SDL's message hook (`input/winkeys.c`: VK + extended
  flag, RSHIFT by scan code 0x36, RCTRL/RALT/KP_ENTER by the extended flag, Alt+Enter only with
  `win_noaltenter 0`); scancode->VK table on other systems; MOUSEnDBL; mouse per §8 (integer sensitivities,
  zoom factor, invert, filter), read every frame, movement share added to the next command; focus changes
  release all keys with `-` commands; buttons with the held flag; user command per §10 once per packet frame;
  cinematic freeze/skip; `console` cvar gates toggleconsole; force-feedback commands/messages (no device).
- **Demos (SPEC-311)**: `record`/`stop` and `demomap` (played inside the client: one block per 100 ms, one per
  frame with timedemo; demo flag -> pm_type 5 view, lerp 1, any key = Escape, toggleconsole = disconnect,
  `changing` only closes menus). Server demos (`serverrecord`) stay server code (I1b).
- **View (SPEC-310 §12–13)** in `cl_sofview.c`: SoF player-state record per frame (all fields of SPEC-202 §4),
  view values, weapon kick, night-vision jitter, remote camera, fov rules, near clip, cl_cinematicfreeze,
  fps smoothing / effect detail, deathmatch byte copied to the client cvar (SPEC-203), view-weapon sway
  (computed; not applied — no Ghoul view weapon in the client yet).
- **HUD / screen (SPEC-310 §1–11, 15)**: SoF screen order; interface layer (scope, panels with tray animation,
  big digits, health/armor/meter, countdown, mission messages, rank/spectator, CTF and control flags, debug
  target), crosshair, labels (range, cone, world trace), layout language, centre/caption/typewriter/credit
  prints, string-package prints (SPEC-201 §34), pause, loading menu + progress, graphs, net icon,
  cl_showfps/cl_showlocation/cl_stats prints; ignore/unignore/listignore/addfave/remfave/terrain/cloud/cl_touch.
  Drawing through I4a's `OM_GetSoFRefExports` (SPEC-002 2D slots).
- **Strings**: `cl_strings.c` uses I1a's runtime (`common/sof_strings.c`): registers the client packages of
  configstrings 1464–1492 (owner 1), releases them per level, guards lookups.
- **Test aids (OpenMerc only, documented in the code)**: `omshot <name>`, `cl_omshotframes`, `cl_omshotprefix`
  (captures before the swap through slot 0xd4), `cl_omshowframe`, `cl_omdemoquit`, `cl_omexecactive <cfg>`
  (exec once in the level), `cl_omkey <key> <down> [ext]` (key event without OS input).

Edits in files of other packages (minimal, marked `OpenMerc (I6h)`): `cl_sof.c` (player-state record, baseline /
ghoul / uncompressed-frame demo hooks, layout, countdown, cinprintf, caption, sp prints, nameprint ignore,
namecols, force_con_notify, `CL_SoF_SetDemoProtocol`), `cl_parse.c` (demo write after a SoF message),
`cl_network.c` (demo frame feed, no disconnect packets for demos, `changing` rules), `cl_main.c` (demo
commands, SoF cheat-cvar list), `cl_entities.c` (SoF view values, lerp 1 in demos), `cl_console.c`
(toggleconsole policy), `engine/CMakeLists.txt` (source lines). I did not edit any I1a file: the
`cmdparser.c` / `sv_user.c` / `sv_pmove.c` / fx / gl3 build breaks of the morning were fixed by their owners
(while waiting I used a private snapshot copy in my scratchpad, now deleted).

## Evidence (data under `<openmerc-data>\`)
- Original demo played in OpenMerc: `shots\I6h\pairs\orig1\pair_1..7.png` (original playback | OpenMerc playback
  | x4 difference) at demo frames 199/253/298/351/437/479/520 of `I6h\demos\orig1.dm2` (recorded by the 1.07f
  client on a 1.07f dedicated server). HUD matches (panels, digits, health bar, rank, scoreboard layout incl.
  portrait and texts); differences: world lighting (renderer), no Ghoul view weapon, tray animation phase.
- OpenMerc-recorded demo `I6h\demos\om1.dm2` (OpenMerc client on the original server): the original 1.07f client
  plays it (`shots\I6h\origom1b_p_*.png`, log without errors); 40 of the 41 header + ghoul blocks are
  byte-identical to the original's recording of the same level (`I6h\om1-vs-orig1.txt`; block 13 differs in one
  baseline of the server session). Two fixes came from this: the deathmatch byte (SPEC-203 copy into the client
  cvar) and the welcome configstring 1493, which the client empties after printing it (SPEC-201 §38, cl_sof.c).
  Pairs of the OpenMerc demo in both engines: `shots\I6h\pairs\om1\`, numbers in `I6h\pairs-om1.txt`.
- Config compatibility: `tests/I6h/keys.cfg` (execs the oracle's config.cfg, binding/name/fave/ignore/ff/cvar
  checks) gives the same 129 output lines in the original and in OpenMerc (`I6h\keys-orig.txt`, `keys-om.txt`).
- User commands (SPEC-312 test vectors 1–3): `I6h\moves.check.txt` (relay capture decoded with sofnet.py):
  forward 200; diagonal 156/124; run bit 0x20 with cl_run 1, clear with +speed; +use +moveright -> lean -1,
  side 0, bit 0x02; +leanright +leanleft -> lean +1; msec ~30 (cl_maxfps 30).
- Key rules (SPEC-312 §4, tests 5 and 10): `shots\I6h\keyevents.log` (tests/I6h/keyevents.cfg with cl_omkey).

## Running engines
Every OpenMerc run goes through `tests/I6h/run_om.ps1` (always `-portable -basedir D:/Games/SoF-test-I6h
+set user <openmerc-data>/I6h/home/user`, hidden desktop). The Documents\YamagiQ2 write of
05:13 was a "Yamagi Quake II v8.71pre x86_64" build (stdout.txt there), not an I6h run (I6h builds are x86).

## Build / test
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I6h-x86&& tools\build\build-msvc.cmd x86 Release"
powershell -File tests\I6h\run_om.ps1 -Script tests\I6h\play_om.cfg -Tag omplay -Extra '+set cl_omshotframes "199 253"'
powershell -File tests\I6h\run_omrec.ps1 -Script tests\I6h\omrec.cfg -Tag omrec      (original server + OpenMerc)
powershell -File tests\I6h\run_omrec.ps1 -Script tests\I6h\moves.cfg -Tag moves -Proxy; python -I tests\I6h\movecheck.py ...
powershell -File tests\I6h\run_orig.ps1 ...   (original client / server, see its header)
python -I tests\I6h\compare.py <outdir> <orig.png> <om.png> ...;  python -I tests\I6h\dm2dump.py <file.dm2>
```
Windows PowerShell 5.1 (`powershell.exe`) is needed for DeskRun.cs (System.Drawing).

## Needs from other packages
- **I1a**: walking the command and alias lists for TAB cycling (SPEC-313 §10), e.g.
  `const char *Cmd_CompleteNext(const char *prefix, const char *previous)`; until then commands are offered only
  as the first match, cycling covers cvars.
- **I1b**: the server registers `demomap`; the client replaces it at CL_Init (demos play in the client).
- **I6m**: the `menu` command should request a cinematic skip instead of opening a menu while
  `CL_CinematicFreezeActive()` (SPEC-312 §11; `CL_RequestCinematicSkip()`, header/sofclient.h).
- **Player-module host / Ghoul client (I1b, I4b)**: `Key_PendingWeaponCommand()` for the weapon-prediction hook
  (SPEC-310 §14); `CL_SoF_ViewWeaponSway()` for the view-weapon transform (SPEC-310 §13); the view weapon
  itself (player-state field 1).

## Remaining
- sdl2.c is not converted (only the SDL3 backend is built by default).
- Labels: darkness rule (lighting value unknown, SPEC-310 open question) and entity occlusion; joystick axes per
  §9 (SDL gamepad path kept, SoF joy_ cvars registered for configs); configstring loading progress
  (0.33 + 0.33 i/1415) not drawn per configstring.
- Spec gaps: see OPEN_QUESTIONS.md, I6h section.

## SPEC-AMEND-005 (2026-10-25)
Applied by package F5; item-by-item results, code changes and tests in `docs/impl/F5.md` (section I6h).
