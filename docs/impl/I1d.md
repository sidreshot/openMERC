# I1d — Engine client: the client side of the player module (work log)

Package: SPEC-003 (client side of player.dll: loading, import tables, context flag, svc 28–30), SPEC-602 (what the
client runs), SPEC-400 "View weapon" (notes `sound` / `effect`, predicted fire effects), SPEC-310 §13, SPEC-012/013
(the view weapon's Ghoul instance on the client).
Directory: `engine/src/client/cl_player.c`, `cl_playerghl.cpp`, `header/cl_player.h` (new); minimal hooks marked
`/* I1d */` in cl_main.c, cl_network.c, cl_sof.c, sv_main.c, sv_player.c, sv_entities.c, game_api.h,
engine/CMakeLists.txt. Tests: `tests/I1d/`. Data: `<openmerc-data>\I1d\`.
Build tree: `build-I1d-x86`. Ports: base 29560 (server 29560, client 29561).
Game copies: `D:\Games\SoF-test-I1d` = the fully open stack (`tests\X1\assemble.ps1 -Dest D:\Games\SoF-test-I1d
-EngineBuild build-I1d-x86\release -SoundBuild build-sound-x86-Release -GameBuild build-game-x86-Release
-PlayerBuild build-player-x86-Release`: our engine, OUR gamex86.dll and player.dll); `D:\Games\SoF-test-I1d-orig` =
the same with `-OrigModules` (original SoF.exe and DLLs, original base\gamex86.dll / player.dll, our engine in
`om\`) — the former `SoF-test-I1d` of session 1, renamed.

## State
- 2026-10-25 session 1 (an earlier I1d session, interrupted, no journal): wrote the code below and the
  test aids, ran the original and our engine with the original modules (sessions o1/o2/o_a/o_a3/o_c and
  m1/m2/m_a..m_a4/m_c4 in openmerc-data\I1d, logs only). Nothing committed.
- 2026-10-25 18:0x session 2 (this log): resumed; code read, `build-I1d-x86` rebuilt from the current tree
  (all 177 targets, exit 0). Journal written from the code. Open copy and original-modules copy assembled.
  Session-1 code committed as found (51d0d66).
- 18:2x–18:4x the owner's priority (no view-weapon sounds in the fully open stack) diagnosed, two causes
  (*Findings* 1 and 2 below): (1) the owner's game build `build-game-x86-Release` (2026-10-22 09:38) predates the
  G3/F5 fix of 2026-10-25 12:23 that gives the inventory re-created by `elbow`/`playlevel`/`bigelbow` its owner
  context — with OUR player module the new inventory's `WritePSWeapon` then finds no player state, the view weapon
  of the player state stays the OLD instance (the client keeps drawing the old shotgun, no notes = no sounds); the
  original module tolerates a missing context. (2) a Ghoul replication hazard once the context is right: our module
  destroys the old view instance before it creates the new one, the library hands out the freed id again at once
  (SPEC-010 "freed ids are reused"), and the reliable removal can reach the client after the new instance's first
  frame record — the client then applies the new instance's states to the old instance it still holds. Fixed in the
  net layer (both directions, below). Game rebuilt from the current tree into `build-I1d-game-x86` for the test
  copy (the owner's three build dirs untouched).
- 18:4x–19:0x sound comparisons original vs open stack vs original modules (Results, session 2): reload / draw /
  cock / dry-click notes and the fire sounds match the original per shot and to the frame; goal 3 (Missing Child
  after `load`) fixed with a before/after run. Commits 51d0d66, 571f41f (+ this journal). Open: sniper zoom sound
  (client view), the animation-phase / casing lag seen in the 125 ms capture, the predictor (SPEC-602, remote
  servers), Ghoul registry string differences of I1b.

## For other packages
- Owner / coordinator: the playable open copies need the game module rebuilt from a tree at or after a5e9c76
  (2026-10-25 12:23); `build-game-x86-Release` of 2026-10-22 lacks the owner-context fix for `elbow` & co. and,
  with OUR player module, leaves the view weapon on the old instance (no sounds, old model). X1's
  `build-X1-game` (13:03) is new enough.
- I7: a server inventory that gets no owner context before its first selection writes no player state
  (OPEN_QUESTIONS I1d-5; the original module tolerates it). The module's view instance is destroyed before the new
  one is created (`NewViewInstance`), so handles are reused (29 → 29) where the original's alternate (30 → 31 →
  30); harmless with the net-layer change, visible in `om_cpltrace` / vwstates.
- I5: the muzzle effect's sound is started with entity 0 and the casing sound with entity −1; the original logs
  the opposite (cosmetic in the logs; attenuation 0 / positioned).
- I6h / client view: the sniper zoom sound (SPEC-400 "View weapon" 3) is missing in OpenMerc.
- I1b: "Needs from I3" Missing Child after `load` — answered above (net layer rule 6 for never-sent instances).
- G2 used `build-I1d-x86\release\openMerc-ded.exe` for a dedicated server (18:33–18:4x, port 29464), which blocked the
  q2ded link of one of my rebuilds; please run from your own build tree.

## What the code does (as found, session 1; verified by reading and building)

`cl_player.c` — the host of the client side:
- Session: `CL_Player_ConnectionStarting` (from `CL_SendConnectPacket`, before the connect's userinfo goes out) and
  `CL_Player_FrameStart` (demo playback: no connect packet) start a session: `predicting` is force-set to `0`
  (SPEC-003 "Client prediction": the predictor itself is not hosted yet, so the client must not claim to predict —
  a predicting client has to send fire times, SPEC-602 §3), the common (19 entries) and client (20 entries) import
  tables are built and `SV_LoadPlayerModule(common, client, NULL, -1)` — I1b's loader shared with the server side,
  one library, reference-counted — returns the export table; version checked (`player is version %i, not %i`),
  `Init` called, a prediction inventory created (export 9). `CL_Player_Disconnect` / `CL_Player_Shutdown` delete
  the inventory, call `Shutdown` and drop the reference (`SV_UnloadPlayerModule(1)`). When the server side is
  unloaded first, sv_player.c calls `sv_player_clientdrop` → the client ends its session too (SPEC-003 "Loading").
  `om_clplayer 0` disables the whole client side (as before I1d).
- Context flag (SPEC-003): −1 written at the start of every client frame (`CL_Player_FrameStart`), again after the
  packets (`CL_Player_Frame`), and before every inventory call; `clp.time` (client import 1) = the Ghoul game clock
  of I1c (`CL_Ghoul_GameClock`). The server writes 0 at the start of every server frame (`sv_main.c`, I1d hunk).
- svc 28 → slot 69 (read events) and, since the client never predicts, slot 70 + 68 at once; svc 29 → new
  inventory (export 10 + 9; on a non-local connection export 8 "forget the in-view models" too — observed in I7's
  proxy traces of the 1.07f client); svc 30 → slot 77 with a 20-byte module sizebuf over the 640 bytes of
  net_message. Without a client side the old skip code of cl_sof.c is kept (`CLP_SkipEvents`).
- Client imports: 0 `CL_Ghoul_System()`; 1 `&clp.time`; 2/3 predicted fire effects → `CLFX_PredictedFire(weapon−1 /
  weapon+11)` (I5, SPEC-400 "View weapon" 2); 4 note sound (below); 5 prints `WeaponKillSoundClient`; 6 note effect →
  `CLFX_ViewWeaponNote(note, shown, fov)` (I5); 7–9 MSG_Read* on net_message; 10–19 the module's own 20-byte
  sizebuf (SPEC-003 layout: allowoverflow, overflowed, data, maxsize, cursize, readcount).
- Note sound (import 4): `name [n|*]` → `<name>.wav` / `<name><irand(1..n)>.wav`; on the player's own entity,
  CHAN_WEAPON (1), volume 0.6, ATTN_NONE — observed on the original (OMsnd log `e1 c1 v153 a0.0`). `*` = random
  volume, range not in the spec (flrand(0.3, 0.6) here, unverified; OPEN_QUESTIONS below).
- **Engine note handlers while not predicting** (the whole point for single player): the module's client imports
  are never called by the original player.dll in single player (I7's traces), yet the original client plays the
  view weapon's `sound` / `effect` notes and the fire effects. So the engine itself registers `ViewNote` callbacks
  (`cl_playerghl.cpp`, SPEC-010 `IGhoulCallback`) for `sound`, `effect`, `fire`, `altfire` on the client Ghoul
  instance of player-state field 1 (the server-replicated view weapon, `CLG_FindInstance(gun_id)`) and processes
  its events once per client frame, after the packets, up to game clock + 0.001 s (`ProcessEvents`, SPEC-011
  method 45). The attach is redone every frame (remove + add) because instance ids and memory are reused.
  `fire`/`altfire` notes call `CL_Player_FireEffects(−1, …)` = the fire effects of the player state's weapon type.
- `CL_Player_Predicting()` is 0, `CL_Player_PredictedViewInstance()` NULL, `CL_Player_CommandFireTimes` 0/0:
  the predictor (SPEC-602) is not hosted — single player and listen servers never predict anyway.
- Test aid: `om_cpltrace 1` prints every note, fire effect, view-weapon handle change and svc 29.

`cl_playerghl.cpp` — the C++ layer: the inventory's 79-slot method table (MSVC thiscall; only slots 3, 4, 23,
61, 68, 69, 70, 77 carry argument lists) and the `ViewNote` callbacks.

Hunks in other files (all `I1d`): `cl_main.c` (Init/Shutdown, FrameStart before `CL_ReadPackets`, Frame after
it), `cl_network.c` (connect / disconnect), `cl_sof.c` (svc 28/29/30 → cl_player.c), `sv_main.c` (context 0 per
server frame), `sv_player.c` (`sv_player_clientdrop`), `sv_entities.c` (player-state field 1 is sent when the
weapon TYPE changes with the same handle — the original sends (31, 6), (30, 8), (31, 1) … at weapon switches,
`tests/I1d/vwstates.py` on the o_a3 demo), `game_api.h` (the two import tables, static asserts 76 / 80 bytes),
`engine/CMakeLists.txt` (sources). `player/build.cmd` honours `OM_BUILD_DIR` (X1's packaging line, left in).

Test aids in `tests/I1d/`: `run.ps1` (one scripted single-player session on a hidden desktop: `-Engine orig` =
original SoF.exe through I4a's capture shim, `-Engine om` = our engine + original modules, `-Engine open` = the
fully open stack; OMsnd `s_om_debug 4` start log + `s_om_wavefile` capture `snd.wav`; fixedtime 25 ms for
deterministic frames; ports 29560/29561), scripts `sp_pistol.cfg`, `sp_a.cfg` (pistols, sniper, shotgun, knife,
assault — after I7's sp_a), `sp_c.cfg` (slugger, rocket, machinegun, machine pistol, MPG, flamegun), `sp_clock.cfg`,
`*_rec.cfg` (demo recording), `demo_a.cfg`; `sndcmp.py` (sound starts of two logs per `OMTEST marker` segment,
aligned by name); `vwstates.py` (view-weapon fields, svc 27 records and svc 28–30 of a demo through
tools/net/sofnet.py); `notedump.cpp` (note events of every in-view sequence from the Ghoul library,
`openmerc-data\I1d\notes.txt`).

## Findings, session 2 (the fully open stack)
Evidence: `openmerc-data\I1d\x_a` (open stack, build-game-x86-Release + build-player-x86-Release), `x_p` (captures
`1.png`/`2.png`: after `elbow` the client still shows the SHOTGUN while the 9 mm fires, clip 18 -> 17), the client
demos `x_prec`, `m_prec` (our engine + original modules), `mp_prec` (original game + our player), `xo_prec` (our
game + original player) decoded with `tests/I1d/vwstates.py`, and the module trace `xt_prec` (`run.ps1 -PmTrace`).
1. **Stale game build.** With our game + our player, `elbow` left the player state at (instance 29, type 8): the
   module trace shows `NewInventory` -> `SelectSlot slot 1 weapon 3` -> `NewViewInstance ... inst 30` ->
   `WritePSWeapon ... ctx 00000000 owner 00000000 ps 00000000` — no `SetOwnerContext` between the game's
   `NewInventory` and its `SwitchToWeapon`. The game source has the fix (g_cmds.cpp `GiveKit`: `PlayerSpawning` /
   `PlayerSetIdleContext` before the kit, commit a5e9c76 2026-10-25 12:23), the owner's `build-game-x86-Release`
   is from 2026-10-22 09:38. The three other pairings work (demos: (30, 3) after `elbow`, records every change).
   So the "missing shotgun fire / reload sounds" of the open copy = the client animating and sounding the OLD
   instance (which the server no longer animates) while the shots come from the new, unreplicated one.
   Test copy rebuilt with `build-I1d-game-x86` (game source of the current tree, incl. other packages'
   uncommitted hunks of the moment). The owner's copies need a game rebuild; nothing to change in I1d for this.
2. **Same-frame instance id reuse** (`ghoul_net.cpp`, marked I1d, both sides): our module destroys the old view
   instance before creating the new one; the library's allocator gives the freed id back at once (SPEC-010), the
   removal is queued for the reliable stream, the new instance's first record goes out in the same frame's svc 27 —
   and the reliable message can lag (it waits for the acknowledgement of the previous one), so the client applied
   the new instance's state to the old instance it still held. Server: an instance whose id still has a queued
   removal for the client is not written until the removal went out. Client: a record whose object id is not the
   held instance's object destroys that instance (and its stored delta states) and creates the new one from the
   record. `omghoul_test`: 1243 checks, 0 failed. OPEN_QUESTIONS I1d-4.
   Honest limit: the loopback listen server acknowledges every reliable message within a frame, so the lag does
   not occur there (the stale-build `x_a` switched weapons under one reused id 30 with correct sounds before this
   change); the change is reasoned from the netchan rules and matters over a network. The server-side part only
   covers the case that the removal could not be written in the frame it was queued (budget), the client-side part
   the lag itself; a removal that never arrives after a replacement would leave one stale swallow for that id until
   the level changes (NetResetData).
4. **Goal 3, "Missing Child" after `load`** (`ghoul_net.cpp` NetWriteUnreliable, marked I1d): SPEC-013 rule 6
   ("an instance equal to its base is not written") was applied also to instances the client had never received.
   After a `load` the objects are re-created with their restored instances before the definition is written, so the
   baseline (= the lowest-id instance) equals that instance, which was then never sent, and the parents' bolt-on
   maps named children the client did not have. Now an instance is skipped for equality only once it is in the
   client's `sentInsts`. The print carries the ids now ("Missing Child (instance N bolt B of P)").
   Evidence (`tests/I1d/sp_load_kos1.cfg`, kos1: intro, `save`, `load`, walk): I1b's engine build
   (`build-I1b-x86`, before the change) with the original modules `l_old`: 4 × `Missing Child` after the load;
   this build with the original modules `l_m` and the open stack `l_x`: 0, the loaded game runs (registration 2,
   walk, quit). The six `Ghoul :GHOUL/ENEMY/... not found` lines are in I1b's b3_load log as well (pre-existing,
   not mine). `omghoul_test`: 1243 checks, 0 failed. I1b's "two Ghoul registry differences" (TAB in a `.gsq`
   name, backslashes in texture paths) not touched.
5. Test aid in our player module (I7's `player/src`, marked I1d): environment variable `OM_PMTRACE=1` prints
   `pm:S|C` lines for GetPlayer*API, New/DeleteInventory, SetOwnerContext, SelectSlot, NewViewInstance,
   WritePSWeapon through the common Com_Printf (`run.ps1 -PmTrace`). A cvar was not usable: the common table's
   `Cvar_Get` is the game's on the server side.

## Results, session 2 (2026-10-25 18:3x–18:4x; fixedtime 25 ms on both engines, OMsnd 0.3 `[t=]` real-ms stamps,
## both engines at the same ~5x wall speed; `openmerc-data\I1d\<tag>`)
Engines: `o_*` original SoF.exe + original modules; `x_*` fully open stack (our engine 18:35 + build-I1d-game-x86 +
build-I1d-player-x86); `m_*` our engine + original gamex86/player. Scripts: `sp_pistol.cfg` (`*_p`),
`sp_shotgun.cfg` (`*_s`), `sp_a.cfg` (`*_a`). Sound starts from the OMsnd logs (`sndcmp.py`), the OpenAL mix in
`snd.wav` (`wavstat.py`: duration / peak / 0.5 s windows with sound).

| Pair | sound starts (orig / OpenMerc) | identical by name+entity+channel+volume+attenuation | wav orig | wav OpenMerc |
|---|---|---|---|---|
| o_p / x_p pistol: 2 shots, reload | 9 / 9 | 5 (+ the 3 fire.wav and 1 casing sound, which differ only in the logged entity) | 3.6 s, peak 30135, 7/8 windows | 3.9 s, peak 28553, 6/8 |
| o_p / m_p (original modules) | 9 / 9 | the same | | 3.9 s, peak 28553, 6/8 |
| o_s / x_s shotgun: draw, 3 shots, 4-shell reload, hold fire until empty | 50 / 52 | 23 (+ 17 fire.wav / casing pairs differing in the logged entity; OpenMerc fires one shot more in the "fire3" segment: the original's first press came 1 frame later) | 6.7 s, 32767, 13/14 | 7.1 s, 32767, 13/15 |
| o_s / m_s | 50 / 51 | as x_s | | 7.1 s, 32767, 13/15 |
| o_a / x_a session A (pistols, sniper, shotgun, knife, assault) | 240 / 224 | 109; every reload / draw / cock / putin / dry-click note at the same real ms (±1–2 ms = within a frame): pistol1 reloadout/in/fin 1327/1407/1473 vs 1326/1406/1472, shotgun rattle/slap/cock/putin×4/cock, sniper cock, assault spindone/catch, pistol2 throw/putin | 17.6 s, 32767, 35/36 | 17.8 s, 32767, 34/36 |

What differs in `o_a / x_a` (16 starts): 12 × `weapons/sniper/zoom.wav` (the client-view zoom sound, SPEC-400
"View weapon" 3 — not a module note, not implemented, OPEN_QUESTIONS I1d-3); 1 × `weapons/dpistol/adjust.wav`
and the knife swing order (time-seeded idle variations, as I7's acceptance notes); the assault reload 145 real ms
(≈0.7 s game) earlier in OpenMerc (the manual `reload` waits for "ready", the preceding idle variation differs);
shell-casing sounds: random pick/chance/delay (SPEC-400 "View weapon" 2), counts within the expected spread.
Systematic, cosmetic: the original logs the muzzle effect's `fire.wav` with entity −1 and the casing sound with
entity 0; OpenMerc the other way round (I5's sound-start arguments; attenuation 0 / positioned sounds either way).

Shotgun: the owner's complaint (no fire sound, no reload sounds) does not reproduce with the rebuilt game module:
`x_s` has `weapons/shotgun/fire.wav` for each of the 11 shots, `putin` for every shell and `cock` at the end, at
the original's times. With the owner's `build-game-x86-Release` (`x_a` of 18:1x, before the rebuild) the view
weapon stayed the old instance after `elbow` and no note of the real weapon reached the client (Findings 1).

Captures (`-FixedTime 25`, 75 ms and 125 ms after `+attack`): `o_p\1.png`/`2.png` vs `x_p\1.png`/`2.png`.
At 125 ms the original shows the slide in full recoil, the casing in the air and the view kicked; OpenMerc shows
the fire sound and the `effect` notes delivered (console lines) but the model still near its idle pose and no
casing visible — the client's animation phase of the replicated fire sequence / the shell particle lag the
original by about one server frame there (I1c clock / I5 particle; not a sound issue; not investigated further).

## Results, session 1 (our engine + ORIGINAL modules vs the original, sp_a.cfg, `sndcmp.py o_a3 m_a4`)
- Reload / cock / draw / dry-click `sound` notes: identical names, entities, channels, volumes, attenuation and
  times (e.g. pistol1 reloadout/reloadin/reloadfin at 0/400/750 ms; shotgun putin ×4 + cock; sniper reload, cock,
  cock2; knife ready; assault slap).
- Fire sounds (`weapons/<w>/fire.wav`, from the muzzle effect of the `effect` note, volume 255, attn 0): present
  for pistol1, sniper, shotgun; logged with entity 0 where the original logs −1 (the sound-start path, not the
  effect).
- Shell-casing sounds (`weapons/fx/tink/*`, SPEC-400 "View weapon" 2): present but fewer / other random picks
  (random chance and delay — expected spread).
- Missing in OpenMerc: the sniper **zoom sound** (`weapons/sniper/zoom.wav`, e1 c0 v153 a1.0, every 200 ms while
  zooming; SPEC-400 "View weapon" 3 — the zoom-change sound is not implemented in the client view yet; not a
  player-module note). Knife swing notes come in a different order (time-seeded idle/stance randomness, as I7).

## Build / test
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I1d-x86&& tools\build\build-msvc.cmd x86 Release"
build-I1d-x86\src\ghoul\omghoul_test.exe                                  (after Ghoul library changes)
cmd /c "set OM_BUILD_DIR=<repo>\build-I1d-player-x86&& player\build.cmd Release"
cmd /c "set OM_BUILD_DIR=<repo>\build-I1d-game-x86&& game\build.cmd Release"
powershell -File tests\X1\assemble.ps1 -Dest D:\Games\SoF-test-I1d -Repo <repo> -EngineBuild build-I1d-x86\release -SoundBuild build-sound-x86-Release -GameBuild build-I1d-game-x86 -PlayerBuild build-I1d-player-x86
powershell -File tests\X1\assemble.ps1 -Dest D:\Games\SoF-test-I1d-orig ... -OrigModules
powershell -File tests\I1d\run.ps1 -Engine open|om|orig -Script tests\I1d\sp_shotgun.cfg -Tag <tag> [-FixedTime 0] [-PmTrace] [-OmPlayer <dll>] -Extra '+set om_cpltrace 1'
python -I tests\I1d\sndcmp.py openmerc-data\I1d\<orig>\client.log openmerc-data\I1d\<om>\client.log
python -I tests\X1\wavstat.py openmerc-data\I1d\<tag>\snd.wav
python -I tests\I1d\vwstates.py openmerc-data\I1d\<tag>\user\demos\i1dpistol.dm2      (after *_rec.cfg)
```
Windows PowerShell 5.1 (`powershell.exe`) for the harness (DeskRun.cs). `-FixedTime 25` (default) = deterministic
25 ms frames, the game runs ~5x faster than real time (the wave capture is then time-compressed); `-FixedTime 0` =
real time at 40 fps on both engines (`cl_maxfps` / `vid_maxfps`), for sound captures. OMsnd of
`build-sound-x86-Release` (0.3) logs `[t=<real ms>]`, the 0.4 builds `[p=.. cl=..]`; `sndcmp.py` reads both.

## Open questions
Recorded in OPEN_QUESTIONS.md "I1d" (1 note playback owner in single player, 2 import 4 `*` volume range, 3 sniper
zoom sound, 4 instance id reuse against the two Ghoul streams, 5 a server inventory without an owner context).

## Needs from I5
- none so far (`CLFX_ViewWeaponNote`, `CLFX_PredictedFire` exist).
