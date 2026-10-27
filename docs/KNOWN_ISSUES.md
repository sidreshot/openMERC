# Known issues and unfinished work (release 0.1.1)

State on 2026-10-27 (`main`). Two columns matter: **mode A** = openMERC engine with the original game and player
modules, **mode B** = everything openMERC (`base\gamex86.dll` and `base\player.dll` from this project).
Package ids (I1b, G2 …) refer to `docs/IMPLEMENTATION.md` and the journals in `docs/impl/`.

## What works

- Mode A: all 32 single-player maps, save/load, level changes (`gamemap`), the intro and the menus, openMERC demos;
  multiplayer between openMERC builds (not with original 1.07f, see below); the GameSpy replacement (community
  masters, OpenSpy peerchat).
- Renderer: all 75 stock maps, Ghoul characters and view weapons, fog, terrain, detail textures, flowing
  surfaces, sky; measured 0–1/255 against the original renderer on the compared frames (I4a/I4b/I4c).
- Sound: the OpenAL module plays everything the original default sound module plays (I5s-2); EAX-style
  environments through EFX.
- Video settings page, desktop defaults, aspect-correct 2D with HUD scaling (V1).

## Unfinished work on the port (mode B)

| Area | Package | State |
|---|---|---|
| Client side of the player module (view-weapon sounds, muzzle effects) | I1d | **done in 0.1.1**: fire, reload, cock and dry-fire sounds of the own weapon match the original in both modes. Open: pistol recoil one frame late, the remote-server predictor (SPEC-602) is not hosted. |
| AI: perception, decisions, scripted sequences, idle behaviour | G2 | in progress. 0.1.1 fixes two "stands and does nothing" cases (crouch flicker without aiming, startle loop); modeb-gaps fixes `ai(x, 99)` never ending script control (kos2 boss and tsr1 token booth now fight; MODEB-GAPS 6.4) and the startle after a character has already fired. Open: a guard on kos1 (`wallguy`) does not perceive a still player ~100 units behind it (SR-MODEB-05); un-alerted characters notice a still player at angles where the original's do not; in the tsr1 booth one shooter kills its neighbour with a shotgun; two kos1 characters react 4-5 s late; a kos2 boss startles 1 s after its script before firing. |
| Roster, helicopters, vehicles | G4 | in progress (with G2); the trn1 ending and the ger4 boss precache are open. |
| Game core: world, spawning, physics, player/client, items, projectiles | G18 | in progress; single-player level change and savegames work; benches/seat usage, remote cameras and several requests from G2/G3/G7 are open. |
| Props and server effects | G7 | in progress. |
| Damage, gore, dismemberment | G3 | code complete for its specs; waits on G18 player poses and the G2 save hook; animal (cow) idle choice differs. |
| Game modes, Ghoul glue | G9 | code complete; waits on five amendment items. |
| Script VM, cinematics, script host, savegames | G6a/G6b/G10 | done for openMERC's module; cross-module level saves are refused by design; worldspawn script runner after a load and a statics leak across map restarts are open. |
| Navigation and bots | G5 | done; single-player navigation and team objectives untested in game. |
| Player movement amendment (SPEC-314), tsr1 fDot, trn1 camera | I1b-2 | in progress. |
| Linux build | P1 | mode B builds on Linux x86_64 and i386 on branch `linux-build` (`docs/BUILD-linux.md` there); unit tests pass; dedicated server and client sessions with the stock paks run on both (cinematics, trn1, a deathmatch spawn with walking and firing; Xvfb + llvmpipe, wave-writer sound). Not merged into `main`. Not verified: real display/sound device, full campaign with saves, network play between machines; OMsnd focus mute is always off on Linux. 64-bit Windows, macOS and ARM: not started. Mode A stays 32-bit Windows (the original modules are 32-bit DLLs). |
| Graphics upgrades | GFX-modern | plan and off-by-default cvars on branch `gfx-modern` (bloom on emissive surfaces, volumetric fog, per-pixel region fog, cascaded soft shadows, per-pixel dlights, soft particles, overbright). No shaders yet. The same branch sets graphics defaults to maximum (`f870b5e`: MSAA 8x, anisotropy 16, trilinear, far clip); not in `main`. Effect settings are to be added to the video page. |
| Menu redesign | MENU2 | `main` has the interim left-anchored widescreen layout (`dc4eccf`). The first from-scratch redesign (`b7dc0a8`) cut pages off and was reverted (kept as branch `backup/menu-modern-b7dc0a8-broken`). Branch `menu-scratch` has the new wide layout (see the menu-scratch items below and docs/impl/MENU2.md); not merged until the owner's live test. |
| Android | M9 | deferred until the desktop code is complete. |

## Known bugs and gaps (both modes unless stated)

- **Old savegames (mode B)**: savegames written by OpenMerc builds before the legal-game merge (2026-10-26) do not
  restore human animations correctly: they refer to animation records by position, and the animation table is now
  keyed and sorted by sequence name (`docs/impl/LEGAL-game.md`). Start a new game or load a newer save.
- **No 1.07f compatibility**: openMERC no longer talks to original 1.07f servers or clients or plays 1.07f demos
  (the fixed network tables were removed for the legal cleanup); a compatibility mode will not be built.
- **Parental locks**: the original keeps its lock words in the registry; openMERC reads them but never writes.
  Without a valid store the locks now start off (intentional, `docs/DEVIATIONS.md`); the lock page changes
  apply to the running program only.
- **Language**: the stock pages keep the language of your installation; openMERC's own texts exist in English and
  Russian (`om_language auto` detects Russian installs); other languages show English for the new rows.
- **Fonts at non-integer scale**: menus, HUD and console use a whole factor by default (`ui_scale 0`: 2 at 1080p,
  3 at 1440p); a fractional `ui_scale` makes the nearest-filtered SoF fonts slightly uneven.
- **Video page**: resolution is a fixed list (plus "Desktop"); modes outside the list are reachable by console
  (`r_mode -1`, `r_customwidth`, `r_customheight`).
- **Renderer**: not implemented — A3D geometry export, `gl_drawflat`, `gl_drawmode`, the lightmap grid of
  `gl_showtris 3/4`, two `r_speeds` counters; `iraq` water looked different in one comparison (I4c).
- **Client effects** (I5): the editor text pages are not drawn; the shooter's own weapon effects depend on I1d.
- **Menus** (I6m): two chat-page display items; the credits roll timing was not compared.
- **Menus** (MENU2): tool tips of the tab pictures do not appear (own wording is looked up, no tip is drawn);
  page text beside a dialog box stays faintly visible through the darkening; the character page reports
  "Ghoul model not found" in both modes; a save made from the console (not from the menu) has a black
  thumbnail, because the picture is taken when the menus open in game.
- **Menus** (menu-scratch widescreen layout):
  - The EFFECTS block of the video page stays hidden until a renderer that registers its cvars (`r_om_*`, gfx-modern)
    is running (checked with the gfx-modern DLLs: the rows show).
  - Wide layout (`menu_layout 1`), seen on the 2026-10-27 screenshots: the stock pages keep their own small fonts
    (stock medium/title at 2x), so pages with little content (new game, credits, lock, statistics) sit small in a
    large panel; the video page colour block spreads its sliders, colour bar and buttons across the width; a
    scrolling ticker is cut at the footer buttons while it scrolls; the join list header "Pure" is clipped to its
    narrow column; at 2560x1440 the main menu canvas fills the full height (no top/bottom margin).
  - Screens with stock full-screen art keep the stock layout without the side panel: the end statistics are black
    (stock `menu_nostats` backdrop), and the end and mission-failed art is pillarboxed at 4:3.
  - Stock data, unchanged: the Russian credits keep an untranslated "ART" header, and `m_start.rmf` reports one list
    count mismatch.
  - With a window size outside the resolution list (`r_mode -1`, e.g. 1440x1080), the resolution row reads
    "Desktop".
  - Below 2x (1600x900, 1366x768) the menus use a fractional factor, so the fonts are linear-filtered.
- **View weapon**: SMG colour (I1c-9), view-weapon prediction, x64 sound (I1c open items).
- **Sound**: `s_nosound` is ignored by the original default module in hidden test runs (test harness only).
- **Multiplayer**: fixed 2026-10-27: a single-player map started with `deathmatch 1` (tsr1, jpn3) used to stop with
  "*Index: sound overflow", then "Couldn't find deathmatch spawn point" (jpn3 also "String package not registered for
  ID 7100"), with the retail game in mode A and the open game in mode B alike. The sound table now has 511 slots, the
  map's single-player starts serve as deathmatch spots when it has none, and a missing level package gives empty text
  in deathmatch (`docs/DEVIATIONS.md`). Checked in both modes: tsr1 and jpn3 load, walk and fire. Not checked: bots
  on those maps (no deathmatch waypoints), and the `CFSX` save chunk (no single-player map uses more than 355 sounds).
- **Sound (AUD)**: stutter / cut-off phrases reported in mode A not reproduced at 300/165/60 fps on trn1, tsr1, kos1,
  nyc1, nor on a real output device under CPU load and slow frames (no underrun, no dialogue cuts;
  `docs/impl/SND2.md`). Fixed: music ran dry during game hitches over 1 s (the queue now holds 2 s). If it happens
  again, `s_om_debug 8` logs every cut with its reason.
- **Multiplayer**: the query responder has no rate limit yet (planned, `docs/DEVIATIONS.md`).
- **Console**: always available since 2026-10-27 (`console` defaults to 1, no `+set console 1`); the `` ` ``/`~`
  key opens and closes it in the game, over the menus and at the start-up menu (checked live in both modes,
  hidden-desktop key press and screenshots). Closing it with the key used to leave it open until Escape (fixed).
  `toggleconsole` from an exec'd script during a level load is still unreliable (the key works).
- **Death and end screens** at widescreen were not captured by the automated runs; check them by hand.
- **Hidden-desktop test harness** (`tests/`, `*/test/`): Windows PowerShell 5.1 only, hard-coded paths of the
  development machine; it is development tooling, not part of the release.

- **Floating after a few minutes (mode A, reported 2026-10-26)**: after roughly two minutes of play the
  player sometimes seems to float above the floor and steering becomes sluggish. **Probably fixed on branch
  `q2-pmove`** (the floor hover, `sofpm_test` H1-H5); not confirmed by a long play session. Scripted runs of more
  than two minutes did not reproduce it. The many jump sounds heard at the same time were `jump.wav` repeating
  while sliding up stairs with jump held (~30-88 sounds); fixed in `7508977`: one sound per press, in mode B by the
  game module and in mode A by the server filter `sv_om_jumpgate 1` (default).
- **Sliding "on ice" after many sharp direction changes (both modes, reported 2026-10-27)**: `dc4eccf` added
  lateral friction on sharp turns (`docs/DEVIATIONS.md`), which fixed single 90-degree turns, but after frequent
  rapid strafing and direction changes the player still slides; it stops after standing still. **Fixed on branch
  `q2-pmove` (not merged)**: the cause was the floor hover (airborne just above the floor, so no friction), not the
  turn handling; movement is now the Quake II pmove with SoF's deltas (`docs/DEVIATIONS.md` Q2PM) and the
  lateral-friction rule is removed. X1 `ice_stop` (nycdm4, 6 s rapid strafing and turning, then release), mode B:
  before, 112-176 units of glide over 1.4 s at 165/300 fps; after, at most 30 units, stopped within 0.3 s at
  60/165/300 fps. One run at 60 fps glided 109 units while dropping off a ledge (airborne, as in Quake II).
- **tsr1, TV-booth glass (mode A, reported 2026-10-27)**: fixed in `0a17ab2`, rule tightened in the fixes-1027
  merge: the booth glass breaks in both modes and the player can climb in through the broken panes (scripted walk,
  `openmerc-data\glassfix\runs2\tsr1_*`). The blockers were non-solid clip brushes inside the panes; only brushes
  of that kind are ignored now (all 32 SP maps: tsr1 only, `docs/DEVIATIONS.md` "Glass seal clips").
- **kos2, sewer pipe drift (both modes)**: standing still pressed into the round sewer pipe (eye height about -528,
  against x = 516 or y = -3604) the player glides at 20-65 units/s. The map has no current or water brushes and
  no push trigger there, so this is not a water current but the player box wedged against the pipe's curved
  brushes. **Fixed on branch `q2-pmove` (not merged)**: X1 `kos2_pipe` (5 spots x 4 moves, then 3 s standing),
  mode B: before, drift of 187 units (165 fps) and 21 units (300 fps); after, 0 at 60/165/300 fps. A Source-style
  four-quarter ground probe was tried and dropped: no measured case needed it. `sofpm_test` H6-H8 pin the crease.
- **Typing Cyrillic in the console** (fixed on branch `cyrillic-input`, 2026-10-27): the console and the chat line
  take the Windows keyboard layout's characters (Russian, and Latin-1 letters), see `docs/DEVIATIONS.md` "Keyboard
  text". Checked with `tests/X1/xtype.ps1` (posted key messages, captures). Not yet checked by hand on a physical
  keyboard; menu text fields (player name, server address) still take only the SoF layout table's characters.
- **Gore chunks** (fixed in `7508977`, confirmed by the owner 2026-10-27): flesh debris was drawn with the large rock
  meshes ("half a person"); see `docs/DEVIATIONS.md` "Gore chunk size".

## Mode-B specific (own game and player modules)

- Pistol recoil starts one frame later than the original (I1d). The sniper zoom sound now plays the same in both
  modes (kos1, 3 s of zoom: 7 plays of `weapons/sniper/zoom.wav` in each mode, `s_om_debug 4`).
- Dormant characters and script starts: a few characters stay idle; crouched aiming asks for a pose the body
  code does not have yet (G2 ↔ G3).
- Severed limbs are random (engine RNG) and differ run to run; the killing-blow cloud and gib rules follow
  SPEC-AMEND-006 but were compared only in deathmatch.
- Helicopter ricochet sparks now follow SPEC-AMEND-008 G4 (entity origin, types 0/10/14, as the original); not
  yet re-measured against the original on `crew_*` hits.
- Fixed on branch `modeb-gaps` (2026-10-27, `docs/impl/MODEB-GAPS.md` 6): string 060e ("Check your fire!"),
  the mid-game and end-of-mission statistics pages (filled, same values as mode A on kos1), chat lines that
  showed the sender name twice, "Can't find material:gun" on the automatic guns (ger2, ger3, jpn2, nyc1), the
  `gimme` "Spawning" line, and the character muzzle effect, which now sits on the weapon's `flash` bolt.
- `environ_soundgen` without a sound name (sib2 `sib2_catsound`) prints "Soundgen with invalid name!" in mode B
  only; the spec says it prints, mode A shows nothing (spec request SR-MODEB-01).
- Cinematic sounds on `ger4` and `nyc3` are routed differently (G6b/G18).
- Savegames made in mode B cannot be loaded in mode A and vice versa (by design: different module layouts).
- **Save-load and level-change crashes (mode B)**: fixed in `ab3c804` (branch `modeb-crashes`, SPEC-AMEND-012/013):
  `helperlink` is now saved and cleared before the helper relink, and the player module's pointers are reset
  across a level change. kos1/ger1/ger3/sib1 save-load and tsr1->tsr2, arm2->nyc1, sud3->arm3 pass
  (`docs/impl/MODEB-GAPS.md` 5).

## Reporting

Please include: mode (A or B), map, `user\sof.log`, and the steps. Compare with the original game on the same
map where you can. The project's goal is single-player gameplay at least as good as the original 1.07f on every
map (network compatibility with 1.07f is not a goal), so any gameplay difference that is not listed in
`docs/DEVIATIONS.md` is a bug.
