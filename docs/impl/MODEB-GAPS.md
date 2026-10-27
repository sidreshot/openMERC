# MODEB-GAPS: what the open modules (mode B) do today

Status: audit, 26 Oct 2026, run 22:44 to 22:58 (Asia/Yekaterinburg, UTC+5), on main `bb318d4`.
Author: C team (clean). No R material was read. No original executable or DLL was disassembled.
Direction ([pt48u], from the owner): drop the retail `gamex86.dll` and `player.dll`. Ship only our modules on yQuake2 (mode B),
and use the retail DLLs only as an information source.

The doc has three parts. Part 1 reports what the audit saw, with evidence. Part 2 lists the gaps and questions for R.
Part 3 lists the old engine parts we can now drop and the yQuake2 features to adopt. Nothing was deleted in this audit.

## 0. Method

| item | value |
|---|---|
| build | `build-AUD-{engine,sound,game,player}` from main `bb318d4`, MSVC x86, Release |
| install | `D:\Games\OpenMerc-modeB-audit` was assembled with `tests/X1/assemble.ps1 -Paks "D:\Games\Soldier of Fortune\base"`. The paks are hard links. There is **no** retail `gamex86.dll` or `player.dll`: our `gamex86.dll` is 1 254 912 bytes, the retail one is 1 544 192. sofhd and sofplus are not included. |
| parallel slots | 6 clones, `D:\Games\OpenMerc-modeB-audit-slots\s0..s5`, plus `s9` for the menu run. `xrun.ps1` kills foreign processes that share its exe path, so each slot needs its own clone. |
| ports | 29700-29709 and 29710-29719. The integrator's 29680/29681 were not used. |
| desktop | hidden desktop `OMTestX1`. No focus stealing and no synthetic input. `-portable`, with the user dir under `openmerc-data\aud\runs\<tag>\user` |
| harness | `tests/X1/xaudit.ps1` (orchestrator), `x1_audit.cfg` (SP), `x1_dmaudit.cfg` (DM), `x1_menuaudit.cfg` (menus). Evidence goes to `<openmerc-data>\aud\runs\` (`summary.txt`, `summary.jsonl`, `facts.jsonl`, and per run `client.log`, `snd.wav`, `*.png`, `user/watchdog.txt`) |
| SP script | intro wait (tsr1: 22×w1k, because of the 70 s opening cinematic) → `entlist`/`listpickups` → walk/strafe/jump/crouch → primary ×3 → alt ×2 → `reload` → `heretic` → kit 1 (`elbow`) with each weapon fired and alt-fired → kit 2 (`bigelbow`) the same way → `gimme m_kos_mgrunt1` 100 u in front → shoot it → `killallmonsters` → `entlist` → `save audit1` → walk → `load audit1` → walk/shoot → quit |
| chain script | `gamemap` through the campaign order, 5 maps per run, 8 runs |
| DM script | 10 maps × modes 1-7, `maxclients 6`, `bot_fillserv 1`: walk, fire, alt, scoreboard |
| runs | 32 SP, 8 chain, 1 save/load (`save_kos1`), 10 DM, 1 menu: **52 runs** |

What this method cannot see is in §1.9.

## 1. Results

### 1.1 Overview

| | runs | clean exit | crashed |
|---|---|---|---|
| SP maps (load, spawn, walk, fire, every weapon, AI spawn, killall, save/load) | 32 | 30 | 2: ger1 and ger3, both **on load** |
| level transitions (`gamemap`) | 8 runs, 25 transitions | 5 runs | 3 transitions: tsr1→tsr2, arm2→nyc1, sud3→arm3 |
| save/load on a live map (`save_kos1`) | 1 | 0 | 1, on load |
| DM/Assassin/Arsenal/CTF/Realistic/Control/Conquer with bots | 10 | 10 | 0 |
| in-game menus (12 pages) + console save | 1 | 1 | 0 |

Every one of the 32 SP maps loads, spawns the player, plays its intro and accepts movement, fire, alt fire, reload, weapon switching and cheats.
All 32 also produce sound: about 178 of about 186 s of the WAV are non-silent in each run (`snd` column in `summary.txt`).
None of the 51 runs hit an ERROR_DROP or a watchdog stall. The 6 crashes are all `c0000005` inside our modules (`user/watchdog.txt`).

### 1.2 Per map (SP)

Census = `entlist` Total/Monsters before / after killall. "killed" = the `killallmonsters` count.
save/load: **ok** means "Saving game… Done", then "Loading game… Done / Server restarted" with the position restored.
**dead** means killall raised the "too many friendly losses" mission failure, so the player died and the save was refused. That is a test artefact; see §1.6.

| map | census | killed | save/load | notes |
|---|---|---|---|---|
| tsr1 | 547/86 → 581/56 | 83 | dead | 70 s opening cinematic plays. During it "Cannot save during cinematic." is correct. String 060e undefined at mission fail. Chain: **crash on transition to tsr2** |
| tsr2 | 184/6 | 31 | ok | "Couldn't find gut death!" once (gore) |
| trn1 | 234/21 | 20 | ok | |
| tut1 | 296/3 → 296/4 | 3 | ok | `gimme` spawn visible as monsters +1 |
| kos1 | 534/68 → 589/67 | 66 | dead | `save_kos1`: save at intro OK, **load crashes** (read 0000051C in gamex86) |
| kos2 | 544/81 → 604/75 | 79 | dead | |
| kos3 | 826/89 → 884/92 | 90 | dead | 060e |
| sib1 | 469/63 → 518/56 | 60 | ok | the "Server disconnected" line seen earlier is the normal reconnect after load, the same on every load |
| sib2 | 480/67 → 515/41 | 65 | dead | "Soundgen with invalid name!", 060e |
| sib3 | 724/83 → 793/81 | 81 | dead | 060e |
| irq1a | 716/89 → 737/53 | 92 | dead | 060e |
| irq2a | 499/56 → 519/25 | 56 | dead | 060e |
| irq3a | 479/46 → 508/41 | 45 | dead | 060e |
| arm2 | 154/5 → 150/6 | 6 | ok | chain: **crash on transition arm2→nyc1** |
| nyc1 | 552/63 | 64 | refused | killall starts a scripted cinematic, so "Cannot save during cinematic." (correct) |
| nyc2 | 623/62 → 674/63 | 61 | ok | |
| nyc3 | 485/85 → 545/76 | 84 | dead | |
| sud1 | 620/80 → 651/49 | 81 | dead | |
| sud2 | 650/68 → 666/47 | 69 | ok | |
| sud3 | 769/102 → 851/103 | 103 | dead | chain: **crash on transition sud3→arm3** (`c0000409`, fast-fail, no OMCRASH report) |
| arm3 | 101/1 → 101/2 | 1 | ok | |
| jpn1 | 565/46 → 603/42 | 46 | ok | 060e |
| jpn2 | 666/81 → 683/49 | 82 | dead | 060e, "Can't find material:gun" |
| jpn3 | 300/12 → 296/11 | 13 | ok | |
| irq1b | 719/78 → 749/46 | 78 | dead | 060e |
| irq2b | 686/89 → 737/76 | 86 | dead | 060e |
| irq3b | 734/79 → 736/33 | 80 | dead | 060e |
| ger1 | 448/44 → 492/45 | 45 | **save ok, load crash** | read 2C30A954 in gamex86 during the load's game init |
| ger2 | 711/56 → 768/57 | 57 | ok | material:gun |
| ger3 | 607/50 → 654/51 | 51 | **save ok, load crash** | read 2FBDD6DC in gamex86, material:gun |
| ger4 | 354/36 → 378/26 | 37 | dead | |

Transitions that worked (22): kos1→kos2→kos3→sib1→sib2, sib2→sib3→irq1a→irq2a→irq3a, irq3a→arm2,
nyc3→sud1→sud2→sud3, arm3→jpn1→jpn2→jpn3→irq1b, irq1b→irq2b→irq3b→ger1→ger2, ger2→ger3→ger4.

### 1.3 Per feature

| feature | state | evidence |
|---|---|---|
| map load / spawn | works on 32/32 SP maps and 10/10 DM maps | `summary.txt`, `a0_start.png` |
| player movement (walk, strafe, jump, crouch) | works. Positions change as scripted | `facts.jsonl` `positions` |
| weapons, primary | all 13 cheat-kit weapons select, draw and fire. Ammo counters drop | `sp_kos1\w1..w13.png`, contact sheet `aud\c_kos1w.png` |
| weapons, alt fire | the command is accepted on every weapon and no errors are logged. Whether each alt mode does the right thing is **not verified** (no per-weapon event trace) | logs |
| reload | accepted, no error. Animation not verified | |
| HUD | health/armour bars, ammo counter, weapon and item icons and the inventory select are drawn. CTF shows the team flag and a team scoreboard | `menu_kos1\hud*.png`, `aud\c_dm1.png` |
| AI | NPCs spawn (census), `gimme m_kos_mgrunt1` adds a monster. The harness-fired shots **cannot prove a hit or kill** from 4 screenshots. Every AI died to killall. Behaviour is only as good as G2/G4/G7/G18 already document | `ai*.png` |
| gore / deaths | killall deaths go through the death code. "Couldn't find gut death!" on tsr2 | `sp_tsr2\client.log` |
| items / pickups | `listpickups` and `entlist` list the expected item classes (e.g. kos1: 14 pickup lines, item_weapon_* and item_equip_armor) | census in `client.log` |
| scripts / cinematics | tsr1 opening cinematic with credits runs about 70 s. The nyc1 scripted cinematic triggers. The friendly-loss mission failure fires | `sp_tsr1`, `sp_nyc1`, `sp_kos1\ai3_killall.png` |
| triggers / level end | a real level end through the exit trigger is **not exercised** (the harness uses `gamemap`, the same gap as I1b) | |
| savegame | save works everywhere it is allowed. Load works on 12 maps and **crashes on 3** (ger1, ger3, kos1 live) | §1.2 |
| level transitions | 22 of 25 OK, **3 crash** | §1.2 |
| in-game menus | main, game, save, load (with thumbnail), video, audio, keys, options, misc, player, stats and quit all open over a running game and draw | `menu_kos1\menu_*.png`, `aud\c_menu.png` |
| MP modes | DM, Assassin, Arsenal, CTF, Realistic, Control and Conquer start a listen server. 5 bots join, the scoreboard shows them, and the player walks and fires | `dm_*`, `players_lines` in `facts.jsonl` |
| sound | OMsnd writes about 178 of 186 s of non-silent output on every SP map. The EAX environment per map is loaded (`maps/<map>.eal`). Per-event correctness is **not verified** | `snd.wav`, `summary.txt` |
| text / strings | string ID 060e undefined, printed empty, on 12 maps at mission fail | logs |

### 1.4 Crash class A: savegame load (ger1, ger3, save_kos1)

On all three crashes the stack sits only in `gamex86.dll`, under the engine's `Z_Touch`/`Sys_Error` frames. The last checkpoint is "after game shutdown": the game DLL was re-initialised for the load and crashed while reading the level.
`save_kos1` was saved 5 s after spawn, with 68 live NPCs. ger1 and ger3 were saved after killall, with 45 and 51 corpses.
Loads with fewer or simpler entities worked (arm1-3, trn1, tut1, tsr2, nyc2, jpn1, jpn3, sib1, sud2 and ger2).
This matches the G18/G10 handover items (savegames, transient statics, G7 "transient statics not saved"). It **blocks normal play**: quick-load in the first area of kos1 crashes.

### 1.5 Crash class B: level transition (tsr1→tsr2, arm2→nyc1, sud3→arm3)

The crash comes in `SV_Frame` right after the "Changing map / reconnect" message. Inside the game module, `player.dll` makes an **execute of address 00000009 / 3F800000**: a call through a function pointer that holds an integer or float (1.0f).
This looks like a stale callback or table pointer that `player.dll` keeps across `ShutdownGame`/`InitGame`, or a client persistent block read with the wrong layout.
sud3→arm3 ended in `c0000409` (fast-fail or stack cookie) with no report. 3 of 25 transitions crash, so a full playthrough breaks.

### 1.6 Test-method artefacts (not product bugs)

- On maps with friendlies, `killallmonsters` triggers the mission failure ("too many friendly losses"), so the player is killed even under `heretic` and the save is refused ("Can't savegame while dead!"). This is plausibly correct game behaviour. Saves on such maps are covered by `save_kos1`.
- "Server disconnected, reconnecting" after a load is the normal local-server restart.
- `Unknown command "-score"` comes from the DM script.
- `NET_IPSocket ERROR: getaddrinfo` ×2 per DM run: the master/GameSpy lookup has no network on this machine. Harmless, but it belongs to the GameSpy code in §3.

### 1.7 Assets that resolve to nothing (every run)

The Ghoul loader reports "not found" for names such as `GHOUL/ENEMY/MESO/B_BASIC` (52 runs), `BOLT/ACC_BOX_HIP_BRWN` (39),
`ACC_HAT_BERET` (25), `ACC_HOLSTER` (23), `ECTO/[AB]_NYCSKINHLEADER` (19), `ACC_BACKPACK_LOW` (12),
`ACC_POUCH_THIGH_GREEN`, `OBJECTS/GENERIC/{FORKLIFT,BANNER_GENERIC/BANNER1,CART,PILLOW,TABLE_ROUND}`, `UGANDA/ROCKET_ENGINE/ENGINE2`
and `ACC_HAT_GENERAL{,2}`. The paks do have `ACC_BOX_HIP.ghb`, `ACC_BOX_HIP_LRG.ghb` and skins like `acc_box_hip_blk.tga`,
but no `ACC_BOX_HIP_BRWN` model and no `meso/b_basic`. So our code asks for a **skin or variant name as a model name**, or the name has to go through an `.ifl` or skin table.
Opening the MP **player** menu adds about 40 more (`MESO/A_BAB`, `COMSKIN/F_BAB_*`, ...), and the player-model preview there is incomplete.
"Can't find material:gun" (ger2, ger3, jpn2, nyc1) and "material:(null)" (gerctf1, sudctf2) are similar. Players see missing hats, boxes, holsters, NPC faces and props.

### 1.8 Menus (visual)

All 12 pages draw (`aud\c_menu.png`). Seen:

- the **main** page is drawn at quarter size in the top-left corner of the in-game shot;
- the **audio** page still offers the SoF "sound driver / 22 kHz / 16 bit" items, which no longer apply to OpenAL OMsnd;
- **stats** is empty mid-game.

### 1.9 Not covered by this audit

- per-weapon alt-fire semantics;
- AI tactics, perception and hit reaction;
- per-event sounds;
- real trigger-based level end;
- exact position, health and inventory after load (positions only);
- MP with real remote clients;
- gamepad.

## 2. Gaps: top 10 by player impact

1. **Savegame load crashes** in `gamex86` (ger1, ger3, kos1 with live NPCs). **FIXED** on branch `modeb-crashes` (SPEC-AMEND-012; see section 5).
2. **Level transition crash** in `player.dll` (stale pointer after map change), 3 of 25. **FIXED** on branch `modeb-crashes` (SPEC-AMEND-013; see section 5).
3. **Real level end** via the exit trigger and the mission-complete flow has not been tested once in mode B (I1b). Players reach it on every map.
4. **Missing Ghoul parts**: hats, boxes, holsters, NPC heads and props, on every map. §1.7.
5. **AI combat quality** is only as far as G2/G4/G7/G18 got:
   - wallguy perception, tsr2 runners, kos2 bosguy2 fire cadence (G2, G2.19 partly answered by AMEND-008);
   - NPC muzzle flash at the origin instead of the `flash` bolt, and locomotion sequences (G18);
   - helicopter numbers G4 12/14/16/17, tank damage, dogs without a stock model (G4);
   - fallover, 5 classes only code-reviewed (G7).
6. **Gore**: "Couldn't find gut death!" (tsr2); the ghoul stream is not compared (G7).
7. **Undefined text, string 060e**, at mission fail on 12 maps: the player sees an empty message.
8. **Menus**: the main page is mis-scaled, the audio page is obsolete, and the player-model preview is missing in MP. Stats is empty.
9. **Script/cutscene details**: tsr1 token booth (G2), the 15 helper volumes and the tut1 overflow (G18), the input-freeze flag (AMEND-011 gate 1).
10. **Sound details**: zero-length (0 ms) notes (R11.3) and "Soundgen with invalid name!" (sib2, chain kos1/sib2). Per-event sound has not been compared.

### 2.1 Questions for R (behaviour only)

1. When the original game loads a save on ger1, ger3, or kos1 with live NPCs, which entity fields and classes are restored, and which are re-spawned from the map? In particular: are corpses, transient statics and NPC Ghoul instances written to the save at all?
2. Across a level change (`gamemap` or the exit trigger), what does the original player module keep from the old level (client persistent data, inventory, weapon state, callback tables)? Does it re-register its function tables on every `InitGame`?
3. Does the original Ghoul loader ever ask for names such as `ACC_BOX_HIP_BRWN`, `MESO/B_BASIC` or `ECTO/A_NYCSKINHLEADER` as files? Or are they skin or variant names resolved through a table? If a table, what is it and where does it live?
4. Which text does the original print at the friendly-losses mission failure, and is string ID 060e defined in a string pack we don't load?
5. What does "gut death" select in the original death code? Which tsr2 NPC asks for it, and what is the fallback?
6. Does the original kill the player under god mode when the friendly-loss mission failure fires?
7. What does the original stats page show mid-game in SP?
8. When the player-model preview in the MP player menu needs a model, which files does it load (the `COMSKIN/F_*` and `MESO/A_*` names)?
9. For each weapon, what exactly does alt fire do (mode, ammo use, rate) and does `reload` interrupt it?
10. What is the identity of the input-freeze flag (AMEND-011 gate 1), and how is view-model attachment placement handled (OPEN_QUESTIONS)?
11. Does the original honour sounds with 0 ms length (R11.3), and what does it do for an `environ_soundgen` with an empty or invalid name?
12. Does a level end through the trigger save an `auto` game before the next map loads? What is carried over (health, armour, ammo)?

## 3. Engine cleanup (proposal only; nothing deleted)

### 3.1 Parts that existed only for retail DLLs or retail SoF.exe and can go

| part | files | why it existed | risk if removed |
|---|---|---|---|
| SoF-ABI renderer `ref_omgl.dll` | `engine/CMakeLists.txt` ~905-922 (`OM_SOFREF`), `engine/src/client/refresh/gl3/gl3_sof.c`, `gl3_win32.c` | to drive the retail SoF.exe | low, as long as no test still launches SoF.exe |
| SoF.exe named exports | `engine/src/common/sof_exports.c` | retail sound DLLs import from the exe | low; only the retail sound DLLs need it |
| SPEC-004 sound host ABI | `engine/src/client/snd_host.c`; EAX/A3D/defsnd remnants in `modules/sound/src/snd_efx.c`, `snd_dma.c` | loading retail `*snd.dll` | medium: OMsnd uses it today. Replace with a direct static sound backend first |
| retail import-ring diagnostics | `engine/src/server/sv_imptrace.c`, `sv_watchdog.c` (import tracing) | diagnosing retail gamex86 | low (keep the watchdog crash report) |
| pure-check retail checksums | `engine/src/server/sv_init.c:408-417` (`0x64C0FC54`, `0xEACC2EEA`) | pure bit for the shipped DLLs | low; replace with our own module hash |
| registry parental-lock store | `engine/src/common/sof_locks.c` (`HKCU\Software\Raven Software\SoF`), `player/test/sof_locks.py` | retail lock compatibility | low; move it into the config file |
| protocol 33 | `engine/src/client/cl_sof.c:59`, `engine/src/server/header/server.h:45`, vs yQuake2 `common.h:213` (34) | joining retail servers | decided 2026-10-26: no retail interop (legal cleanup removed the network tables; no compatibility mode) |
| GameSpy | `engine/src/server/sv_gamespy.c`, `engine/src/common/gamespy_chat.c` (~2200 lines), `client/cl_serverlist.c` | dead master servers | low; getaddrinfo errors go away |
| force feedback | `engine/src/client/cl_input.c` FF paths, gl3 ghoul hit hooks | retail FF API | low |
| unused yQuake2 baseq2 game | `engine/src/game/` (21 files) → `base\game.dll`; `quake2.exe` wrapper; `client/cl_cin.c` (Q2 .cin) | upstream leftovers | low; **game removed on branch `cleanup`** (`CLEANUP.md`) |
| unused renderers | `GL1_RENDERER`/`SOFT_RENDERER`/`GLES3_RENDERER` options (`engine/CMakeLists.txt:66-70`), `client/refresh/{gl1,soft}` | upstream | low for soft and GL1; keep GLES3 if ARM is wanted; **soft, GL1, GLES1 removed on branch `cleanup`** |
| retail cvar table | `engine/src/common/sof_cvartable.h`, `sof_cvarreg.c`, `tools/build/gen_sof_cvartable.py` | SPEC-302 parity with retail cvars | medium; first keep the cvars that the modules read |
| two pmoves | `engine/src/common/pmove.c` (Q2) vs `sof_pmove.c` | Q2 and SoF prediction side by side | medium; keep only `sof_pmove` |
| 99-entry SPEC-001 game ABI | `engine/src/server/sv_game.c`, `game_api.h` | binary compatibility with the retail gamex86 | **high**; the big refactor. Only after mode B is the only target; see 3.2 |
| oracle and proxy tests | `game/probe/rfproxy.cpp`, `game/src/save/test/g10proxy.cpp`, `game/src/scriptglue/test/spproxy/`, other `*/test` scripts that load original DLLs | comparing against retail | low; keep them on a branch or tag for R work |

### 3.2 yQuake2 features to adopt now

- **Native game API**: our game and player in one module or a static link. This brings 64-bit and Linux builds and drops the 32-bit-only MSVC constraint.
- **Protocol 34** and the Yamagi netcode (retail interop is dropped, so this is open to decide).
- **Yamagi OpenAL sound** as the backend, with OMsnd's SoF logic (`.eal` environments, sound sets) on top, instead of the DLL ABI.
- **ref_gl3 / GLES3**, and Vulkan later.
- **SDL3 input**: gamepad, gyro and rumble, replacing force feedback.
- **XDG and portable home paths**, plus curl/HTTP downloads for MP.
- **Yamagi menu framework** for the remaining menu pages.
- **Yamagi savegame layout**: per-slot directories, versioned, with a header.

Suggested order:
1. Fix §2 items 1 and 2.
2. Static sound backend.
3. Drop `ref_omgl`, `sof_exports` and GameSpy.
4. Merge game and player and switch to the native API.
5. 64-bit.

## 4. Reproduce

```
powershell -File tests\X1\xaudit.ps1 -Parallel 6
python <data>\aud\facts.py <data>\aud\runs
```
`xrun.ps1 -Script tests\X1\x1_menuaudit.cfg -Map kos1` runs the menu audit.


## 5. Fix verification (modeb-crashes, 26 Oct 2026 ~23:30 Asia/Yekaterinburg UTC+5)

Worktree `<worktree>`, branch `modeb-crashes` rebased onto main `900a960` (SPEC-AMEND-012/013/014 approved).
Install `D:\Games\OpenMerc-modeB-crashes` from `build-MODEB-{engine,game,player}`. Ports 29720-29733. Harness `tests/X1/xrun.ps1 -Unlock`.
Evidence: `<openmerc-data>\modeb-crashes\`. No R material read beyond the approved amend specs.

### 5.1 SPEC-AMEND-012 — save / load (crash class A)

Cause: `G_HelperUnlink` ← `G_HelperAppend` ← `om_ai::ReadAll` ← `ReadLevelAI` ← `G10_ReadLevel`. `edict_t::helperlink` was missing from `ent_fields`, so a raw absolute pointer survived load (gatekeeper note on AMEND-012).

Patches:
- `game/src/save/save_records.cpp`: `EF(helperlink, FK_ENTITY)`.
- `game/src/ai/ai_save.cpp`: clear every `helperlink` before the helper relink loop in `ReadAll`.
- `game/src/g_utils.cpp`: `helper_ptr_ok` guards on Append/Unlink.

### 5.2 SPEC-AMEND-013 — player API across level change (crash class B)

Cause: listen-server map change left client context set while Ghoul view instances were already freed; `PlayerDeleteInventory` → Destruct → Destroy on stale pointers. `weaponmode==0` maps skipped invent recreate.

Patches:
- `engine/src/server/sv_user.c`: clear `*sv_playercontext` around `ge->ClientBegin`.
- `game/src/g_player.cpp`: `PlayerDeleteInventory` forces server context for the delete.
- `player/src/pm_inventory.cpp`: Destruct destroys Ghoul instances only when `IsClient()`.
- `game/src/p_client.cpp`: `weaponmode==0` still deletes then `PlayerNewInventory`.
- `game/src/g_main.cpp`: `ShutdownGame` deletes every `client->inv` before `PlayerModuleUnload` so InitGame re-queries `GetPlayer*API` with no dangling invent pointers.

### 5.3 SPEC-AMEND-014 — gore / TE 18 scale (open-module + client)

Census (code path, before change): game emits flesh TE 18 as chunk type 32 / size class 4 (head explosion + per-hit puff). Client `TE_Debris` clamped `size > 3` to 3, so class 4 selected `ROCK_LG` (half-person) instead of `ROCK_TINY`. No `chunks_meat` / `BodyHuman_DebrisBurst` emitters in open game module; `gibbody`/`mpgsplatter` only on full `Gib()`, not on `Sever` (caps + bloodcloud/stumpblood).

Patch:
- `engine/src/client/fx/fx_tent.c`: rock/wall series allow size class 4 → model index base+3 (`ROCK_TINY` / wall tiny). Applies to mode A and mode B (OpenMerc client).

### 5.4 Repro results (all exit 0)

| tag | action | result |
|---|---|---|
| mbc_save_kos1 | kos1 save→load | pass |
| mbc_save_ger1 | ger1 save→load | pass |
| mbc_save_ger3 | ger3 save→load | pass |
| mbc_smoke_sib1 | sib1 save→load | pass |
| mbc_chain_tsr1 | tsr1→tsr2 | pass |
| mbc_chain_arm2 | arm2→nyc1 | pass |
| mbc_chain_sud3 | sud3→arm3 (noweapons) | pass |

Risks: full 32-map audit not re-run; old mode-B saves written without `helperlink` FK still need the clear-before-relink path (present). Flight regression is another worker. Persistent blood pools (R14.11) out of scope.

## 6. Gap closing (branch `modeb-gaps`, 27 Oct 2026, Asia/Yekaterinburg UTC+5)

Base `679c2fe`. Commits (oldest first):

| commit | change | evidence |
|---|---|---|
| `a988df2` | strings: entries a shadowing package copy lacks are filled from the next copy (060e, SPEC-AMEND-014 R14.2) | `om_strings 060e` prints `"Check your fire!"`; no "String ID" line in any A, B0 or B1 log |
| `c57b5fc` | mission statistics counters, `getgamestats`, `updateinvfinal` (SPEC-AMEND-007 G18.41, R14.5) | kos1: mid-game and end pages filled with the same values as mode A |
| `1255a28` | automatic gun: hidden yaw mount bolted without a skin | "Can't find material:gun" gone in B1 on ger2, ger3, jpn2, nyc1 (table) |
| `649f85d` | chat: the message is sent without the sender name (it was shown twice) | chat line identical to mode A |
| `58ac1a7` | `gimme` prints "Spawning : <class>" without `developer` | printed in A and B (`gaps\t\gimme`, `t\mz6A`/`mz6B`) |
| `7e4a0ad` | character muzzle effect on the weapon's `flash` bolt (SPEC-782 2) | builds; not seen on screen (scripted grunts never fired at the player) |
| `46b7967` | no-op `if` in `door_open_team`, bool/qboolean compare | game module builds without warnings |
| `4508a16` | helicopter ricochet sparks per SPEC-AMEND-008 G4 (origin, types 0/10/14, 0.3 s gate, ric1-6) | builds; not run on g4jpn3d |

Measured without a code change: sniper zoom sound. kos1, `s_om_debug 4`: `weapons/sniper/zoom.wav` starts 7 times
in A and 7 times in B for the same zoom input, so the KNOWN_ISSUES entry was out of date.

### 6.1 Per map, before (B0 = `main`) and after (B1 = `modeb-gaps` without `46b7967`/`4508a16`)

Same audit script (`xaudit_gaps.ps1 -Only sp`) in all three columns. caps = screenshots taken; it depends on how far
the script got before the 224 s limit and is not a quality measure. Runs that finished early in B1 (22 caps,
exit 0) ran while the machine was idle and reached the AI, kill-all and save/load sections; under load every run in
both modes stops around `w5`. "Ghoul not-found" counts are the data quirk in 1.7 (same lines in mode A).

| map | caps A/B0/B1 | Ghoul not-found lines A/B0/B1 | other warnings, A | B0 only (vs A) | B1 only (vs A) |
|---|---|---|---|---|---|
| arm1 | 8/8/8 | 3/3/3 | - | - | - |
| arm2 | 8/8/8 | 1/1/1 | - | - | - |
| arm3 | 8/8/8 | 1/1/1 | - | - | - |
| ger1 | 8/8/22 | 2/2/2 | - | - | - |
| ger2 | 8/8/22 | 2/2/2 | - | WARNING: Can't find material:gun x1 | - |
| ger3 | 7/7/22 | 2/2/2 | - | WARNING: Can't find material:gun x1 | - |
| ger4 | 8/8/22 | 2/2/2 | - | - | - |
| irq1a | 8/8/7 | 5/4/4 | - | - | - |
| irq1b | 7/7/22 | 5/5/5 | - | - | - |
| irq2a | 8/7/8 | 8/8/8 | - | - | - |
| irq2b | 7/7/22 | 9/9/9 | - | - | - |
| irq3a | 7/7/8 | 6/6/6 | - | - | - |
| irq3b | 7/7/8 | 6/6/6 | - | - | - |
| jpn1 | 8/8/8 | 3/3/3 | - | - | - |
| jpn2 | 8/8/8 | 2/2/2 | - | WARNING: Can't find material:gun x1 | - |
| jpn3 | 7/7/22 | 1/1/1 | - | - | - |
| kos1 | 8/8/8 | 6/6/6 | - | - | - |
| kos2 | 8/8/8 | 6/5/5 | - | - | - |
| kos3 | 8/8/7 | 8/8/8 | - | - | - |
| nyc1 | 7/7/8 | 3/3/3 | - | WARNING: Can't find material:gun x1 | - |
| nyc2 | 8/8/8 | 3/3/3 | - | - | - |
| nyc3 | 8/8/8 | 3/3/3 | - | - | - |
| sib1 | 8/8/6 | 2/2/2 | - | - | - |
| sib2 | 8/8/6 | 4/4/4 | - | Soundgen with invalid name! x1 | Soundgen with invalid name! x1 |
| sib3 | 8/8/6 | 4/4/4 | - | - | - |
| sud1 | 8/8/8 | 8/7/7 | - | - | - |
| sud2 | 8/8/8 | 6/6/6 | - | - | - |
| sud3 | 7/7/8 | 7/7/7 | - | - | - |
| trn1 | 5/5/8 | 3/3/3 | - | - | - |
| tsr1 | 0/0/0 | 6/6/6 | - | - | - |
| tsr2 | 5/5/8 | 2/2/2 | - | Couldn't find gut death! x1 | Couldn't find gut death! x1 |
| tut1 | 5/5/8 | 3/3/3 | - | - | - |

B-only lines left: sib2 "Soundgen with invalid name!" (`sib2_catsound` has no sound name; SR-MODEB-01) and tsr2
"Couldn't find gut death!" (random death zone; R14.3 says the original prints it, A did not hit it in this run).
Ghoul counts irq1a 5/4/4, kos2 6/5/5, sud1 8/7/7: only mode A asks for `GHOUL/ENEMY/BOLT/ACC_HOLSTER` (absent from the data, so nothing is drawn in either mode); all other names match.

B1 runs that reached the late sections: ger1-ger4, irq1b, irq2b, jpn3 save -> load pass; on irq1b `killallmonsters`
also kills the friendlies and the mission fails ("too many civilian casualties"), which is the expected reaction.
No mode A run reached these sections, so they are not compared.

### 6.2 Spec requests (`openmerc-data\spec-requests`)

- SR-MODEB-01: soundgen without a name, developer-only print or not, entity removed or kept.
- SR-MODEB-02: R14.8, coupling of the cinematic state to the client input freeze.
- SR-MODEB-03: R14.4, god mode against the friendly-loss mission fail.
- SR-MODEB-04: a `gimme` grunt on kos1 never attacks a god-mode player in front of it for 10 s.

### 6.3 Still open

- AI (G2): `wallguy` (kos1) perceives a still player ~100 units behind it in the original and not in OpenMerc
  (SR-MODEB-05); tsr1 booth: pistol shots now 43 vs A 46/38 (`c3b08b5`), 8 shots still refused at very close range;
  the far shotgun shooter near (940, -97) fires 6 times vs A 11; kos2 `bosguy2` startles once (1 s) after its senses
  come back with the end of the script (first shot 4.4 s after the trigger, original 3.5 s); tsr2 runners' alert
  stand after their run not re-measured; ninja: wake voice, fire count (B 400 vs A 272), duplicate rows
  `prn_a_n_mrs_2` / `std_paim_n_a_n` / `cch_raim` not added.
- Alternate fire (SPEC-600); R14.8 input freeze; MP character-page preview (R14.6); audio page legacy options.
- Helicopter sparks (`4508a16`): built per SPEC-AMEND-008 G4, no screen frame of them yet; the real level end of
  each map was not played.

### 6.4 Second pass (27 Oct 2026, after the reboot)

| commit | change | evidence |
|---|---|---|
| `9cd9dbe` | machine pistol fire stances: aim-flag second rows of `cch_a_fwd_p_2` / `std_a_fwd_p_2` (G3) | ninja test (`ninja\`): the machine-pistol ninja fires its stances in B (400 shots vs A 272 in the same run length) |
| `613ae54` | script kind 8 (`ai(x, 99)`) makes the end-of-script action, class 6 (SPEC-710 D3 row "8 (script stand)") | kos2 `kb`: `bosguy2` assault-rifle shots in 30 s after the girder trigger: A 120, B before 0 (scripted decision prio 40 forever in `std_istand_n_a_n`), B after 105 (`kbB3`); also releases the tsr1 booth shooters and `roundguy`/`tokchaser`/`tollguy` (intoken.os) |
| `a278965` | a character that has fired at a player is no longer "just noticed" (no startle after it) | tsr1 `tk`: booth shooters 434/436 startled 1 s after `ai(x, 99)` (`tkB`), none after (`tkB2`); the original's keep firing |
| `c3b08b5` | the body's fire facing test is horizontal (resolves the G2.md:550 note); the ground fire point lies at least 128 units out | tsr1 `tok\tkB*`: the crouched booth pistol shooter 435 (player ~25 units in front) refused most shots as "facing away" (`fire ... -> 0`, 15 of 32); pistol shots after the walk: A 46 / 38 (`tkA`, `tkA2`), B 24 before, 39 with the horizontal test only (`tkB5`, `tkB6`), 43 with both (`tkB7`); booth shotgun 6 in A and B (the weapon assignment matches) |
| `fa5d0c3` | `PmInstId` (player module) asks a view instance for its id only with `OM_PMTRACE` on, and `DeleteInventory` traces pointers; watchdog prints `module+offset` for modules without symbols | tsr2 -> trn1 auto level change crashed in ~40 % of the runs in B (also in the 10:17 build), exit c0000005 "execute of address 2F8ADF10", stack `SpawnLoadout` -> `PlayerDeleteInventory`: the trace arguments were evaluated with the trace off and called through the kept server inventory's view instance, which the level's Ghoul reset had freed; `run2\trF1..6`: 6 of 6 reach trn1, no crash |

The D3 row "8 -> class 6" was keyed on the record's animation class, but records have classes 0-6 only, so the
row never applied: every `ai(x, 99)` in the game (kos2 girder, tsr1 token booth and many more) only played
`std_istand_n_a_n` and left the character under script control, without its withheld senses (spawnflags 2/4).

Audits A2 (original DLLs, `runs_A2`, `runA2.log` + `runA2b.log`) and B3 (`T` build of 10:17 = `9cd9dbe`, `runs_B3`,
`runB3.log`), same script, `-TimeoutScale 4`, one instance: all 32 maps reach the end of the script in B (22 caps
each); A stops early on sud1 and sud2 (19 caps) with `CRASH: exception c0000005, last at "SV_Frame"` in the
original game DLL. Sound starts per map are within the run-to-run spread except where A's fights went on longer
(jpn1 256/179, jpn2 306/177, sib1 220/180, kos3 196/177, sib2 199/178: A/B started). Issue lines that differ:
random roster accessories (`ACC_HOLSTER`, `ACC_HAT_BERET`, `ACC_BOX_HIP_BRWN`, `*NYCSKINHLEADER`, both modes),
`Server disconnected, reconnecting` (arm2, arm3 in B; tsr2 in A), tsr2 "Couldn't find gut death!" (B, R14.3), the
two A crashes. No new B-only warning.

Black-box tests (`openmerc-data\gaps\<dir>`, timelines `*.txt` run with `tests/X1/xtl.py` + `xrun.ps1`):

- kos1 `wallguy` (`wall\`, 16 placements): proximity (< 75 units) and fly-over reactions (aim up
  `std_a_up_mrs_2`, then fire) match. The original also reacts to a still player ~100 units behind it on its +y
  side (bearings 120-165), not at 180/100, 165/125, on the -y side or 150 units behind; B only inside proximity or
  the cone. No rule known: SR-MODEB-05.
- SR-MODEB-04 (`gimme` grunt never attacks): same behaviour in A and B at the same spot; closed as parity.
- kos2 boss (`boss\kb*`): see `613ae54`. The original's many `weapons/shotgun/fire.wav` starts at the player's own
  position (517 in A, 58 in B) are hits on the god-mode player, not shots of his (ammo unchanged).
- tsr1 token booth (`tok\tk*`): walking on the ground through `trigger_once` *70 starts `tsr1/intoken` in B
  (earlier "never woken" came from the noclip landing variance); the scripted chase and the victim's death happen in
  both modes.
- Screens on the box: `/workspace/modeb_check/` `n2Bf_sheet.png` (ninja), `wgf_sheet.png` (wallguy from behind),
  `kb_sheet.png` / `kbB2_sheet.png` (kos2 before/after: tracers and flashes from `bosguy2` after the fix),
  `tk_sheet.png` / `tk_detail.png` (tsr1 booth A top, B bottom).

### 6.5 Third pass (27 Oct 2026, afternoon)

| commit | change | evidence |
|---|---|---|
| `bab7694` | a combat-point holder without wait actions stands aiming at a valid target; trace prints the wake origin, classname and targetname | tsr2 with the intro played (G2 `gaps_r0_*`, `gaps_r1_*`): runners 78/89 start `std_a_fwd_ms_2` at 32.8/33.0 s (original) vs 32.6/32.9 s; sparkrunner 117 at 50.7 vs 50.6 s. The original's later `std_ialert_n_mrs_2` (about 49 s after arrival) is not re-measured |
| `744f813` | crouched with an empty clip: the crouched reload `cch_l_fwd_<w>`, or stand up first when the model has none; level voice dir from a stock level name contained in the map name | g4jpn3d ninja test (`ninja\n3T` before, `n5T` after, `ai_trace 2`): crouched ninja 277 (type 5, clip 33) fired 102 times in one burst with no reload; after the fix it fires 33, stands up (`cch_xcrouch_up_pk_a`), reloads (`std_l_n_p_2`), crouches again: 72 shots in 16 s, the same as A's ninja (72 shots, reload between bursts). Wake voice `enemy/ni1/jpn3/wake3.wav` at 4.7 s (A: `wake1` at 5.5 s); before, the map fell back to `TSR1` and no wake file was found. SR-MODEB-06 asks for both original rules |

Corrections and results:
- The old ninja figure "B 400 vs A 272" counted log lines, not shots. Per-shot counts are in the row above.
- tsr1 booth friendly fire after `c3b08b5`: no character deaths from the booth shooters in `tok\tkB3`-`tkB10`. Closed.
- kos1 `truckguy`: reacts before the player arrives, as in the original (B -3.4 s, original -1.6 s). B plays
  `std_xstartled_n_a_a` where the original walks, then runs. Minor; left as is.
- kos1 `leftguy` (G2 `fly4_orig_kos1` vs `gaps_k2_om`): the original perceives the still player 142-155 units behind
  it or to its left, crouches and aims. B does not perceive the player until the player flies off in front of it,
  4.6 s after reaching waypoint 23. Then B startles, reloads (rule 5, probability 1/2) and fires 8.7 s after the
  arrival (original about 5.3 s). This is the same unknown perception rule as `wallguy`; the data is added to
  SR-MODEB-05. Open.
- Helicopter sparks (`4508a16`, `heli\h7T` vs `h6A_jpn3d`, frames `heli\cmp_s1.png`, `cmp_y299p6_2.png`): the same
  pistol fan gives 19 ricochet sounds on the helicopter (ent 210, channel 5) in A and none in B. B's bullets pass
  the landed helicopter and hit the ground behind it (stone at 632 -1253 96). A few of them hit at the player's own
  box face (y -1034). The spark code therefore never runs in B. Open: why the bullets do not hit the helicopter.
- Far tsr1 shotgun shooter (6 vs 11): still open. In A a third fire source at 933 -83 fires 0.9 s after the far
  shooter in every 4.2 s cycle, which B does not have. B's shooter 292 never plays `xcock`.

### 6.6 Shotgun pellets hitting bystanders (tsr1, 27 Oct 2026)

- Question: does B lack a "friendly in the line of fire" check, or aim at the wrong target? Neither:
  - the far tsr1 shotgunner (type 11) aims at the player in every shot, and its clear-shot check (SPEC-710, eye to
    target eye) is as specified; the character that was hit lay about 90 units off that line;
  - that character is not on the shooter's team, so the SPEC-782 teammate rule does not apply and full pellet damage
    is correct for a pellet that reaches it. The spec has no other in-the-way check for ordinary shots.
- Cause: the shot direction. `G3HumanBody::Fire` used only the per-type spread (+-0.2 per component for type 11) and
  lacked the SPEC-722 Firing rules "for types 7, 9, 11, 12, 16, 17, 18, 29 the vertical aim uses the target's real
  position; type 11 mixes 60 % of the current direction with 40 % towards the target" (already listed in G2.md).
  A hit shot left about 10 degrees off the aim point, and most of its pellets struck the bystander.
- Fix (`g3_human.cpp` `Fire`): after the spread, for those types the vertical slope of the direction is set from the
  target's origin (SPEC-AMEND-006 G2.11), and type 11 is then mixed 60/40 with the direction to the target origin.
  Skipped for blind fire and without a target. Diagnostic: with `g3_shotdebug 1` every pellet that hits a creature
  prints shooter, target, position and whether they are on the same team.
- Result, tsr1 (A = original module, B = openMERC):
  | | shotgunner shots | shots with gore on a character other than the player |
  |---|---|---|
  | A | 30 | 0 |
  | B before | 18 | 4 |
  | B after | 18 | at most 1 pellet on a non-teammate per run |
  - Deviation from the aim point, before: yaw up to 10.5 degrees, elevation -9.5..+5.7; after: yaw -6.8..+4.8,
    elevation within 1.2 degrees.
  - The remaining pellet hits fall on crouched teammates next to the player, which the teammate rule handles.
- kos1 check: shotgun shots A 10, B 8; assault-rifle shots A 74, B 76. Character gore away from the player: A none
  beyond the scripted deaths, B one teammate hit by a grunt standing in the line of fire (spec behaviour).

### 6.8 arm1 NPC pushing against a door that never opens (27 Oct 2026)

- Report (0.2.0, mode B): on arm1 an NPC keeps walking into a door and the door never opens.
- Which NPC and door:
  - Sam (`m_x_msam`, ai 48) at smart door *18 (ent 72, 227 228 44, areaportal `powers`);
  - also toiletguy (ai 124) at smart door *17 (ent 71).
- Root cause 1 (SPEC-710 C8 vs C9): the unstick code sets a door point (kind 3) at the owner's own position, but the
  pending check requires the point to be at least 50 units away. So the point could never run.
  - `CombatDecision::PendingExecutable` now accepts kind 3 when it is younger than 1 s.
  - It is used by ExecutePending, ActionDone and both plan paths.
- Root cause 2 (C9 probe height): the unstick probe is raised by the step height (24). With a tall box in an
  88-unit doorway, it started inside the lintel or hit the wall above the door.
  - Fix: retry at half the step height and then at the origin (the SPEC-740 rule 9 heights).
  - The retry accepts only a hit on a door-like entity.
- Player-use check: the player also cannot open *18 before Sam passes, in both A and B. That is map design:
  - func_wall `locked` (*5) has contents playerclip|detail|translucent, so it blocks only players;
  - use trigger *4 plays the `lockmes` talk sound;
  - Sam's pathtarget `lethimin` at sam4 turns `locked` off.
  - After Sam passes, the player opens door 72 in both A (31.99 s) and B (31.69 s), so the door entity is fine.
  - In 0.2.0, Sam never got through, so the lock stayed on for good.
- Evidence: door open sounds in seconds of client time, mode A vs mode B.
  - arm1:

    | Door | A | B |
    |---|---|---|
    | 71 | 5.89 | 5.09 |
    | 72 | 15.19 | 14.99 |
    | hwy 58 | 23.6 | 27.5 |
    | 86 | 39.1 | 42.4 |

    - Before the fix, B had no door events at all.
    - `move blocked` count, Sam: 276 before, 6 after. toiletguy: 375 before, 7 after.
  - arm2:

    | Door | A | B |
    |---|---|---|
    | 63 | 13.50 / 17.49 | 13.29 / 17.39 |
    | 50 | 21.99 | 25.48 |
    | 24 | 28.29 | 31.39 |

    - Before the fix, B had no door events and 467 `move blocked`.
    - The remaining 175 `move blocked` belong to smokeguy (ai 116, politician). They are identical before and after
      the fix, with no door nearby, and are unrelated (unverified in A, which has no trace).
  - sud1 (trigger *9 -> t474 next to smart door 159): the soldiers fight the player and no door opens in either A
    or B (parity, weak evidence).
- Still open (not changed): after Sam's door, B runs about 3.5-3.9 s behind A, because Sam plays std_epushbuttons
  twice at sam6 (waitaction list sf 2).
