# I1b (+I2c) — module host and protocol-33 server (work log)

Package: game-module host (SPEC-001, 99 imports / 28 exports), player-module loader (SPEC-003), Ghoul services
for modules (SPEC-010/011/013), server side of protocol 33 (SPEC-200..203), engine module map (SPEC-005).
Directories: `engine/src/server/`, `engine/src/common/header/sof/game_api.h` (owned; G18 and I7 read it).

## State
- 2026-10-22 04:30: started. Test copy `D:\Games\SoF-test-I1b` (paks hard-linked from the oracle, executables and
  DLLs copied; the oracle is never written).
- Session interrupted three times (last resume 14:21).
- 09:46 reference session recorded: original 1.07f client on the original dedicated server (r3).
- 09:5x the same scripted session with the original client on the OpenMerc dedicated server (o3): joins, gets the
  welcome, chats, disconnects cleanly. Player gets stuck at the crater edge of dm/jpndm2 (see Known defects).
- 14:2x journal written. 14:5x import #6 ghoul-file configstrings. 15:0x brush renderfx. 15:2x initial snap
  order; original-client session o6 matches the reference path. Next: OpenMerc client test, open questions.
- 2026-10-25 (resume after the API-limit stop of 10-22 14:45): goal is the single-player campaign on the
  OpenMerc engine with the original gamex86/player modules. Found from the 10-22 work: local connection uses
  protocol 33 (`cl_sof.c` `SoF_WantProtocol`, I1b edit), OpenMerc client vs OpenMerc dedicated server works (c5:
  join, chat, disconnect). 06:xx SP harness `tests/sprun.ps1` (hidden desktop, both engines). Fixed: `map` from the
  command line with developer 0 loaded "OPTIONS"/"CANCEL" (Cmd_Argv clobbered during game start-up; now copied
  first in `SV_GameMap_f`/`SV_Map`); killserver/quit hung or failed fast (Ghoul reset ran before the game's
  Shutdown; now drop-all, game Shutdown, reset, unload). 07:xx test aids: `sv_watchdog <s>` (stall + crash
  reports with OpenMerc symbols, foreign modules by name only) and the game-import call ring
  (`sv_imptrace.c`). kos1: intro cinematic, captions, HUD, walking, save and load work; ~3 s after walking off,
  a crash (null read) inside gamex86 during RunFrame, about when the first enemy shoots.
- 10-25 08:xx-11:xx (one more API-limit stop): the kos1 crash found and fixed. The crash report put it inside the
  game's callback of the filtered trace (#53). Tracing proxy game module `tests/gtrace/` (loads the real
  gamex86.dll, wraps the import table; run under the ORIGINAL engine on a dedicated server with deathmatch forced
  to 0 and the headless client walking into the enemies): the original calls the callback as
  `filter(candidate, start)`, `start` = the pointer the game passed to #53; with NULL there the original game
  faults at once, with our stray stack word at random (~70 % of runs). Now passed (`SV_CallTraceFilter`,
  `sof_tracefilter_t` in game_api.h has two arguments). 0 crashes in 6 runs after, 4-5 of 6 before.
  Also: commands after `map` deferred to the client's begin (Quake II `Cbuf_CopyToDefer`, the original does the
  same, observed); sound-only entities with attenuation 0 sent at any distance (original kos1 demo: 39 such
  entities up to 4678 units). Ports moved to the I1b base 29400 (brief "Network ports").
- 10-25 12:xx: the proxy showed a THIRD callback argument as well: the end point of the entity sweep (world hit
  point when the world was hit; values match start + world fraction × (end − start) in every logged call). With
  only `start` the Release build still crashed (rd build had a harmless stack word there by chance). Now
  `filter(candidate, start, sweepend)`: Release 0 crashes in 6 runs (5 of 6 before). `load` fixed: SPEC-303 step 4
  "a new server is started": `SVH_StartLoaded` now does SV_InitGame ("Server restarted", client reconnects), game
  ReadGame, then the map in load mode - before, the old client slot stayed and the local reconnect got "Server is
  full" (the 10-22 "load works" was the script quitting before the reconnect). Same message sequence as the
  original now. "Can't savegame while dead!" uses client 1's stats[1]. GS.15 (SPEC-AMEND-005, from I2d):
  `sv_violence` = locks & 0x1F while no server runs (`common/sof_locks.c`, I1a's file, one line), bit 0x20 set at
  the end of every spawn when gamex86 = 64C0FC54 and the server-side player.dll = EACC2EEA (`SV_SetPureViolenceBit`;
  checksums verified on the shipped files). Evidence: OpenMerc q2ded with stock modules `sv_violence` 63, with the
  proxy module (`+set game i1bt`) 31 - the original rejected its own local client with the proxy for the same
  reason ("Server violence settings are incompatible").
- 10-25 13:xx: SPEC-200 single-player datagram size. tut1 dropped the local client at once ("John Mullins
  overflowed") and kos1 after `load` printed "Netchan_Transmit: dumped unreliable" (then "Missing Child"): the
  engine had 1400 bytes everywhere. Now one global `net_maxmsglen` (common.h/netchan.c, loopback buffers in
  backends/*/network.c - minimal edits in I1a's/base files): 16384 for a local server with maxclients 1, 1400
  otherwise (reset at shutdown); netchan buffers sized 16384; the server's datagram, Ghoul budgets and signon
  batches (SPEC-203: half the maximum, 700 / 8192) follow it. MP regression (headless, jpndm2): 0 decode errors,
  700-byte batches as before. Deviation: the receive buffer is 16384 in network games too (the original drops a
  datagram of 1400 bytes as "Oversize"); harmless for interop.
- 10-25 14:xx: frame counter. The proxy's level-start timeline under the original (`I1B_GTRACE_TIMELINE=1`):
  RunFrame(0), RunFrame(1) while loading, then counters 1, 2, 3 ... (= wire frame numbers, first frame 1). OpenMerc
  started at wire frame 0 with counter frame + 2, so its game time was (frame + 3) x 0.1 against the original's
  (frame + 2) x 0.1 (o7: 187 -> 19.0; r3: 577 -> 57.9). Fixed (`sv.framenum = 0` after the loading frames,
  counter = frame): headless MP check 63 -> 6.5. Also from that timeline: with the headless client the original
  calls ClientThink twice right after ClientBegin while the cinematic state is still 0; the cinematic starts in the
  next RunFrame. kos1 single-player demo: OpenMerc's game time lagged its frame numbers by 2.1 s (frame 77 = 5.6 s):
  `paused` held the game during the level start: Yamagi's `cl_loadpaused 1` pauses a single-player game from the
  local `connect` until the client is active; SoF has no such pause. Default now 0 (`cl_main.c`, one line,
  I1b comment).
- 10-25 11:2x-11:58 map sweep sw1 (`tests/sweep.ps1`, build of ~11:20 = with the filter fix, before the
  datagram/frame-counter/loadpause changes): all 32 single-player maps load and run 35-41 s each (intro
  cinematics, captions, HUD, view weapon, a short walk) with exit 0 and no crash report; only tut1 dropped the
  local client ("overflowed", fixed since by the 16384-byte datagram). Summary `openmerc-data/i1b/sweep_sw1.txt`,
  pictures `openmerc-data/i1b/sw1_<map>/m0..m2.png`.
- 10-25 12:0x kos1 client demo after the datagram / frame-counter / load-pause / sound-entity changes vs the
  original's (`demodiff.py`, 221 common frames): game time (frame + 2) x 0.1 as the original; the entity sets are
  equal except one-frame boundary cases (39 sound entities now sent as the original); the player no longer moves
  before the intro (ClientThink held back from the first command: begin at frame 7, cinematic from frame 8,
  `sv_hostdebug 64`); the intro runs one frame earlier than the original's (fade from frame 97 vs 98: the local
  client finished loading one frame sooner). Remaining field differences: Ghoul instance ids 2-4 lower (I3 registry
  items above) and what follows from the one-frame shift.
- 10-25: the player-module context flag is now also set to the server side around ClientThink and ClientCommand
  (SPEC-602 §7; SPEC-003 named only exports 0x48/0x4C) - matters once the client hosts the module's client side.
  tut1 runs now (tutorial texts, no overflow).
- 10-25 12:3x the OpenMerc client could not fire at all (single player and multiplayer, on the original server
  too): it sent userinfo `predicting 1` (the SPEC-302 table's default wins over cl_sof.c's "0"), so the game waited
  for predicted fire times the client never sends (no client-side player module). Isolated black-box: the headless
  client (predicting 0) fires on both dedicated servers in SP mode (clip 8 -> 5 in three presses), the OpenMerc
  client did not on either. Fix: `predicting` forced to 0 in `CL_SoF_Init` (cl_sof.c, I1b comment). kos1 now:
  clip 8 -> 7 -> 6 -> 5, 6 temp entities, 5 culled events (original's demo: 6 and 4). Level change: the
  OpenMerc client reconnects with a full handshake after `disconnect`, the server skipped the zombie slot ("Server
  is full"); the reconnect rule now reuses any non-free slot of the same address (Quake II, SPEC-200 step 6). The
  original prints "loopback:reconnect" there. Verified: `gamemap kos2` from kos1 (`tests/sp_change.cfg`) now
  reconnects ("[0.0.0.0]:0:reconnect", client_connect) and the player starts at (328 -3583 -540) - the original's
  spot - with the same HUD (pictures b3_change/c1.png vs b1_ochange/c1.png); `load` then walking moves the player
  (the loaded game runs) in both engines.
- 10-25: import 13 (game developer print) now prints only while `developer` is odd (SPEC-AMEND-005 G6b.6, request
  in docs/impl/F5.md).
- 10-25 12:2x-12:5x time-aligned idle pictures, 10 maps x 2 engines (`sweep.ps1 -Script sp_idle.cfg`: OpenMerc
  captures at server frames 100/200/300, the original by time from its "Map:" line; `pairs.py` ->
  `openmerc-data/i1b/pairs_idle/`): kos1, tsr1 (same moment of the intro, same credits, characters in place), sib1,
  nyc1 ... show the same shots; differences are renderer lighting (not I1b) and trn1, whose intro camera is a few
  seconds off (different camera segment at frame 300) - to check whether the local client entered later there.
  OpenMerc prints "Failed fDot test 0.465090" twice on tsr1 (the subway train touching something sideways), the
  original never: open (train push / touch).
- Note (coordinator correction 10-25): the original dedicated server is said to need `set developer 1` inside an
  exec'd cfg. My original dedicated runs with `+set developer 0` (r3, gt_orig2..6) did spawn kos1/jpndm2 (headless
  client active, 400 frames, AI traced), so they stand; new runs follow the brief.
- Environment note: the parental-lock store is machine-wide (registry) and was changed by other tests during the
  day: `sv_violence` of the same OpenMerc build read 63, then 0; one run (o5) was rejected because server and
  client had read different lock states. Not an I1b defect.

## Files
| File | Content |
|---|---|
| `common/header/sof/game_api.h` | SoF 1.07f module ABI: import table (99), export table (28), player-module export, entity prefix (0x19C), entity state (116), player state (200), user command (24), trace (56), surface (72), pmove block (256), x86 static asserts |
| `server/header/server.h` | server/client structs (3555 configstrings, 1024 baselines, culled events), svc ids, prototypes |
| `server/header/sv_tables.h` | configstring dictionary and direction table, generated by `tools/net/gen_sof_tables.py` |
| `sv_game.c` | game loader (search order, `GetGameAPI`, version 3), the 99 imports, client think wrapper, player-context calls, Ghoul host callbacks |
| `sv_player.c` | player.dll loader (refcount per side, SPEC-003 checksum: shipped DLL = EACC2EEA) |
| `sv_ghoul.cpp` | Ghoul services: real I3 library by default (`OMGhoul_*`), stub selectable with `sv_ghoul_stub 1`; svc 26/27 writer glue (offer, commit, acknowledge, resend, drop) |
| `sv_world.c` | SoF linking (SPEC-313 §6, solid packing SPEC-202 §2.3), traces #52/#53 (SPEC-313 §7), PVS/PHS, map extras (check string, texinfo records, regions), `sv_tracetest` debug command |
| `sv_pmove.c` | import #81: Quake II 3.19 pmove on the SoF block; spectator type 2 per SPEC-313 §8 |
| `sv_msg.c` | coord/angle/dir writers, configstring dictionary coding, move checksum |
| `sv_entities.c` | SoF delta writer (entity header, player state flags), frames (game time, culled events), client frame build |
| `sv_send.c` | prints, multicast/unicast, sounds (SoF flags), Ghoul reliable/unreliable budget, datagrams |
| `sv_user.c` | signon (new, configstrings in 700-byte batches, ghoulstrings, baselines, precache, begin), downloads (SPEC-203), usercmd reader, stringcmds |
| `sv_conless.c` | connectionless commands (SPEC-200): status/info/ping/challenge/connect checks 1–9/rcon, LAN test |
| `sv_init.c` | index allocation, spawn, game init, map changes |
| `sv_main.c` | cvars, frame, drops, userinfo, final message |
| `sv_cmd.c` | operator commands; I1a hooks (`savesleft`, `getgamestats`, `updateinven`, `gserver`) |
| `sv_strings.c` | string-package services of the game (SP_* of I1a), svc 34/36/37/39 prints |
| `sv_save.c` | savegame host for I1a's container (`sg_host_t`), imports #85/#86; thumbnail = I6m `M_SaveThumbnail` (client build only) |
| `sv_gamespy.c` | I2b's file, adapted to the SoF server structs (heartbeat trigger, client loops, score) |
| `tests/svtest.py` | runs a dedicated server (OpenMerc q2ded or original SoF.exe, hidden) in the test copy, optional client (headless `game/test/sofclient.py`, UDP proxy, or the original client minimised with console text through OOB `cmd` packets), rcon (also timed), logs to `openmerc-data/i1b/<tag>/` |
| `tests/capcompare.py` | compares two captures: OOB, stufftexts, serverdata, configstrings, baselines, Ghoul chunks, svc counts, first frames |
| `tests/netstates.py` | prints entity / player states of a capture |
| `sv_watchdog.c` | test aid: cvar `sv_watchdog <s>` - stall reports (all thread stacks) and an unhandled-exception report to `<user>/watchdog.txt`; OpenMerc frames with symbols (RelWithDebInfo build `build-I1b-x86-rd`), Windows frames with public symbols (`-SysSymbols`), other modules by name only |
| `sv_imptrace.c` | test aid (with `sv_watchdog`): ring of the last game-import calls and Ghoul interface calls (vtable thunks on the library's classes), printed in the reports |
| `tests/sprun.ps1` | single-player session on a hidden desktop: `-Engine om` (OpenMerc client + original modules) or `-Engine orig` (original SoF.exe, capture shim); script started by `+map <map> +exec omtest.cfg`; `-TimedShots` for the original |
| `tests/sp_smoke.cfg`, `sp_walk.cfg`, `sp_rec.cfg` | session scripts (walk, turn, fire, save, load; demo recording) |
| `tests/demostates.py` | entity / player states of a client demo (`--at <frame>`, `--first`, `--sizes`) |
| `tests/gtrace/` | tracing proxy game module (`build_gtrace.cmd`; install as `<test copy>\i1bt\gamex86.dll`, run with `+set game i1bt`; env `I1B_GTRACE_LOG`, `I1B_GTRACE_SP=1` forces deathmatch 0 on a dedicated server, `I1B_GTRACE_TIMELINE=1` logs SpawnEntities/ClientBegin/ClientThink/RunFrame with the cinematic state): logs filtered traces, the candidates offered, the callback's answer and its extra arguments (with the words they point to), imports called inside the callback |
| `tests/demodiff.py` | compares two client demos of the same level frame by frame (entity sets, fields with a position tolerance, player state, svc counts) |
| `tests/sweep.ps1`, `tests/sp_sweep.cfg`, `tests/pairs.py` | all 32 single-player maps through sprun (one engine), one line per map (exit, crash report, log errors); pictures of two sweeps side by side |
| `tests/sp_change.cfg`, `tests/sp_load.cfg` | level change by `gamemap`; save + load |
| `tests/DeskWin.cs` | lists the windows of a process on a test desktop (message boxes of a stalled run) |

## Build
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I1b-x86&& tools\build\build-msvc.cmd x86 Release"
```
Only the dedicated target (faster): `cmake --build build-I1b-x86 --target q2ded` in a VS x86 prompt. The client
target links when the other packages' client files are consistent (it failed earlier in I5's fx files).

## Test
Single player (both engines; ports 29403/29404):
```
powershell -File engine/src/server/tests/sprun.ps1 -Engine om -Script engine/src/server/tests/sp_smoke.cfg -Tag x
   (diagnostics: add -Build build-I1b-x86-rd/release -SysSymbols -Extra '+set sv_watchdog 8')
powershell -File engine/src/server/tests/sprun.ps1 -Engine orig -Script engine/src/server/tests/sp_rec.cfg -Tag y
python engine/src/server/tests/demostates.py <user>/demos/i1bsp.dm2 --at 120
```
Filtered-trace comparison (original dedicated, SP rules, headless walker):
```
set I1B_GTRACE_SP=1 & set I1B_GTRACE_LOG=...\gtrace_orig.log
python engine/src/server/tests/svtest.py --engine orig --tag gt --map kos1 --dm 0 --maxclients 1 --developer 0
   --client headless --seconds 45 --script "16:move 200 0 0;40:quit" --set "game i1bt"
```
Multiplayer (the 10-22 sessions below used ports 27940/27941; since 10-25 the defaults are the I1b ports
29400 server, 29401 client, 29402 relay):
Every OpenMerc run passes `-portable -basedir D:\Games\SoF-test-I1b -user openmerc-data/i1b/<tag>/user`. Original
runs use `SoF.exe +set dedicated 1 +set user ./u_i1b_<tag>` in the test copy and **must use developer 0**
(otherwise SoF.exe opens the "Developer Mode" message box, SPEC-301, and blocks hidden).
```
python engine/src/server/tests/svtest.py --engine orig --developer 0 --tag r3 --port 27940 --proxy 27941 --seconds 40 \
   --client sof --cscript "16:+forward;19:-forward;20:+attack;21:-attack;23:say hello from I1b;30:disconnect;31:quit"
python engine/src/server/tests/svtest.py --engine om --tag o3 ...same...
python engine/src/server/tests/capcompare.py openmerc-data/i1b/r3/cap.jsonl openmerc-data/i1b/o3/cap.jsonl
python engine/src/server/tests/svtest.py --engine om --tag x --client headless --script "0:move 0 0 0;4:move 200 0 0;8:quit"
```

## Done (with evidence)
- Original `gamex86.dll` and `player.dll` (1.07f) run in OpenMerc `q2ded`: dm/jpndm2 spawns (string packages
  dm_generic/items/weapons/general/bot registered, bot waypoints loaded), player.dll checksum EACC2EEA.
- Original 1.07f client joins the OpenMerc dedicated server and plays (o3, 2026-10-22): challenge, connect,
  configstrings, 26 ghoulstrings (real I3 Ghoul library), baselines, precache, begin, "Welcome to Soldier of Fortune
  Multiplayer", movement, chat both ways ("I1bbot : hello from I1b" in the client), death message, disconnect.
  sofnet decodes every datagram of both sessions with 0 errors.
- Capture comparison r3 (original server) vs o3 (OpenMerc), same spawn point:
  identical: OOB replies, serverdata, all non-Ghoul-file configstrings, 16 baselines except renderfx (below),
  entity lists of the first frames, Ghoul instance handles, svc_nameprint/print/restart_predn/serverdata/
  spawnbaseline/welcomeprint counts. Differences: see Known defects.
- `sv_violence` on an unlocked server is now 63 like the original (was 31: every original client with
  `cl_violence` 63 was rejected with `rejected_violence`); one-line fix in `common/sof_locks.c` (I1a's file,
  I1a finished), evidence: rcon on the original dedicated server `"sv_violence" is "63"`.
- (10-25) Single player on the OpenMerc engine with the original modules: all 32 campaign maps load and run
  (sweep sw1), intro cinematics and captions, HUD, view weapon, walking, firing (clip and temp entities as the
  original), save / load (server restart and reconnect as the original), level change by `gamemap` (kos1 -> kos2,
  same start spot and HUD as the original), tutorial texts (tut1). The kos1 client demo matches the original's
  frame by frame in game time, entity sets and the intro timeline (one frame earlier). AI shots at the player
  no longer crash the game (filter callback arguments).
- (10-25) MP regression after all changes: original client on the OpenMerc server (o7) vs the original server
  (r3) as before; headless client 0 decode errors; `sv_violence` 63 with the stock modules, 31 with a modified
  game module (GS.15).

## Known defects / differences (open)
0. (10-25) tsr1: the original game module prints "Failed fDot test" twice under OpenMerc (train touch at a side
   angle), never under the original engine. trn1: intro camera a few seconds off. After `load`: "Missing Child"
   (I3). No weapon sounds/flash from the view weapon in single player (client side of SPEC-003 not hosted).
1. Ghoul-file configstrings 1498.. (import #6, done 14:5x): every file the Ghoul library opened through the
   engine, sorted by strcmp; .ghb/.ifl/.tga upper-cased, .gsq left out, .gpm/.gbm as `ghoul/pmodels/...`
   (rule read off the reference list). dm/jpndm2: 442 names, all of them in the reference's 837 in the same
   spelling; missing are 393 skin `.TGA` and 2 `.IFL` files the original server-side Ghoul opens and I3's does not
   (needs I3). The list is kept for the process lifetime (superset after map changes; original UNVERIFIED).
2. Brush-model renderfx (done 15:0x): setmodel of `*n` sets 0x400, plus 0x800 when one of the model's faces has
   texinfo flag 0x800 (dm/jpndm2: *1, *2 alpha faces → 3072, *3..*6 → 1024). All 16 baselines now equal the
   reference. Whether the original sets them at setmodel or per frame: UNVERIFIED.
3. Player movement, initial snap (fixed 15:2x): the original moves a fresh spawn by +1/8 on all axes (also after a
   respawn, once per snap: (−768, 7296, −176) → (−767, 7297, −175) → (−766, 7298, …)). Cause: its
   `PM_InitialSnapPosition` tries the offsets +1, 0, −1 (early Quake II order; Yamagi tries 0 first). With the
   Yamagi order the player stood at y = −928.0 exactly, its box touched brush 371's face y = −944, Quake II's position
   test counts touching as solid while the sweep does not reach the brush, and the player hung at the crater edge
   (x ≈ −680) until it died. After the fix the headless walk test and the original-client session follow the
   reference path to the frame: walk off at x ≈ −676, land at (−6271, −7423, −1087)/8, pick up the 5.56 SMG.
4. Landing flag: after the long crater fall the original sets pm_flags 0x10 (land time) on landing, OpenMerc does
   not (the Quake II rule needs velocity < −200 at the categorize after the slide, which clips it). Open.
5. Usercmd timing jitter: in o3 the per-frame displacement varied 108–216 (×8 units) vs a steady 162 on the
   original; in o6 it is a steady 162 too. Not reproducible, left as an observation.
6. o6 vs r3: temp entities (type 61) 25 vs 9 and Ghoul unreliable 52 vs 34 messages: the 1-second attack falls on
   different weapons/moments in the two runs (the scripted console text is wall-clock timed). Not a protocol
   difference as far as the decoded messages show.
7. (10-25) Import #25 is implemented (SPEC-313 §4, `SV_DamageTexture`). The leaf number in OpenMerc traces is
   always −1; the original's filtered traces logged by the proxy (point traces through characters, kos1) also
   carried −1, so no difference seen yet (other trace kinds not checked).

## Remaining
- Single player: play deeper into the campaign (a level end by the game's own trigger, `SaveClientData` and the
  inventory carried over; death -> `respawn` = alias `menu load`, I6m's menu); compare the time-aligned idle
  pictures of 10 maps (batch `id_om` / `id_or`, `tests/pairs.py`).
- Engine CLIENT side of SPEC-003 is not hosted: the original client loads player.dll's client side at client
  init (`GetPlayerClientAPI`, 20-entry client import table: client Ghoul system, client time, predicted fire
  effects, view-weapon note sounds/effects, MSG/SZ helpers), writes the context flag -1 each client frame, feeds
  svc 28/29/30 to its inventory (cl_sof.c skips them) and, while predicting, copies exports 5/6 into the usercmd.
  Until then the client must report `predicting 0` (done) - multiplayer firing then works without prediction.
  Touches I1c (client Ghoul), I5 (effects), I6h/I2a (cl_sof.c, input): to be scoped with the coordinator.
- "Missing Child" after `load` (I3, see Needs).
- pmove differences (needs a movement spec); landing flag (Known defects 4).
- `sof_pmove_t` tail: is-client callback 0xF8, stagger time 0xFC.
- Done since 10-22: OpenMerc client against the OpenMerc server (dedicated c5, listen = single player);
  `cl_sof.c` protocol 33 for the local server; open questions written (OPEN_QUESTIONS "I1b" 1-6); GS.15.

## Needs from other packages
- I1a: none blocking. I1b edited I1a's files minimally: `common/sof_locks.c` (sv_violence 63 on 10-22; GS.15 lock
  bits only on 10-25), `common/netchan.c` + `common/header/common.h` + `backends/*/network.c` (SPEC-200 global
  maximum datagram `net_maxmsglen`, 10-25).
- Notes for others: G18 - the engine calls the #53 filter callback as `filter(trace, start, sweepend)` (the
  original does; `sof_tracefilter_t` in game_api.h); a game module that wants the shot's ray in its filter can use
  them. I6h/I1c - the local single-player connection now carries datagrams up to 16384 bytes (SPEC-200): client
  buffers that assume 1400 (demo recording header buffer "of the current maximum message size", SPEC-311) should
  use `net_maxmsglen`.
- I3: the server-role library does not open the skin textures the original server-side Ghoul opens (dm/jpndm2:
  393 `GHOUL/.../*.TGA` and `GHOUL/ENEMY/BOLT/ACC_ANTENNA.IFL`, `GHOUL/OBJECTS/LIGHT/DESK_LAMP/BULB.IFL` are in the
  original's configstrings 1498.., not in OpenMerc's). I1b records host loads in `sv_ghoul.cpp` `HostLoad`.
- I3 (from the kos1 single-player client demos, original vs OpenMerc, 10-25; `tests/demodiff.py` and a string diff
  of the Ghoul reliable streams): (1) the original registers a `.gsq` sequence name with the TAB that stands in the
  list file (`GHOUL/ENEMY/MESO/STD_ATHREAT_DWN_P_N<TAB>.GHL`, also ECTO) as its own registry string - OpenMerc trims
  it, so the registry has one string fewer and the ids that follow shift; (2) texture paths keep their
  backslashes in the original's registry (`\\RAVEND\SOF\ART\GHOUL MODELS\...\ACC_ANTENNA.TGA`, two leading backslashes), OpenMerc sends
  `//RAVEND/SOF/...`. Both change the bytes of svc 26 and the instance ids the client sees (ids 2-4 lower in
  OpenMerc on kos1). Equipment choices (e.g. ACC_GOGGLES) differ by the time-seeded random numbers, not a defect.
- I3: after `load` the OpenMerc client prints `Missing Child` (4-8 times, kos1, `tests/sp_load.cfg`, also with the
  16384-byte datagram now, so no budget cut): an instance state in svc 27 names bolt-on children the client has
  not received. The original client prints nothing after the same load. Suspect: instances restored by method 38
  (LoadState) and re-attached by the game are not all offered/replicated as children by the server system, or
  their creation reaches the client after the parent's bolt-on map. A level start without load shows none.
  Possibly relevant (read in `ghoul_net.cpp`, not verified): `NetWriteUnreliable` skips an instance whose state
  equals its base ("rule 6") also when the client has never received it, and the client creates instances only
  from a written record - a restored child equal to its object baseline would then never reach the client.
- Spec team: player movement differences (SPEC-313 §8 says "the rest ... need their own spec").
