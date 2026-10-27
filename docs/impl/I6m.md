# I6m — Menu runtime (RMF) (work log)

Package: SPEC-130 (.rmf files), SPEC-800 (runtime core), SPEC-801 (stack, input), SPEC-802 (widgets),
SPEC-803 (engine-data widgets); fonts SPEC-132, strings SPEC-131/313, 2D through SPEC-002/412.
Directory: `engine/src/client/menu/` (the Yamagi Quake II menu — menu.c, qmenu.c, videomenu.c, qmenu.h — is
replaced and deleted).

## State (2026-10-22 10:0x)
- Runtime complete except *Remaining*. The shared tree builds (build-I6m-x86, no warnings in the menu files).
- Parse test: every effective stock `.rmf` parses (453 files = 343 documents + 110 fragments; 0 documents fail).
- Spec test vectors: 34/34 pass with the shared-tree build (`test/vectors.py`).
- Startup flow works: `disclaimer` → 3 s timeout → `intro` (Ghoul animation, EOS after 20.5 s) → `amain`
  (waitanim until `b_web_home` EOS after 4.5 s) → ready.
- Black-box pairs original | OpenMerc for 28 top-level pages at 640×480 in `openmerc-data/shots/I6m/pair_*.png`.
- Server browser: `join` lists the community-master servers (18 listed, 8 locked by the violence filter in one
  run), selecting one fills host name, address, rules (`serverdetail`) and players.
- Ghoul models load through the engine's Ghoul system (I3) and are posed/animated every frame, so bolted items
  (tabs, titles, hot spots, tickers, bolted inputs) sit where the original has them; the models themselves are not
  drawn (I4b). `gpm` builds the player-model catalogue and drives skin/team cvars and the team icon.

## Layout
| File | Content |
|---|---|
| `header/menu.h` | internal structures: widget/page/frame/screen, parse context, menu state |
| `header/m_draw.h` | 2D calls = SPEC-002 slots (0x24, 0x44–0x54, 0x74–0x7c, 0x8c–0x9c, 0xa0, 0xc0, 0xc8–0xd4) + test capture |
| `header/m_ghoul.h`, `m_ghoul.cpp` | C interface to the Ghoul library (menu system object): load, parts, bolt-ons, playback, transform, bounds, bolt matrices, EOS/sound notes, player-model descriptions |
| `menu.c` | SPEC-801: system open/close (pause, hidden-cvar lock, savegame thumbnail), stack, `menu`/`intermission`/`popmenu`/`killmenu`/`menuoff`/`return`/`refresh`/`reloadall`/`select`/`checkpass`/`changepass`/`freshgame`/`hackteam`, command queue, frame loop, keys, mouse, wheel, TAB, tooltips, pointer; client entry points `M_*` (+ Yamagi stubs `VID_MenuInit`, `CalibrationFinishedCallback`, `CleanCachedMapsList`) |
| `m_parse.c` | SPEC-800 §1–5, 8: STM window, tokeniser (quotes, `<`/`>`, `^REF^` insertion), errors, includes, document tags, `<stm>`, frames (cutting, page history) |
| `m_layout.c` | SPEC-800 §6, 7, 9, 10: widget creation, common options, conditions, key lists, flow layout, page/frame/border drawing |
| `m_widgets.c` | SPEC-802: text, ctext, list, slider, input, setkey, image, hbar, vbar, ticker, hr, blank, selection, popup |
| `m_data.c` | SPEC-803 §3–6: chat/users/rooms, serverbox/serverdetail/players/translate + server list + `serverstatus`, filebox/filereq, loadbox (via `SG_ReadInfo`), listfile/botlist/bot |
| `m_model.c` | SPEC-803 §1–2: ghoul/bghoul/gpm, bolts, per-frame model update |
| `m_draw.c` | 2D: software implementation of the slots (SPEC-412 rules) presented with `Draw_StretchRaw` (default, `om_menusoft 1`), or the renderer's SPEC-002 table through `OM_GetSoFRefExports()` (`om_menusoft 0`, see I4a below) |
| `m_strings.c` | `<strings>` / `^REF^` / key names through I1a's string packages (`SP_*`, owner `SP_OWNER_MENUS`) |
| `m_test.c` | OpenMerc test commands: `om_rmftest [v]`, `om_menudump`, `om_menushot <name>`, `om_menutour <frames> quit|stay <menus…>`, `om_after <frames> <text>` |
| `test/` | `run_oracle.ps1` (original in `D:\Games\SoF-test-I6m*`, I4a's capture shim), `run_om.ps1` (OpenMerc, hidden desktop, always `-portable` + `-user` under openmerc-data), `gen_scripts.py`, `pairs.py`, `vectors.py` |

`menu.c` also has `om_menukey [+shift] [+alt] [+ctrl] <key> [x y]` (deliver a key to the menus, pointer optional).

Edits outside the package (minimal hooks): `engine/CMakeLists.txt` (menu source list), `client/cl_network.c`
(`pingservers`: busy check, `M_PingServers()`, then `CL_ServerList_Refresh`; `print` → `MS_ServerPrint`),
`client/cl_sof.c` (`ack` → `MS_ServerAck`), `client/header/client.h` (`M_MouseMove` prototype),
`client/input/sdl3.c` (menu pointer: `M_MouseMove(xrel, yrel)` while `key_dest == key_menu`; I6h please keep).

## Build
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I6m-x86&& tools\build\build-msvc.cmd x86 Release"
```
When other packages' half-finished files break the shared build, `openmerc-data/I6m/build_overlay.py` builds a
private copy (server files from git HEAD, do-nothing stubs for symbols referenced before they exist) into
`openmerc-data/I6m/build-ov/`.

## Test
- Parse test (client console): `om_rmftest` → `453 files, 343 documents, 110 fragments, 0 documents not created,
  18721 widgets`; messages: 34 × `demote` Unknown keyword (a_chathelp; SPEC-803: no handler), 65 × Could not find
  token (the 6 never-defined refs of SPEC-131 + Gold/Platinum entries hidden by zmain's 1.0 packages under the
  SPEC-300 pak order), 5 × match/label count mismatch (stock m_start, m_video), 1 × Unrecognised text keyword
  (zmain text with inner quotes).
- Test vectors: `python engine\src\client\menu\test\vectors.py --exe build-I6m-x86\release\openMerc.exe` (English
  data copy `D:\Games\SoF-test-I6m-en`, test pages written to `openmerc-data/I6m/home-vec/user/menus/`):
  SPEC-800 1–8, 10–13, 15; SPEC-801 1, 3, 4, 5, 8; SPEC-802 1–5, 7, 10 → 34/34.
- Black-box pairs: `python test\gen_scripts.py <dir> 10`; `run_oracle.ps1 -Script <dir>\menus_oracle.cfg`;
  `run_om.ps1 -Script <dir>\menus_om_tour.cfg -Tag omsw -Extra "+set snd_dll OMsnd.dll"`;
  `python test\pairs.py <shots> oracle omsw pair`.
- Interactive checks (captures `inter_*.png`): tooltip on the name field, wheel scrolling of `video`, `setkey`
  waiting + binding (`bind j` → `"j" = "+lookdown"`), server selection on `join` (`omjoin_join_sel.png`).
- Performance (software 2D): 640×480 and 1600×1200 both ≈165 frames/s (frame cap) after the 1:1 picture fast path.
- Notes: the console `wait` of the OpenMerc engine is not one drawn frame (≈10 000 waits/s), so OpenMerc scripts use
  the frame-driven `om_menutour` / `om_after`. The engine accepts only ~40 command-line words: `run_om.ps1` puts
  extra settings in front of the script (`-Pre`). All runs use `-portable`, `-basedir D:\Games\SoF-test-I6m*` and
  `-user` under `openmerc-data` (the 05:13 write into `Documents\YamagiQ2` was an x64 Yamagi build, not these
  scripts; checked its stdout.txt).

## Findings (details in OPEN_QUESTIONS.md, section I6m)
- Layout, frames, borders, fonts, flow: popups and the disclaimer page match the original pixel-exactly; content
  pages (video, audio, keys, misc, options, game, start, join, language, lock, load) match row by row; with bolts,
  tab icons, titles and bolted texts match positions.
- Remaining differences: Ghoul geometry (not drawn), cvar values owned by other packages (`snd_dll`,
  `ghl_light_method`, `win_broken` = 1 in OpenMerc hides two `misc` rows the original shows), the pointer (drawn
  by OpenMerc, absent in the original's unfocused captures).

## Deliberate interim behaviour (only while a model cannot load; not reached with the stock data now)
- a widget whose `bolt`/`bbolt` cannot be resolved stays in the flow layout;
- a `waitanim` screen without a loaded `control` model is ready at once; the `end` command of a model that
  failed to load is queued at once.

## Needs from other packages
- I4a: through `OM_GetSoFRefExports()` pictures come out as flat vertex colour (the tiled `backdrop/web_bg` white,
  the scroll bar flat teal; text, fills and lines are right; reading back with slot 0xd4 is right). Hence the
  software default (`om_menusoft 1`); switch the default in `m_draw.c` once fixed. Also SPEC-802 defines slot 0x54
  rotations 4–7 as flips (5 left–right, 6 top–bottom, 7 both), not `rotation & 3` (I4a OPEN_QUESTIONS 3): the
  slider's right cap and the scroll bar's bottom cap must be mirrored.
- I4b: menu model drawing (slot 0xb8/0xb4 with the menu Ghoul system's GL table). Models are already posed every
  frame in `m_model.c` (`MM_DrawBackground` for background/bolted models, the ghoul widget update for placed
  ones): draw `mgmodel_t` instances there; then the software 2D layer must be split around the models (or the
  renderer table used).
- I1b: savegame thumbnail — set `sg_host_t.Screenshot = M_SaveThumbnail` (`qboolean M_SaveThumbnail(byte *)`,
  menu.c: the 128×128 picture taken when the menus open in single player, SPEC-801 §1).
- I1a: `win_broken` (the original behaves as 0 on this PC: `m_misc` shows the task-switch rows).
- GameSpy lobby chat (`gsc_*`, SPEC-204 §5) is not implemented anywhere; `m_data.c` offers `MC_PublicLine`,
  `MC_PrivateLine`, `MC_SetRooms`, `MC_SetUsers` for it.

## Remaining
- In-game use (ESC in a running single-player game: pause, thumbnail, pop rule for the dead player) is untested —
  no complete game module run here yet.
- `filereq` has no directory navigation (no stock menu uses it); joystick/TAB navigation not exercised.
- Ghoul `animate`/`mousex`/`mousey` (no stock use) are parsed but have no effect; `rotate` composition order and
  the bounds turn for spinning axes (π/4) are not applied (only `gpm` rotates, and it is not drawn).

## SPEC-AMEND-005 (2026-10-25)
Applied by package F5; item-by-item results, code changes and tests in `docs/impl/F5.md` (section I6m).
