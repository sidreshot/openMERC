# SPEC-801: Menu stack, commands, input, frame loop, cursor, tooltips and engine hooks
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (menu system and its hooks in the client, console and input code), version 1.07f
Depends on: SPEC-800 (menu runtime core), SPEC-802, SPEC-803 (widgets), SPEC-002 (renderer), SPEC-004 (sound
library), SPEC-302 (cvars, commands, key binding), SPEC-131 (string packages), SPEC-303 (savegames)

## Purpose

Describes the dynamic side of the menu system: when menus open and close and what that does to the game, the stack of
menu screens and the console commands that manipulate it, how console commands issued by menus are queued, the order
of work in every frame, how keys, mouse, wheel and joystick reach menu widgets, the mouse pointer, tooltips, menu
sounds, and the places where the engine opens menus by itself (main menu, loading screen, disconnect and error pages).

## Interface / layout

### Console commands (registered at client start)

| Command | Arguments | Section |
|---|---|---|
| `menu` | `<name> [frame]` | 2 |
| `intermission` | `<name> [frame]` | 2 |
| `popmenu`, `killmenu`, `menuoff`, `return` | — | 3 |
| `refresh`, `reloadall` | — | 4 |
| `select` | `<cvar>` | 4 |
| `checkpass` | `<cvar> <ok-menu> <fail-menu>` | 4 |
| `changepass` | `<old> <new> <verify> [ok-menu] [fail-menu]` | 4 |
| `freshgame`, `hackteam`, `userweapons`, `useritems`, `userammo`, `usermisc` | see 4 | 4 |

### Cvars used by the menu system

| Cvar | Default, flags | Meaning |
|---|---|---|
| `menu_mouse_x`, `menu_mouse_y` | "0" | pointer position in screen pixels (set to the screen centre when the menu system is created) |
| `m_cursor_speed` | "8", archive | maximum keyboard pointer step (section 7.4) |
| `m_cursor_accel` | "2", archive | keyboard pointer acceleration (section 7.4) |
| `m_sensitivity_wheel` | "1" | wheel scroll multiplier (section 7.3) |
| `tip_duration` | "2500", archive | tooltip lifetime in milliseconds (section 8) |
| `cursor_blink_speed` | "500", archive | text-cursor blink period for input widgets (SPEC-802) |
| `ghl_no_texture_stack` | "0", archive | Ghoul rendering option of menu models (SPEC-803) |
| `menu_animated` | — | set by menu data; reset to 0 whenever the menu system closes |
| `scr_menuoverlay` | "0" | when non-zero, the 3D view, HUD and console are not drawn while menus are shown; reset to 0 when the menu system closes |
| `scr_showpause`, `paused` | | see section 1 |
| `player_summary`, `server_summary`, `locked_summary` | | server-browser totals, reset to "0/0", "0", "0" on open (SPEC-803) |
| `menu_dc_reason`, `menu_won_error`, `menu_loading` | | texts and progress for the engine-opened pages (section 10) |
| `m_doublespeed` | "200" | double-click time in milliseconds (section 7.2) |

### Sounds (all played as local, non-positional sounds; registered at client start)

| Sound | Played when |
|---|---|
| `menu/small/select.wav` | a widget key binding fires (SPEC-800 §7), and by several widgets (SPEC-802/803) |
| `menu/small/invalid.wav` | a menu cannot be opened or left (sections 2, 3), and by widgets |
| `menu/small/hilite.wav` | a tooltip appears (section 8) |
| `menu/small/click.wav` | input widgets (SPEC-802) |

## Behaviour

### 1. The menu system: opening and closing

The menu system exists from the first `menu`/`intermission` command until it is closed (section 3). Creating it:

1. Stops all sounds (sound library `S_StopAllSounds`, SPEC-004).
2. Single player only (`maxclients` = 1 and a local server running): if the keyboard was in game mode, a 128 × 128
   picture of the current screen is captured for the next savegame thumbnail (SPEC-002 slot 0xd4, SPEC-303); if no
   picture exists yet and the keyboard was not in game mode, an all-black picture is used.
3. Hidden cvars become locked again (see `checkpass`).
4. The pointer is placed at (W_vid ÷ 2, H_vid ÷ 2); keyboard input goes to the menus; `scr_showpause` := 0;
   `paused` := 1 if `maxclients` = 1 and a local server is running (single-player games pause behind menus);
   the three server-browser summary cvars are reset.

Closing (`menuoff`, or the last screen popped): all screens are destroyed; every picture registered by the menus
(`pics/menus/…`) is released; menu Ghoul models are released; keyboard input returns to the game; all key-down states
are cleared; `scr_menuoverlay` := 0, `scr_showpause` := 1, `paused` := 0; hidden cvars are locked; string packages
loaded by menus are released (SPEC-800 §5.2); the savegame thumbnail is discarded; `menu_animated` := 0.

### 2. Opening menus: `menu <name> [frame]` and `intermission <name> [frame]`

Missing name: prints `Usage: menu <name> [frame]` (resp. `Usage: intermission <name> [frame]`). Both names are
converted to lower case. `intermission` is identical except that it marks the whole menu system as **forced**
(the mark stays until the system is closed) — a forced menu cannot be closed by the player (section 3).
The command does nothing while the screen is disabled for a video restart; while the client is in a state where the
screen is not updated, the request is deferred (UNVERIFIED which state — the engine sets a pending flag instead).

**Frame given (not `default`):** the page `<name>` is loaded into frame `[frame]` of the **current** screen, replacing
what is shown there (SPEC-800 §8; page history: if that frame already showed a page called `<name>` earlier, the frame
goes back to it and refreshes it instead of parsing it again). No new screen is created. Errors:
`Error : Cannot find frame "<frame>"`, `Error : Could not create "<name>"`. Nothing happens while the current screen is
not ready (`waitanim`).

**No frame (or `default`): a new screen.**

1. If the current screen has an `<exitcfg x>`, `exec menus/x` is queued (forced, each `;`-separated part separately).
2. If the current screen has an `<onexit cvar m>` and the cvar ≠ 0, `<name>` is replaced by `m` (the "apply your
   changes?" page is shown instead of the requested menu).
3. If the current screen is not ready (SPEC-800 §5.5 `waitanim`), `menu/small/invalid.wav` is played and nothing else
   happens.
4. If a screen named `<name>` that is not `nopush` is already on the stack, all screens above it are removed and it
   is refreshed (section 4) — menus are never duplicated on the stack; the command ends here.
5. Otherwise a new screen is pushed and `<name>` is loaded into its `default` frame. If that fails (file missing,
   not STM, a frame failed, a widget cvar could not be created), the new screen is removed again (and if the stack is
   then empty the menu system closes).

Opening time is printed with a developer print: `Overall menu creation time: <n> ms`.

### 3. Leaving menus

**Pop rule.** Removing the top screen is *allowed* when:

- the `console` cvar ≠ 0 and the menus are not forced; or
- the client is not in a level (connection state below "connected and loading", or the menus are forced): only if a
  screen without `nopush` remains below it; or
- in a level, not forced: always, except in single player (`deathmatch` 0) while fully in the game with a **dead**
  player, where it is allowed only if a screen without `nopush` remains below.

So at the start-up main menu (no console) ESC is refused, in a running game ESC from the first menu returns to the
game, and after dying in single player the menus cannot be left without loading or restarting.

| Command / key | Effect |
|---|---|
| `popmenu`, ESC | If the top screen has `<onexit>` with its cvar ≠ 0: queue `menu <onexit menu>` (forced) and stop. Else if it has `<exitcfg x>`: queue `exec menus/x` (forced). Then pop the top screen if the pop rule allows (else `invalid.wav`); stop all sounds; keep popping while the new top screen is `nopush` (each time checking the rule); refresh the new top screen (section 4). If no screen is left the menu system closes |
| `killmenu` | pops the top screen without any rule, `onexit` or `exitcfg`; stops all sounds; refreshes the new top; closes the system if empty |
| `return` | `onexit`/`exitcfg` handling as for `popmenu`; then closes the **whole** menu system if: `console` ≠ 0 and not forced; or in a level and not forced and not the single-player dead case; or in the single-player dead case when a non-`nopush` screen exists below the top. Otherwise `invalid.wav` |
| `menuoff` | closes the menu system unconditionally (section 1) |

Every pop or kill also restarts the 0.2 s "mouse hold" delay of section 7.2.

### 4. Other commands

| Command | Effect |
|---|---|
| `refresh` | refreshes the top screen: executes `exec <config>.cfg` again if the screen has `<config>`; then for every frame: if it has `cpage` and the cvar's value differs from the page shown, queues `menu <value> <frame>` (forced) and clears the cvar's modified flag; then if the frame's page was marked by `reloadall` it is destroyed and parsed again from its file, otherwise every widget of the page refreshes itself (SPEC-802/803 — e.g. re-reads its cvar) |
| `reloadall` | marks, in every screen, the pages of every frame except the `default` frames for re-parsing at their next refresh (used after a language change; the visible effect comes with the next `refresh` or return to the screen) |
| `select <cvar>` | gives the cvar to the outfitting `selection` widget of the top screen whose category matches the cvar's inventory flag: 0x0100 weapons, 0x0200 items, 0x0400 ammo, otherwise misc (SPEC-802) |
| `checkpass <cvar> <ok> [fail]` | compares the stored parental-lock password with the cvar's string (case-insensitive). Equal: hidden cvars (flag 0x80, SPEC-302) become accessible until the menu system closes, and `menu <ok>` is appended to the console buffer; otherwise `menu <fail>`. Fewer than 2 arguments: `Too few parameters.` |
| `changepass <old> <new> <verify> [ok] [fail]` | fewer than 3 arguments prints the usage line `Usage: changepass <oldpass> <newpass> <verify newpass> [passmenu] [failmenu]`. If the stored password cannot be read: `Failed to get original password`; if `<old>` differs (case-sensitive): `Incorrect old password`; if `<new>` ≠ `<verify>`: `New password differs from verfied password` (sic) — each printed in colour 2, then `menu <fail>`. Otherwise the password is set to `<new>` and `menu <ok>` runs |
| `freshgame` | in a level (state ≥ connected/loading): appends `menu exit` (without line end) to the console buffer; otherwise `newgame` |
| `hackteam <skin> <teamname>` | if `skin` differs from the cvar `skin` (ignoring case): sets `skin` and `teamname` |
| `userweapons`, `useritems`, `userammo`, `usermisc` | print `Weapons:`, `Items:`, `Ammo:` or `Misc:` (colour code 7) and the info string of all cvars with flag 0x0100 / 0x0200 / 0x0400 / 0x0800 |

### 5. Menu command queue

Commands produced by menus (widget and page key bindings, `<cbuf>`, `cpage` changes, `onexit`/`exitcfg`) do not go
directly to the console buffer. They are split at `;`, CR and LF (empty parts dropped) and appended to a queue:

- A *forced* request is always appended. A *non-forced* request (key bindings) is **discarded entirely if the queue is
  not empty**.
- Once per frame, after the menus were drawn, the first queued command plus a line end is appended to the console
  buffer and removed from the queue.

Consequences: a binding `"a;b;c"` runs one command per frame over three frames; if several bindings fire for the same
key in the same frame only the first one is kept (the widget's own binding before the page's, section 7.1); a second
click before the queue has drained is ignored.

### 6. Frame loop and drawing order

Each client frame (inside the screen update, after the 3D view, HUD and console have been drawn, SPEC-002 order):

1. If keyboard input belongs to the menus: the menu frame runs:
   1. current time is sampled; pointer movement is applied (section 7.2); joystick and joystick buttons are read;
   2. the renderer is switched to 2D (slot 0xb4 with 0) and the Ghoul model system begins a menu frame;
   3. the top screen is drawn (below); the Ghoul model system ends the menu frame.
2. One queued menu command is moved to the console buffer (section 5).
3. The frame is presented.

If `scr_menuoverlay` ≠ 0 the 3D view, HUD, crosshair, labels and console are skipped entirely (the menu draws over
whatever the menu background paints).

**Screen drawing.**

1. The list of tab-navigable widgets is rebuilt if needed (first draw, or after a refresh).
2. If the screen is a `resize` window: while the right mouse button is held the window follows the pointer (the
   window's origin moves by the pointer movement since the button went down; all frames move with it); then the
   screen **below** it is drawn first, completely, without input (recursively, so stacked popups show through).
3. The frame under the pointer is determined: the first frame (in creation order) whose content area (free rectangle
   minus padding) contains the pointer.
4. For each frame in creation order: mouse processing of its page (section 7.2, top screen only), then the frame is
   drawn (SPEC-800 §8–10).
5. The mouse pointer (section 9) and the tooltip (section 8) are drawn.
6. `<timeout>`: if set and its time has passed (measured from the screen's open time), its command is appended to
   the console buffer (directly, not through the queue) and the timeout is cleared.

### 7. Input

#### 7.1 Keys

While the menus have keyboard input, key events reach the menu system for key **presses only** (releases are only
used for button states), **including auto-repeat** (unlike in the game). Keys flagged in the engine key table as
"menu-bound" — PAUSE and F1–F12 — run their normal console binding instead (ESC is handled below). The console key
does not open the console while menus are shown.

Routing of a key press (in this order):

1. **Keyboard capture.** If a widget holds the keyboard (an input field being edited, a key-binding row waiting for a
   key, SPEC-802): ESC ends the capture; any other key whose character is not `` ` `` is given to that widget only.
2. **With LALT or RALT held:** Ctrl (left or right) = a MOUSE1 press (below); arrow keys move the pointer (7.4); other
   keys are ignored.
3. **Otherwise:**

   | Key | Effect |
   |---|---|
   | ESC | `popmenu` behaviour (section 3) |
   | MOUSE2 | opens the popup menu of the page under the pointer, if any (SPEC-802), then is delivered as a key |
   | TAB | moves the pointer to the next tab-navigable widget (7.5), then is delivered as a key |
   | ENTER, KP_ENTER | delivered first as MOUSE1, then as itself |
   | MWHEELUP / MWHEELDOWN | scroll (7.3) |
   | UPARROW / DOWNARROW with a SHIFT held | scroll like the wheel up / down |
   | any other key | delivered |

**Delivery** of a key K to the top screen: ignored while the screen is not ready, unless it is `fragile`. If K is
MOUSE2, the pointer position is remembered (start of a window drag). Then, for every frame (creation order), its
current page receives K: (a) the widget holding the page's focus, (b) the hovered widget if different, each run their
key handling (SPEC-802/803; by default: every matching widget binding plays `select.wav` and queues its command),
(c) every matching page-level binding queues its command and marks the screen ready. A MOUSE1 press also ends any
mouse drag on that page.

#### 7.2 Mouse

- **Movement.** Each menu frame the pointer moves by the raw Windows mouse movement in screen pixels (no sensitivity
  applied); if `m_filter` ≠ 0 the movement is averaged with the previous frame's. The pointer is clamped to
  −8 … W_vid + 8 and −8 … H_vid + 8. `menu_mouse_x`/`menu_mouse_y` hold the position and may be set by commands.
- **Buttons** arrive as keys MOUSE1…MOUSE5 (7.1). A second press of the same button within `m_doublespeed`
  milliseconds additionally produces a press and release of MOUSE1DBL…MOUSE5DBL right after it (used by `key mouse1dbl`).
- **Hover** (every frame, for each page of the top screen): the pointer is converted to page coordinates
  (relative to the frame's content area, plus the page scroll). If the page has a widget that is being dragged and
  MOUSE1 is held, that widget receives "activate (button 1)" and hover is not updated this frame. Otherwise every
  widget's hover flag is recomputed: a widget is hovered when the pointer is inside the frame's content area (any
  position for `global` pages) and inside the widget's total rectangle [x, x + w − 1] × [y, y + h − 1] (the drawing
  offsets `xoff`/`yoff` are not applied). Several widgets can show the hover colour, but the page's **hovered widget**
  (the one receiving keys and tooltips) is the last interactive widget, in creation order, under the pointer.
  When the hovered widget changes to a widget with a `tip`, its tooltip starts (section 8).
- **Holding a button.** If at least 0.2 s have passed since the menu system was created or a screen was last popped,
  and MOUSE1, MOUSE2 or MOUSE3 is held (the first of these), the hovered widget receives "activate (button)" **every
  frame** while it is held (sliders and scroll arrows use this; the common `inc` option of SPEC-800 applies on every
  such frame).

#### 7.3 Scrolling

Wheel up / down scroll the page in the frame under the pointer, only if that page has a vertical bar (SPEC-802):
scroll offset += int(∓16 × `m_sensitivity_wheel`) (up = −), then the offset is clamped to 0 … content height −
(frame height − 2 × padding).

#### 7.4 Keyboard pointer

With ALT held, each arrow-key press (and auto-repeat) first increases a speed v := min(v + `m_cursor_accel`,
`m_cursor_speed`) and then moves the pointer by v pixels in that direction. v starts at 0 when the menu system is
created and is **never reduced**, so after a few presses the pointer always moves by `m_cursor_speed` pixels
(defaults: 2, 4, 6, 8, 8, …). ALT+CTRL acts as a MOUSE1 press (joystick-style use).

#### 7.5 TAB navigation

The tab list contains every interactive widget (SPEC-800 §7) of the pages shown in the screen's frames, in frame then
creation order. TAB selects the next entry after the hovered widget (or after the last TAB position if nothing is
hovered); SHIFT+TAB the previous one, wrapping around; widgets not in view are skipped. The pointer is moved to the
centre of the chosen widget's total rectangle on screen. Nothing happens if the list is empty.

#### 7.6 Joystick

Joystick buttons arrive as keys JOY1–JOY4 and AUX1–AUX32 and are routed like any other key (page bindings such as
`<key aux1 …>` work). Joystick axes do not move the menu pointer (UNVERIFIED — check: open a menu with a joystick
connected and move the stick).

### 8. Tooltips

- Created when the hovered widget changes to one with `tip`: position x = pointer x, y = bottom edge of the widget on
  screen; duration D = `tip_duration` (ms); the previous tooltip is discarded.
- Nothing is drawn during the first D ÷ 4 ms; then `menu/small/hilite.wav` is played once and the tooltip is shown.
- Size: box width = SW(text, tipfont) + 4, box height = FH(tipfont) + 4; x is moved left so that x + box width ≤ W_vid.
- Drawing: a bordered box (SPEC-002 slot 0x78) at (x − 2, y − 2) of that size in the `tipbtint` colour, then the text
  (slot 0xc8) at (x, y) in `tiptint`, font `tipfont` (SPEC-800 `<defaults>`).
- Both colours' alpha is replaced by: 255 × (t − D/4) ÷ (D/16) during the first D/16 ms after appearing, 255 while
  shown, and 255 × (D − t) ÷ (D/16) during the last D/16 ms (t = ms since creation, integer arithmetic, D/4 and D/16
  by arithmetic shift).
- The tooltip is deleted when t > D. Tooltips are not drawn while the screen is not ready.

Example: D = 2500 → silent until 625 ms, fades in until 781 ms, full until 2344 ms, fades out until 2500 ms.

### 9. Mouse pointer

- The pointer setting comes from the page shown in the frame under the pointer (`<cursor n>`, SPEC-800; a frame
  without a page hides the pointer); while the pointer is over no frame's content area the last setting stays.
  Default: visible.
- Drawn last, in opaque white, with the picture `pics/menus/cursor/cursor` stretched (slot 0x54, rotation 0) to
  int(32 × s) × int(32 × s) at (int(x − 16 × s), int(y − 16 × s)) — the hot spot is the picture's centre.
  Example: 1024×768, pointer at (512, 384) → drawn at (486, 358), size 51 × 51.

### 10. Menus opened by the engine

| Situation | Effect |
|---|---|
| ESC while the keyboard belongs to the game or the console (and in the attract/demo state for any key) | console text `menu main` |
| Loading a level (loading plaque and the `loading` command) | closes the menu system, stops sounds and CD audio; unless the screen is disabled, the `dedicated` or `developer` cvar is non-zero, or the console has the keyboard: executes `menu loading` immediately and redraws the screen. During loading the engine sets `menu_loading` to the progress value and redraws (shown by an `hbar`, SPEC-802) |
| Disconnect with a reason (server message carrying a string-package id, connection failures) | `menu_dc_reason` := the localised text of that id; console text `menu disconnect` |
| Map file cannot be opened | `menu_dc_reason` := text 0x416 of the string packages; disconnect error (below) |
| Any engine error (Com_Error) | closes the menu system and deletes the `current` savegame; then by level: **disconnect** (2) → the client drops and console text `menu disconnect` is added; **drop** (1) → `console` is forced to 1, the local server shuts down with `Server crashed`, the client drops (no menu: the console is shown); level **3** → server and client drop, `menu_won_error` := text 0x411 of the string packages and console text `menu wonerror` is added (that page shows it with `<includecvar menu_won_error>`); level 0 is fatal |

### 11. Not part of the menu system

The in-game HUD (`scr_interface`, status bars, team icons) is drawn by separate client code from `pics/interface*`
pictures and does not use `.rmf` pages, frames or widgets.

## Constants

| Name | Value |
|---|---|
| mouse-hold activation delay after open/pop | 0.2 s |
| wheel step | 16 pixels × `m_sensitivity_wheel` |
| pointer clamp margin | 8 pixels |
| pointer picture | `pics/menus/cursor/cursor`, 32 × 32, hot spot (16, 16), scaled by s |
| tooltip padding | 2 pixels (box 4 larger than the text) |
| tooltip phases | appear at D/4, fade D/16 in and out, D = `tip_duration` ms |
| key-binding blink (SPEC-802) | 256 ms sawtooth |
| menu-bound keys | PAUSE, F1–F12 (and ESC) |

## Test vectors / how to verify

1. **No duplicates on the stack.** `menu a; menu b; menu a` (each on its own frame) → the stack is a, with b removed;
   ESC then leaves a.
2. **ESC at the start-up menu.** With `console 0`, not connected, `menu main`, press ESC → `invalid.wav`, menu stays.
   With `console 1` → the menu system closes.
3. **Queue drop.** A page with `<text "X" key mouse1 "echo one">` and `<key mouse1 "echo two">`: clicking X prints
   only `one`. `<key mouse1 "echo a;echo b">` prints `a` and `b` on consecutive frames.
4. **ENTER.** With the pointer over `<text "X" key mouse1 "echo m" key enter "echo e">`, pressing ENTER prints `m`
   only (MOUSE1 first; the ENTER binding is dropped by the queue).
5. **Keyboard pointer.** ALT+RIGHTARROW pressed five times from x = 320 → x = 322, 326, 332, 340, 348.
6. **Tooltip timing.** `set tip_duration 1600`: the tip appears 400 ms after hovering (with `hilite.wav`), fully
   opaque from 500 ms, starts fading at 1500 ms, gone at 1600 ms.
7. **Pointer.** At 640×480 the pointer at (100, 100) is drawn as a 32 × 32 picture at (84, 84).
8. **onexit.** On a screen with `<onexit menu_settings_changed altervid>` after `set menu_settings_changed 1`:
   ESC opens `altervid` and the screen stays below it; `menu other` also opens `altervid` instead of `other`.
9. **Thumbnail.** In single player, open the menu with ESC and save: the savegame picture shows the game view at the
   moment ESC was pressed (SPEC-303).
10. **Pause.** Opening a menu in a single-player game sets `paused 1`; closing it sets `paused 0`.

## Open questions

1. The client state in which `menu …` is deferred instead of executed (a pending flag is set; check by issuing
   `menu main` while a cinematic or level load runs).
2. Joystick axis behaviour in menus (7.6).
3. Whether the `console` cvar condition in the pop rule is what the shipped configuration uses (`console` is 0 in
   retail configs; check `cvarlist console`).
