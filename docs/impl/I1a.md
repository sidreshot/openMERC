# I1a — Engine core (work log)

Package: filesystem (SPEC-300), startup and config (SPEC-301), cvars and commands (SPEC-302), savegame
container (SPEC-303), string packages (SPEC-131, SPEC-313 §1), engine misc (SPEC-313), sound module host
(SPEC-004, SPEC-AMEND-001).

## Announcements for other packages (shared headers / behaviour changes)
- `engine/src/common/header/shared.h`: `cvar_t` now has the SoF 1.07f layout (SPEC-002): a change callback
  `changed` at offset 0x10, so `modified`/`value`/`next` sit at 0x14/0x18/0x1c; `default_string` and
  `hash_next` follow at the end. All field names are unchanged (source compatible); rebuild everything that
  includes shared.h. New flag bits: `CVAR_INTEGER 0x20`, `CVAR_LITERAL 0x40`, `CVAR_HIDDEN 0x80`,
  `CVAR_WEAPON 0x100`, `CVAR_ITEM 0x200`, `CVAR_AMMO 0x400`, `CVAR_MISC 0x800`, `CVAR_PROTECTED 0x1000`,
  masks `CVAR_INFOMASK`, `CVAR_SAVEMASK`, type `cvarcallback_t`.
- `engine/src/common/header/sof/engine.h` (new, owner I1a): `Cvar_GetCB` (SoF 4-argument Cvar_Get with change
  callback), `Cvar_GetModule` (module-facing: hidden cvars invisible), `Cvar_SetHiddenAccess` (renderer slot
  0x4c), `Cvar_ForceSetUnprotected` (save load / module force-set; respects 0x1000), `Cvar_Find`,
  `Cvar_BitInfo` (game import #91), `flrand`/`irand` (SPEC-313 §9), `FS_Userdir`.
- Cvar list order is now newest-created first (was alphabetical); command list likewise. `Cmd_AddCommand`
  copies the name.
- Removed Yamagi-only commands not in SoF: `inc`, `dec`, `reset`, `resetall`, `toggle`, `vstr`; removed the
  Yamagi cvar renaming table (SoF configs use `gl_mode`, `gl_swapinterval`, ... under their own names).
- Command buffer is 8192 bytes; `wait` waits one frame (Quake II), not 16 ms: the main loop runs the command
  buffer only on packet frames (frame.c).
- `Cmd_CompleteNext(prefix, previous)` (engine.h) for console TAB cycling (SPEC-313 §10), as asked by I6h.
- `userweapons`, `useritems`, `userammo`, `usermisc` are registered by the engine core (`sof_engine.c`) unless a
  menu package registered them first.
- `save`, `load`, `quicksave`, `quickload`, `newsave`, `wipe` live in `common/sof_save.c` (they replace Quake II's
  `save`/`load` at start). I1b: plug the server and game module in with `SG_SetHost(sg_host_t)` (savegame.h) and
  call `SG_AutoSave(true)` after `map` / `SG_AutoSave(false)` after `gamemap` spawned a single-player level.
  I1b: call `SoF_FilterRefused(ip)` for incoming connections, `SoF_BuildMapList()` when a deathmatch map starts,
  set `sof_game_getgamestats` / `sof_game_updateinven` / `sof_game_savesleft` / `sof_ghoul_command` (sof_admin.c).
- Sound: Yamagi's OpenAL/SDL/OGG sound is no longer built; `client/snd_host.c` loads the SoF sound module
  (`snd_dll`) and provides the client's `S_*` calls. cl_sof.c calls `SNDH_*` for svc_sound, svc_sound_info, the
  sound configstrings and level registration.
- The engine executable exports the SoF.exe functions the sound modules import (SPEC-AMEND-001); named `SoF.exe`
  it hosts the original Defsnd/EAXSnd/A3Dsnd.

## State
- 2026-10-22 00:3x: started (second attempt after a power loss). Baseline build in `build-I1a-x86` OK.
- Test copy `D:\Games\SoF-test-I1a` created (paks hard-linked from the oracle, executables/DLLs copied).
- 01:45 stopped by the API session limit, resumed 04:20.
- 05:10 first complete build (private tree, see below); the engine starts, runs the SoF config chain, hosts
  the original Defsnd.dll, writes `<user>/config.cfg` and `<user>/sof.log`.
- 05:30 black-box probe (`openmerc-data/I1a/oracle_user/probe.cfg`, 60 console commands: path, dir, alias,
  user*, set/sset/zero/add/or usage, exec rules, macros, toggles, link, vid_modes, dmflags, filter, bots,
  version, save/load checks, cpuid) gives the same console text as the original except the test paths, CPU MHz
  and `gserver` (Ghoul debug interpreter, I3).

## Build and test
The shared tree is often mid-edit by other packages, so I1a builds a private tree: HEAD plus the I1a files
(`openmerc-data/I1a/tree`, made by a script that exports HEAD with `git archive` and applies the I1a edits of the
shared client files), output `openmerc-data/I1a/build-x86`:
```
cmd /c "set OM_BUILD_DIR=<openmerc-data>\I1a\build-x86&& <openmerc-data>\I1a\tree\tools\build\build-msvc.cmd x86 Release"
```
In the shared tree: `set OM_BUILD_DIR=...\build-I1a-x86` then `tools\build\build-msvc.cmd x86 Release`.
Test install: `D:\Games\SoF-test-I1a\om\SoF.exe` (= openMerc.exe, the name lets the original sound modules bind
their imports) with the sound DLLs next to it; run with
`openmerc-data/I1a/run_om.ps1 -EngineArgs "+set logfile 2 +exec probe"` (adds `-portable -basedir
D:/Games/SoF-test-I1a +set user .../I1a/home/user +set vid_fullscreen 0`). The original runs from the same test
copy with `run_orig.ps1` (never the oracle install).

- 09:20-09:45 sound host verified against an original dedicated server (port 27951, dm/jpndm2); dedicated
  cvar/cmd lists; per-lump BSP limits; final diff report `docs/impl/I1a-cvar-cmd-diff.md`.

## Done (with evidence)
- **Filesystem (SPEC-300)**: mount order cddir/base, basedir/base, mod, user; per directory loose files, pak0..9,
  then the other `*.pak` alphabetically (names ending `PAK<digit>.PAK` skipped); name normalisation; 20 handles;
  `FS_LoadFile` buffer + `\n\0`; links; `path`/`dir`/`link` with the original's output (black-box probe identical);
  `fs_createpak` -> `paklist.txt`; user directory = write directory; `-basedir/-cddir/-game/-user`; mod change
  rules; `gamedir` created only by a mod change (as observed). The binary dir's `base/` stays a module lookup path
  so the engine's own game.dll is found.
- **Startup (SPEC-301)**: two `+set` passes, `default.cfg` then `config.cfg`, autoexec rule, `start` /
  `dedicated_start` unless `+` commands or `-nostart`, version `1.07fx86F`, banner, cpuid report, local
  addresses, config.cfg / configs/CURRENT.cfg in the SoF format (`saveconfig`, `savekeys`), `<user>/sof.log`
  (logfile 1/2+), framed errors deleting `current.sof`, `Error during initialization: <text>`.
- **Cvars/commands (SPEC-302)**: SoF flags, cvarlist columns, set letters, sset, zero, add/or/and/xor, `! + -`,
  integer/literal/hidden/protected, `matrix`, info-char checks, callbacks, newest-first lists, 8192-byte buffer,
  `$`/`#` macros, exec `.cfg` rule, resolution order, alias rules; engine cvar table generated from SPEC-302
  (`tools/build/gen_sof_cvartable.py`, 272 cvars, client/dedicated flags); forwarded game commands.
  Result: no engine cvar missing; client 398/504 identical (rest renderer/game/driver cfg/test setup), dedicated
  267/283 identical (rest test setup, qport, cpu, `dmflags`); commands: see the diff report.
- **Savegames (SPEC-303)**: container with MD4-XOR checksums; engine chunk sequence; save/load/quicksave/quickload/
  newsave/wipe with the specified checks (probe identical to the original); `sv_testsave`. The original's
  `auto.sof` (162 chunks) verifies with `om_savecheck`; OpenMerc saves (137 chunks) verify and load.
- **String packages (SPEC-131, SPEC-313 §1)**: `.spe`/`.sp`, language rule with English fallback, colour tokens,
  `STRIP-`, owners, lookup by id / name (later duplicate wins: `MENU_GENERIC_QUIT` = entry 42), reload on every
  `sp_language` assignment; engine packages items/weapons/general/dm_generic/str_server. `om_strings` checks.
- **SPEC-313**: random generator (test vectors 9 match), parental locks (cvars, German rule, forced locks, store read
  only), IP filter, dmflags, bots, getgamestats, gserver hook, map list, conchars, vid_modes, per-lump BSP limits
  (all 76 stock BSPs within them), TAB-cycling API.
- **Sound host (SPEC-004, AMEND-001)**: snd_dll selection (A3D never present, EAX via DirectSound property set),
  20+3 exports, fatal texts, sound struct with the client data in the original layout, svc_sound /
  svc_sound_info / configstring hooks, level registration from SoF configstrings, S_Activate, exports of the
  executable (24 names, `dumpbin` checked). On a live original server: Defsnd.dll registered 301 sounds of
  dm/jpndm2; OMsnd.dll loaded 301/301, "precache check 303/303", ambient set, music set DM1 and the `.eal`
  environment from the configstrings, music playing.

## Remaining
- Server-side hooks for I1b: `SG_SetHost` (game chunks, level name, screenshot, cinematic/health checks),
  `SG_AutoSave` after map/gamemap, `SoF_FilterRefused`, `SoF_BuildMapList`, stats/inventory/savesleft hooks.
  Without them a save holds the engine chunks only and `load` restarts the map.
- SPEC-301 B5 renderer selection by `vid_ref` (the Yamagi `vid_renderer` path still loads ref_gl3; I4a/I6h), B8
  game/player module lookup order and DLL checksum (I1b), B9 `-cs` utility (writes the registry; not done),
  developer bit 2 console window.
- SPEC-313 §4 texture-information table and surface damage, §6 world linking, §7 filtered traces (server, I1b);
  §8 spectator movement type 2 (pmove + client prediction mapping in cl_sof.c); §12 window glue.
- `ping`, `serverstatus`, `reset_predn` (behaviour not specified), `gsc_*` chat (optional), `vid_front`.
- Client packages of configstrings 1464-1492 are registered by I6h (`cl_strings.c`); menus by I6m.
- Open questions: `docs/specs/OPEN_QUESTIONS.md`, section "I1a".

## Preview build for viewing
`D:\Games\SoF-test-I1a\om-preview\OpenMerc-I1a.cmd` starts the I1a build (windowed, `-portable`, user dir
`openmerc-data/I1a/play/user`). Menus are not there yet; the console works (`map kos1`, `connect`, `path`,
`cvarlist`, `om_strings`, `save x`, `om_savecheck x`).
