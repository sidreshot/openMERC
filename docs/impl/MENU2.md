# MENU2 - modern menus and settings pages (work log)

Clean-team package (C). Branch `menu-modern`, worktree `om-wt-menu`, build tree `build-MENU2` (x86 Release),
test copy `D:\Games\OpenMerc-menu-test`, ports 29630-29690, captures in `D:\Games\OpenMerc-menu-test\shots`.
Behaviour changes are rows marked "(MENU2)" in `docs/DEVIATIONS.md`; new menu files are OpenMerc's own text
(sources `data/base/`, packed into `base\zz_openmerc.pak`), stock pages are only referenced by string id.

## Done
- UI scale: one whole 2D factor (`ui_scale`, auto 2 at 1080p, 3 at 1440p), left-anchored 4:3 canvas (vertically centred),
  crisp fonts; page clipping keeps panels and models inside the canvas.
- Tab folders: static Ghoul poses no longer start events (fixed "button tops not drawn").
- Pages: Video (rewritten), Audio, Controls/key bindings (sections Movement, Combat, Weapon slots, Items,
  Interface, Look, presets from `configs/*.cfg`, Save / Defaults / Unbind all), Gameplay, Multiplayer.
- Video page: resolution list from the display's SDL modes (`vidmodes`, `om_vidstage`, `om_vidapply`), display
  mode, vsync, frame limit; menu/text size, HUD size (to 4x), widescreen 2D; FOV and Hor+; AF, MSAA, texture
  filter, detail textures, Ghoul lighting, specular; gamma/contrast/brightness/light intensity; Apply/Defaults.
  Rows without an effect in the GL3 renderer are gone (S3TC fragment replaced by an empty one).
- RU/EN: `tools/menu2/gen_strings.py` writes `strip/menu_omvideo.sp` (199), `menu_omset.sp` (197),
  `menu_omfill.sp` (198); Russian detected automatically; no Yo letter (missing in the menu fonts).
- Default controls `configs/DEFAULT_KEYS.cfg` and the `weaplast` client command.
- Tests (`tests/MENU2`): `run_menu2.ps1` (hidden desktop, `-portable`, own user dir and ports), `pages.cfg`
  (13 pages), `hover*.cfg`, `rmftest.cfg`, `vidapply.cfg` (Apply and persistence of the video page).

- Audio page: master (`s_om_gain`), effects (`s_volume`), speech (`s_om_voicevolume`, new in OMsnd) and music
  volume sliders; Music on/off on its own line; Output section with the device list (`snddevices`,
  `s_om_device`, new in OMsnd), HRTF, reverb, limiter, mute in background and Sound restart. OMsnd applies
  output rows live (`Vo_ApplySettings`). Bordered menu texts are centred in their border (`Text_Draw`).
- Own wording for stock references (`OMOVR_`, `strip/menu_omovr.sp`): tab tips, page titles "РР“Р РћР’РћР™ РџР РћР¦Р•РЎРЎ"
  (scale page) and "РњР«РЁР¬ Р РџР•Р Р•Р”Р’РР–Р•РќРР•" (misc page), tickers of the audio/video/scale/misc pages.
  `MSP_Find` copies the reference first (callers pass `va()` buffers).
- Own `default.cfg` runs DEFAULT_KEYS.cfg last: always run and free look on a fresh profile (cfeac7f).

## Verified (2026-10-26, rebased on main e469c85)
- `om_rmftest`: 453 files, 0 documents not created, 1 message (stock `m_start.rmf` list mismatch).
- Video page: stage `1920x1080` / `2560x1440`, Apply 1600x900 -> `r_mode -1`, 1600x900, HUD 4x, horplus 0,
  all written to `config.cfg`; `desktop` -> `r_mode -2`. Captures `shots\s1080`, `shots\s1440`, `shots\vid`.
- User `config.cfg` > 8 KB loads (command buffer 32 KB from main).

- Audio (wave-file captures of 15 s of tsr1, `tests/MENU2/snd_*.cfg`, analysis in openmerc-data): `s_om_hrtf 1`
  changes the output (L/R correlation 0.35 -> 0.30, energy above 6 kHz 0.0055 -> 0.0022, readback "HRTF on");
  `s_om_gain 4` with the limiter: 256 full-scale samples, without it 119970 (readback "limiter off");
  `s_om_reverb` is passed to the voices (`EFX_Slot`), but with EAX environment 13 forced the decay after
  gunfire differs only by about 1.5 dB - not conclusively measured. Live switching of HRTF/limiter without
  `snd_restart` in `audio.cfg`; device switching by clicking the row on the real output (Audigy speakers ->
  SPDIF -> HDMI monitor, "OMsnd: output now ..."), the saved device opens on the next start.
- New Game freeze: not reproduced (main menu, in-game, custom, Yes dialog, 4 min runs). The tsr1 intro is
  about 90 s with input frozen; ESC skips it. Most likely what was seen; no code fix.
- Captures: `shots\au1080`, `shots\au1440` (audio top/bottom), `shots\pg1080`, `shots\pg1440` (all pages),
  `shots\dl1080`, `shots\dl1440` (stock dialogs, Gameplay bottom), `shots\ad1080` (device row).

## Done/verified 2026-10-26 (rebased on main bb318d4, mode B = openMERC game and player modules)
- `tests/MENU2/deploy.ps1 -Mode A|B` installs the original or the openMERC modules into the test copy.
- Mode B: New Game from the menu, from in game, the "Yes" dialog, custom difficulty, tutorial, intro skip with
  ESC (`shots\Bng*`); all pages, audio page and dialogs at 1080p and 1440p (`shots\Bpg*`, `Bau*`, `Bdl*`);
  always run and free look on a fresh profile (`shots\Bdef`). New Game freeze still not reproduced in mode B.
- Dialog backdrop `menu_dialogdim` (`shots\Cdl1080`, `Cdl1440`, `Cngi*`).
- Gamepad navigation `menu_gamepad` (`tests/MENU2/nav.cfg`, `shots\nav1080`, `nav1440`): d-pad down walks the
  audio page content and scrolls it, pad-left lowers `s_volume` 0.7 -> 0.6, shoulders reach the tabs, B returns.
- HUD at 1080p/1440p in game is proportional in both modes (`shots\hsA*`, `hsB*`); tab/model animation frames
  at 1440p stay inside the canvas (`shots\an1440`).
- Load/save page: date labels shortened so they stay in their cell; saves made through the in-game menu have
  thumbnails (`tests/MENU2/savemenu*.cfg`, `shots\sm1080`, `sm1440`).
- `run_menu2.ps1 -Debugger` runs the game under cdb and writes the stack of an access violation (`cdb.log`).

## Open
- Mode B: loading a save in the same session crashes in the game module (`helperlink` not restored, see
  `docs/KNOWN_ISSUES.md`); outside this package.
- A save from the console has a black thumbnail (the picture is taken when the menus open, SPEC-801 1).
- Device names: '-' is drawn with a '>'-like glyph of the title font; OpenAL reports the resampler as
  "default" (BSinc24 not found) - pre-existing OMsnd behaviour.
- While `s_om_wavefile` captures, the device list shows only the wave writer.
- `s_om_voicevolume` covers ADPCM speech only; WAV voice lines (if any) follow `s_volume`.
- Tab tips: own wording is looked up (developer 2 trace of `MSP_Find`), but no tool tip appears in captures when the
  pointer rests on a tab (the tab highlights; 400 frames tried) - tool tips over bolted tab pictures not checked further.
- Stock `m_start.rmf` reports one list mismatch (stock data).

## 2026-10-26 ice walk + left menu
- Canvas left-anchored (was centred): owner said the centred 640 panel felt like 640x480 and too large on widescreen.
  Superseded by the menu-scratch composition below (canvas plus side panel, centred).
- LateralFriction in `sof_pmove.c`: extra ground friction on the wish-orthogonal component so sharp turns with always-run do not skate like ice. Evidence: `openmerc-data/menu/icehang/`.

## 2026-10-27 ICE2: stronger lateral friction

- Owner: ice still there with frequent sharp direction changes and strafing; standing still clears it.
- `sofpm_ice` (new, `engine/src/common/test_sof_ice.c`): always-run, flat floor. W held then D only, msec 8: old-axis speed 25/50/100 ms after the snap 202/150/82 -> 92/0/0, |old| < 30 after 160 -> 48 ms, skid 20.0 -> 5.7 u (Spec without LateralFriction: 246/212/158, 376 ms, 46.5 u). 120 s rapid WASD (90 degree changes every 120 ms): max speed 314 -> 300 (Spec 330), mean orthogonal speed 102 -> 29. Same picture at msec 1/16/33. The new axis still reaches 270 u/s after ~130 ms (Spec Accelerate, unchanged).
- `LateralFriction`: orthogonal component removed at 20/s with control floor 250 (x stagger k); above 420 u/s orthogonal (outside impulse) the old one-factor bleed. sofpm_test 324/0. Evidence `openmerc-data/ice2/`.

## 2026-10-27 menu-scratch: widescreen composition (replaces the left-anchored canvas)

Owner brief: a widescreen menu in the original style and art, not a stretched or left-anchored 640x480.

- **Scale.** Menus scale by height on a virtual window: `MR_MenuScale` = the largest whole factor that fits the
  composition (640 canvas plus the side panel block of 200 and an 8-unit edge margin when the panel is wanted) into the
  window, at least 2. `ui_scale` > 0 forces a factor (clamped to the fit). The scale is menu-only. The HUD keeps
  `scr_hudscale`. Factors: 1920x1080 2, 2560x1440 3, 2560x1080 2, 1920x1200 2, 1440x1080 2.
  When the rail would force a fractional factor and the canvas alone gets a whole one, the whole factor wins and the
  rail is left out (1680x1050: 2 without the rail instead of 1.91 with it). Sizes below 2x stay fractional either way
  (1600x900 1.67, 1366x768 1.42, both with the rail).
- **Composition.** The 640x480 canvas is centred in an 840-wide block. The side panel ("rail", 190 wide with 7-unit
  padding, 440 tall, 20 below the canvas top, 10 gap to the canvas) sits on its left. Without the rail the canvas is centred in the window. `SCR_CanvasX` uses the
  same rule, so the HUD-side 2D and the menus agree. Resize dialogs (`player2`, quit, ...) centre on the canvas
  (`M_DialogCentreX`).
- **Rail** (`menus/om_rail.rmf`, `M_AddRail`, `M_ScreenWantsRail`).
  - Shown when `menu_rail 1` (archived, video page row "Menu Side Panel"), the window is at least 3:2 and the
    composition plus the margin fits. An earlier 852-wide block with an 8-unit margin needed a virtual width of 860,
    so at 2560x1440 (853 at factor 3) the panel was not shown. At 4:3 and 5:4 it is never shown, and the page is the stock centred canvas.
  - Screens with a topic frame (the stock tab pages) get the rail, as do `main` and `player`.
    `outfit`, `mail`, `testmotd` and `objectives` never get it.
  - Sections: CAMPAIGN / MULTIPLAYER / SETTINGS / OTHER (string IDs `RAIL_*` in `strip/menu_omset.sp`, from
    `tools/menu2/gen_strings.py`). The rows use the stock medium font and stock icons. The row of the open page is
    highlighted (`currentif`).
  - Mouse click, TAB and the gamepad shoulders reach the rows.
- **Option tables.** `<omtable "menus/om_video.tbl">` builds rows from a UTF-8 table (kinds list, slider, section,
  note, rmf; flags `stage`, `requires=<cvar>`, `tip=EN/RU`).
  - A row whose cvar the running renderer does not register stays hidden, so the gfx-modern effect rows (EFFECTS
    block) appear only with that renderer.
  - `stage` rows go through `menus/video.cfg`/`applyvid.cfg` (Apply).
- **Backdrops.** A full-screen stretched backdrop (`missionfailed`, `endgame`) is drawn into its page's 640x480 rect
  and pillarboxed black instead of being stretched over the widened rect.
- **Fixes found on the way.**
  - `om_rail.rmf` needs `<stm>`. Without it, `MP_LoadPage` drops the page silently.
  - Lists step with the arrow keys and the d-pad left/right. Before, they reacted to mouse buttons only.
  - Rebuilding the screens after a layout change (UI scale, rail, aspect, mode) no longer re-runs the page config.
    Before, toggling the side panel re-read `video.cfg` and silently dropped every unapplied row above it
    (fullscreen, vsync, frame limit, UI/HUD size, aspect). Scroll position, keyboard/gamepad position and the
    pointer's place on the canvas are kept (`M_KeepScreenState`). Before, the gamepad walk jumped back to the top
    row.
  - ENTER / gamepad A on a key-binding row starts waiting for a key. Before, the same ENTER was taken as the new key
    and bound to the action.
- **Test tools.**
  - `om_menuprobe <tag>` prints the hovered widget, its cvar and its value.
  - `tests/MENU2/scratch_pages.cfg` covers every page, `scratch_ingame.cfg` the Esc menu, save, death and the end
    screens, and `scratch_rail.cfg` (+ `_click`) the mouse and TAB on the rail.
  - `scratch_controls.cfg` plus its `scratch_controls_<page>.cfg` parts walks every row with the d-pad, steps it
    with right-arrow and probes before and after.
  - `run_menu2.ps1` copies the `<script>_*.cfg` parts into the user dir, so a long script can be split to stay
    under the 128 pending `om_after` entries and the command buffer size.
- **Verified** (1920x1080 mode A unless stated; evidence `/workspace/menu_check/scratch/` on the review box).
  - 89 controls with their own cvar probed (62 lists, 21 sliders, 4 inputs, map list, player list), plus the key
    rows. 62 cvars are archived directly. The staged video rows (`menu_mode`, `menu_vsync`, ...) are checked through
    the real cvars after Apply. Each value read back from `config.cfg` after quit equals the last probed value
    (0 mismatches). The skill, server, chat and lock staging cvars (`menu_sk_*`, `menu_public`, ...) are not
    archived by design.
  - Inputs (hostname, joystick name, chat, lock password) take typed text and are not stepped. Sliders at their
    maximum (`s_om_gain`, `joy_sidesensitivity`) stay put.
  - Key binding: d-pad to "+forward", ENTER, P gives `bind P "+forward"` in `config.cfg`.

## 2026-10-27 menu-scratch 2: openMERC wide layout of the folder screens

Owner brief: a from-scratch widescreen layout in the SoF style, not the 640x480 panels scaled up next to a rail.

- **Where.** `menu_layout 1` (archived, video page row "Menu Layout": Wide / Classic). Screens with a `<frame topic>`
  (every stock tab page) are laid out by `m_wide.c`; the main menu, player setup and dialogs keep the 640x480 canvas.
  The menus are scaled by `MR_ScaleFor(640)` (1080p 2 = 960x540 units, 1440p 3 = 853x480, 2560x1080 2 = 1280x540,
  1440x1080 2 = 720x540, 1920x1200 2 = 960x600).
- **Composition.** Nav panel (204 units, the former rail) at the left edge with a 10-unit margin, shown when the
  window is at least 832 units wide (not at 4:3); right of it the tab strip (30), the panel with the title band, the
  content area and the footer (ticker + HELP/DEPLOY/HOME/GAME/RETURN buttons; GAME only in game). Frames, the
  panel, the tabs and the buttons are drawn by code (`MWD_Chrome`: flat fills, 1-2 px edges, orange accent; see
  "menu-scratch 3"); the earlier generated 9-slice pictures and `gen_chrome.py` are gone.
- **Stock pages inside.** The web_folders model and its decorations are not created; the widgets bolted to it are
  re-slotted (`MWD_BoltSlot`): tab pictures into the tab strip with their label, the header into the title band,
  the ticker and the control buttons into the footer. `main` takes the whole content area; frames cut from it with
  a fixed size widen with it (x `main.w/560`, heights over 32 by `main.h/292`), and `<tab>` stops on these pages are
  scaled by the same factor (`wg.sx`), so list columns and label/value splits use the width.
- **Fixes on the way.** Mouse wheel scrolls the innermost list under the pointer (before: the outer frame). The
  ticker is static when it fits and starts readable at the left when it scrolls. Footer labels come from
  `menu_omset`, now loaded by the engine (before: only by the rail page, so at 4:3 the buttons were empty). Tab
  icons drawn with the stock picture path and rotation. List cells keep a 6-unit gap to the next column. The
  1.07f GameSpy patch's join screen (`gs.pak`) has a GameSpy logo frame beside the topic; it covered the server
  list and is dropped (`MWD_Frame`, `nopage`). The server-list status no longer names GameSpy. The main menu and
  player setup drop the nav panel when the 640 canvas does not fit beside it with margins (2560x1440). Own
  `m_start.rmf`: map name and game type as "label : value", the GameSpy global-rankings row left out, so the
  create-server column fits without scrolling.
- **Version.** `om_version` (read-only) = `OM_VERSION_STRING` = "openMERC V" + `git describe` (common/header/om_version.h; CMake writes `OM_VERSION` into the generated om_version_gen.h and re-runs when HEAD moves, fallback `0.1.1-dev`),
  shown under the main menu (`menus/version.rmf`), in the console corner (was "Yamagi Quake II v8.71pre") and in
  the start-up line ("openMERC <v> (Yamagi Quake II <v> base)"). The console corner version and clock are drawn
  in plain characters: the upper half of the SoF console font holds the Cyrillic letters in the Russian release,
  so the Quake 2 "+128" colour set printed Cyrillic garbage. The serverinfo `version` cvar is unchanged.
- **Effects rows.** With the gfx-modern renderer DLLs in the test install the EFFECTS block (bloom, volumetric fog,
  world shadows, soft particles, per-pixel fog, per-pixel dynamic lights, overbright) shows in the wide layout; the
  per-pixel dynamic lights row is enabled in `om_video.tbl` after its A/B check.
- **Evidence** (review box `/workspace/menu_check/scratch2/`): `w4/<tag>/` every page and the in-game screens at
  1920x1080, 2560x1440, 2560x1080, 1440x1080, 1920x1200, contact sheets `sheets/w4*.png`; later fixes `w5/`,
  `w5fx/` (EFFECTS rows), `w3/console2.png` (console version).

## 2026-10-27 menu-scratch 3: modern scaling, TrueType text, rail main menu (owner t91u, t94u)

Owner: the rail + tabs + pages layout stays, but everything was too large (whole 2x/3x blow-ups of the 640x480
bitmap fonts); crisp text sized by the window height, SoF colours; the animated ARC main menu removed entirely.

- **Scale.** The wide layout is laid out on a window 720 units tall (`MR_MenuScale`: f = H/720, at most W/640; 1080p
  1.5, 1440p 2, 2160p 3, 2560x1080 1.5 = 1707x720 units, 1440x1080 1.5 = 960x720). `ui_scale > 0` and the classic
  layout keep the earlier rules. Page zoom (sparse pages magnified) is off by default; cvar renamed `menu_zoompages`
  (an archived `menu_pagezoom 1` must not keep it on).
- **Text.** `menus/m_font.c` (stb_truetype, `header/stb_truetype.h`) rasterizes the menu fonts per style and pixel
  size (share of the window height: small 1.5 %, medium 1.72 %, title 1.9 %, interface 2.8 %, logo 5.6 %,
  logosmall 2.3 %; `menu_textsize` 0.6-1.6 multiplies) into alpha atlases (LRU of 16) uploaded through the gl3
  renderer's export `OM_SetAlphaImage` (RGBA, nearest, kept across registration). `MR_DrawString` draws the glyphs
  at whole window pixels (2D scale 1 around the string); widths and heights are converted back to menu units, so
  the layout code is unchanged. CP1251 is mapped to Unicode; kerning on. Fallback to the stock bitmap fonts with
  `menu_ttf 0`, another renderer, or missing font files (`logo`/`logosmall` map to title/medium there).
- **Fonts** (`data/base/fonts/om/`, packed into `zz_openmerc.pak`): Play Regular/Bold and Russo One, SIL OFL 1.1,
  from github.com/google/fonts; licences beside them (`OFL-*.txt`, `README.md`). Not game data.
- **Main menu.** The animated ARC globe menu is gone: `menu amain`, `menu a_main`, `menu a_amain` (intro, stock home
  buttons) open `main` (`M_OpenMenu`); the `menu_animated` cvar and the own `amain.rmf` are removed. `main.rmf` is a
  folder screen (`t_omhome.rmf`: one tab "Home", title, ticker) with the landing page `m_omhome.rmf`: SOLDIER OF
  FORTUNE in the logo font, the openMERC version, and Continue (only in a level, cvar `menu_ingame` set when a
  screen is built) / New game / Load / Multiplayer / Settings (video page) / Exit as `ombutton` bars (text option:
  centred label on a full-width bar, orange edge when hovered). Esc in a level and the start-up open it.
- **Dialogs** over the wide screens darken their inside (the quit dialog let the landing buttons show through).
  Audio page volume sliders are centred with the rest of the page.
- **Evidence** (review box `/workspace/menu_check/`): `ttf/` (first TrueType captures, landing page 1080p/1440p/4:3/
  ultrawide, in-game Esc), `w9/` every page and in-game screen at the five sizes with contact sheets.
- **Known.** Stock bitmap pictures (icons, slider art, save thumbnails, player setup canvas) are still scaled with
  the factor (linear at fractional factors). Stock `m_game2` prints its "takes effect after a new game" line at the
  left edge (stock page text, normal flow).

## menu-scratch 3 (2026-10-27): own chrome, landing page, version, no parental locks

Owner t95u/t102u and review: the menu must not show retail menu art; the landing page balanced; full violence always.

- **Own drawing.** `m_wide.c` `MWD_Chrome` draws panel, well, tab, current tab, button and groove as flat boxes on
  whole window pixels (`MR_FlatBox`, `MR_FlatGradient`, `MR_FlatArrow` in `m_draw.c`); the wide backdrop is a
  vertical gradient (`MWD_DrawBackground`, `<backdrop omflat>`), not the tiled stock `web_bg`. Tabs and rail rows
  have no icons (they were the stock `icons/f_*`). Sliders and scroll bars of wide pages are drawn flat; the mouse
  pointer is a procedural arrow (`MR_DrawOwnCursor`, alpha picture sized by the window). Own colour names in
  `menus/om_tints.rmf` (main, rail, start screens). Stock pages and pictures that still load from the retail paks:
  see the report of this round (stock rmf pages inside the frame, difficulty pictures, player models, menu sounds,
  bitmap fonts as fallback).
- **Landing page.** Logo "openMERC", version, Continue / New game / Load / Multiplayer / Settings / Exit, then the
  newest save (shot, level, file time; `M_NewestSave` in `m_data.c`) or "No saved games yet". Continue
  (`om_continue`) returns to the running level, else loads the newest save. Esc in a level draws the live game,
  dimmed, behind the menu (`menu_livebg 1`, `SCR_SoF_Frame`); panels are more transparent then.
- **Version.** Menus, window title and console stamp show `OM_VERSION_STRING` ("openMERC V0.2.0"); git describe only
  in the build string (start banner, `--help`, cvar `om_build`).
- **Ё** allowed in strings (TTF and the bitmap fallback, which maps it to Е).
- **No parental locks** (owner t102u): `sof_locks.c` registers the `lock_*` cvars read-only at 0, no registry
  store, password or German rule; the rail lock row, the gameplay page's violence button, `checkpass`/`changepass`
  and the menu server-list violence filter are removed; `menu lock` / `menu change` open nothing.
