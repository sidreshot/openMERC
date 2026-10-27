# SPEC-312: Input — key numbers and events, bindings, console/chat keys, mouse, joystick, force feedback, user-command construction
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (engine executable), version 1.07f
Depends on: SPEC-202 §6 (user-command bit packing — approved, not repeated here), SPEC-302 (cvar/command lists and
defaults), SPEC-801 §7 and SPEC-802 (menu-side key and mouse handling), SPEC-003 (weapon prediction),
SPEC-400 (effect-driven force feedback), SPEC-131 (string packages), SPEC-301 (config files)

## Purpose
Defines how Soldier of Fortune turns Windows keyboard, mouse and joystick input into key events, console and chat
text, bound commands, the button commands (`+attack`, `+leanleft`, …), and finally the user command that the client
sends to the server every frame. The design is that of Quake II 3.19 (`client/keys.c`, `client/cl_input.c`,
`win32/in_win.c`); every difference is listed. The bit-level encoding of the user command is in SPEC-202 §6; this
spec defines how each field's value is obtained. Menu-side routing (what a menu does with a key) is in SPEC-801/802.

## Interface / layout

### 1. Key numbers
Key numbers are 0–255. Unlike Quake II they are **Windows virtual-key codes**: letters are the upper-case ASCII
codes, digits their ASCII codes, function keys 112–123, and so on. A few numbers are engine-defined (mouse buttons,
wheel, keypad Enter, right-hand modifiers, double clicks, joystick). Every key number with a name or a character is
listed in the table below; all other numbers have no name and cannot be bound by name.

Meaning of the columns:
- **Chars (unshifted / shifted)**: the character the key types, as four Latin-1 (Windows-1252) byte values, one per
  keyboard layout column 0–3 (see §2); "—" = types nothing in any layout; 00 = types nothing in that layout.
- **Keypad form**: when a key event arrives *without* the Windows "extended key" flag and this column is set, the
  key number is replaced by the keypad key shown (the numeric-keypad cursor keys with Num Lock off send the same
  virtual-key codes as the dedicated cursor block, but without the extended flag).
- **Flags**: `M` menu-bound (runs its binding even while a menu has key focus; it also cannot be bound or unbound
  while a menu has key focus); `C` text key (in the console and in an unconnected game screen the key edits the
  input line instead of running its binding); `T` typing click (a menu input field plays `menu/small/click.wav` when
  it receives this key, SPEC-802).

| Number | Name | Chars unshifted (0 1 2 3) | Chars shifted (0 1 2 3) | Keypad form | Flags |
|---|---|---|---|---|---|
| 1 | `MOUSE1` | — | — | — | — |
| 2 | `MOUSE2` | — | — | — | — |
| 3 | `MOUSE3` | — | — | — | — |
| 4 | `MOUSE4` | — | — | — | — |
| 5 | `MOUSE5` | — | — | — | — |
| 8 | `BACKSPACE` | — | — | — | CT |
| 9 | `TAB` | — | — | — | CT |
| 12 | (no name; keypad 5 with Num Lock off) | — | — | 101 `KP_5` | — |
| 13 | `ENTER` | — | — | — | CT |
| 14 | `KP_ENTER` | — | — | — | CT |
| 16 | `LSHIFT` | — | — | — | C |
| 17 | `LCTRL` | — | — | — | — |
| 18 | `LALT` | — | — | — | — |
| 19 | `PAUSE` | — | — | — | MT |
| 20 | `CAPS` | — | — | — | — |
| 21 | `RSHIFT` | — | — | — | C |
| 22 | `RCTRL` | — | — | — | — |
| 23 | `RALT` | — | — | — | — |
| 24 | `MWHEELDOWN` | — | — | — | — |
| 25 | `MWHEELUP` | — | — | — | — |
| 27 | `ESCAPE` | — | — | — | MT |
| 32 | `SPACE` | 20 20 20 20 | 20 20 00 00 | — | CT |
| 33 | `PGUP` | — | — | 105 `KP_PGUP` | C |
| 34 | `PGDN` | — | — | 99 `KP_PGDN` | C |
| 35 | `END` | — | — | 97 `KP_END` | C |
| 36 | `HOME` | — | — | 103 `KP_HOME` | C |
| 37 | `LEFTARROW` | — | — | 100 `KP_LEFTARROW` | CT |
| 38 | `UPARROW` | — | — | 104 `KP_UPARROW` | CT |
| 39 | `RIGHTARROW` | — | — | 102 `KP_RIGHTARROW` | CT |
| 40 | `DOWNARROW` | — | — | 98 `KP_DOWNARROW` | CT |
| 45 | `INS` | — | — | 96 `KP_INS` | CT |
| 46 | `DEL` | — | — | 110 `KP_DEL` | CT |
| 48 | `0` | 30 30 E0 30 | 29 3D 30 29 | — | CT |
| 49 | `1` | 31 31 26 31 | 21 21 31 21 | — | CT |
| 50 | `2` | 32 32 E9 32 | 40 22 32 22 | — | CT |
| 51 | `3` | 33 33 22 33 | 23 A7 33 A3 | — | CT |
| 52 | `4` | 34 34 27 34 | 24 24 34 24 | — | CT |
| 53 | `5` | 35 35 28 35 | 25 25 35 25 | — | CT |
| 54 | `6` | 36 36 2D 36 | 5E 26 36 5E | — | CT |
| 55 | `7` | 37 37 E8 37 | 26 2F 37 26 | — | CT |
| 56 | `8` | 38 38 5F 38 | 2A 28 38 2A | — | CT |
| 57 | `9` | 39 39 E7 39 | 28 29 39 28 | — | CT |
| 65–90 | `A` … `Z` | lower-case letter in all four layouts (61 … 7A) | upper-case letter (41 … 5A) | — | CT |
| 96 | `KP_INS` | 30 30 30 30 | 30 30 30 30 | — | CT |
| 97 | `KP_END` | 31 31 31 31 | 31 31 31 31 | — | CT |
| 98 | `KP_DOWNARROW` | 32 32 32 32 | 32 32 32 32 | — | CT |
| 99 | `KP_PGDN` | 33 33 33 33 | 33 33 33 33 | — | CT |
| 100 | `KP_LEFTARROW` | 34 34 34 34 | 34 34 34 34 | — | CT |
| 101 | `KP_5` | 35 35 35 35 | 35 35 35 35 | — | CT |
| 102 | `KP_RIGHTARROW` | 36 36 36 36 | 36 36 36 36 | — | CT |
| 103 | `KP_HOME` | 37 37 37 37 | 37 37 37 37 | — | CT |
| 104 | `KP_UPARROW` | 38 38 38 38 | 38 38 38 38 | — | CT |
| 105 | `KP_PGUP` | 39 39 39 39 | 39 39 39 39 | — | CT |
| 106 | `KP_STAR` | 2A 2A 2A 2A | 2A 2A 2A 2A | — | CT |
| 107 | `KP_PLUS` | 2B 2B 2B 2B | 2B 2B 2B 2B | — | CT |
| 109 | `KP_MINUS` | 2D 2D 2D 2D | 2D 2D 2D 2D | — | CT |
| 110 | `KP_DEL` | 2E 2C 2E 2E | 2E 2E 2E 2E | — | CT |
| 111 | `KP_FSLASH` | 2F 2F 2F 2F | 2F 2F 2F 2F | — | CT |
| 112–123 | `F1` … `F12` | — | — | — | M |
| 128–132 | `MOUSE1DBL` … `MOUSE5DBL` | — | — | — | — |
| 144 | `KP_NUMLOCK` | — | — | — | — |
| 145 | `KP_SCROLL` | — | — | — | — |
| 186 | `SEMICOLON` | 3B FC 24 3B | 3A DC A3 3A | — | CT |
| 187 | `=` | 3D 2B 3D 3D | 2B 2A 2B 2B | — | CT |
| 188 | `,` | 2C 2C 2C 2C | 3C 3B 3F 3C | — | CT |
| 189 | `-` | 2D 2D 00 2D | 5F 5F 00 5F | — | CT |
| 190 | `.` | 2E 2E 3B 2E | 3E 3A 2E 3E | — | CT |
| 191 | `/` | 2F 23 3A 2F | 3F 27 2F 3F | — | CT |
| 192 | grave accent (one back-quote character) | 60 F6 F9 27 | 7E D6 25 40 | — | CT |
| 193–196 | `JOY1` … `JOY4` | — | — | — | — |
| 219 | `[` | 5B DF 29 5B | 7B 3F B0 7B | — | CT |
| 220 | backslash (one `\` character) | 5C 60 2A 5C | 7C B0 B5 7C | — | CT |
| 221 | `]` | 5D B4 5E 5D | 7D B4 A8 7D | — | CT |
| 222 | apostrophe (`'`) | 27 E4 60 23 | 22 C4 00 7E | — | CT |
| 223 | `!` | 00 00 21 60 | 00 00 A7 60 | — | CT |
| 224, 225 | `AUX1`, `AUX2` | — | — | — | — |
| 226 | `AUX3` (also the extra "<>" key of European keyboards) | 00 3C 3C 00 | 00 3E 3E 00 | — | CT |
| 227–255 | `AUX4` … `AUX32` | — | — | — | — |

Notes on the table:
- Names are matched case-insensitively (§6). Names are stored and written in the case shown.
- Key 192 is named by the single back-quote character, key 220 by a single backslash, key 222 by a single
  apostrophe, key 223 by an exclamation mark.
- Numbers 0, 6, 7, 10, 11, 15, 26, 28–31, 41–44, 47, 58–64, 91–95, 108, 124–127, 133–143, 146–185, 197–218 have no
  name and no character (for example the Windows keys, Print Screen, F13+): events for them are processed but they
  cannot be bound, and they are shown as `<UNKNOWN KEYNUM>`.

### 2. Keyboard layout column
The character columns are selected by the cvar `sp_language` (integer, the string-package language of SPEC-131),
clamped to 0–3 (anything else → 0): **0 = English (US layout), 1 = German, 2 = French, 3 = British (UK layout)**.
The shifted column is used while `LSHIFT` or `RSHIFT` is held. There is no Caps Lock or AltGr handling: the typed
character depends only on the key number, the language and the Shift state. So the language setting, not Windows'
keyboard layout, decides what a key types.

### 3. Windows messages → key events
| Windows input | Key event(s) |
|---|---|
| key down / up (normal and "system" variants) | key = the virtual-key code, with the extended-key flag carried along; special cases: Enter with the extended flag → 14 `KP_ENTER`; Shift with hardware scan code 0x36 → 21 `RSHIFT` (other Shift → 16); Ctrl with the extended flag → 22 `RCTRL` (else 17); Alt with the extended flag → 23 `RALT` (else 18) |
| Alt+Enter (system key down, Enter) | not a key event; toggles `vid_fullscreen` between 0 and 1, but only when cvar `win_noaltenter` is 0 (default 1: no toggle) |
| mouse wheel (standard wheel message or the registered "MSWHEEL_ROLLMSG" message) | delta ≤ 0 → press and immediate release of 24 `MWHEELDOWN`; delta > 0 → 25 `MWHEELUP` |
| mouse button messages (left, right, middle, X1, X2) | the five button states (left = button 1, right = 2, middle = 3, X1 = 4, X2 = 5) are compared with the previous ones; each newly pressed button n gives a press of key n (`MOUSE`n), each newly released one a release; see §8 for double clicks |
| joystick buttons and hat (polled each frame, §9) | buttons 1–4 → 193–196 `JOY1`–`JOY4`; button n ≥ 5 → `AUX`n (n ≤ 32); hat up/right/down/left → `AUX29`/`AUX30`/`AUX31`/`AUX32` |
| window deactivated / activated | all held keys are released (§4.6); mouse capture is released / taken again |

Every key event carries the Windows message time in milliseconds (joystick events carry 0); the time is used by the
button commands (§7).

## Behaviour

### 4. Key event processing
A key event is (key number, pressed/released, time). It is processed with these rules, in this order:

1. **Keypad translation.** If the event has no extended flag and the key's "keypad form" is set (table §1), the key
   number becomes that keypad key. (Mouse, wheel and joystick events never have keypad forms.)
2. **Auto-repeat.** A per-key press counter counts presses since the last release (a release resets it to 0). A
   repeated press (counter > 1) is **ignored**, except for `BACKSPACE`, `PAUSE`, `PGUP`, `KP_PGUP`, `PGDN`,
   `KP_PGDN`, and except while a menu has key focus (menus receive all repeats, SPEC-801). Unlike Quake II no
   "… is unbound, hit F4 to set" message is printed.
3. **Console key.** The key's character in the current layout and Shift state (§2) is looked up. If that character
   is a grave accent (0x60) and no menu has key focus: a press runs `toggleconsole` (§5.1); a release does nothing.
   The console key is therefore the key left of "1" in each layout (US: key 192; German: key 220; French: key 222;
   UK: key 223 — the UK key also with Shift). Unlike Quake II, the tilde does not toggle the console.
4. **Escape and demo playback.** If the key is `ESCAPE`, or a demo is being played back and no menu has key focus
   (then *every* key, including F1–F12, counts as Escape): releases are ignored; a press does, depending on the key
   focus — game or console: the command `menu main` is queued; chat: the chat line is cancelled (§5.3); menu: the
   menu receives `ESCAPE` (SPEC-801). Nothing else happens with the event (the key is not marked as held). Quake II's
   "Escape with an open layout sends `putaway`" rule does not exist.
5. **Held state.** The key is marked as held (press) or not held (release). An "any key held" counter is increased on
   the first press of a key and decreased on a release (never below 0); it drives button bit 0x80 (§10).
6. **Release.** If the key's binding starts with `+`, the text `-<binding without the +> <key number> <time>` and a
   newline are queued. Nothing else happens on a release. (Quake II's extra release of the shifted key's binding does
   not exist: there are no shifted bindings.)
7. **Press — routing by key focus:**

   | Key focus | Key with flag | Result |
   |---|---|---|
   | menu | `M` | the binding runs |
   | menu | other | the menu receives the key (SPEC-801 §7.1) |
   | console | not `C` | the binding runs |
   | console | `C` | console line editing (§5.2) |
   | game, fully connected (in a level) | any | the binding runs |
   | game, not in a level | not `C` | the binding runs |
   | game, not in a level | `C` | console line editing (§5.2) |
   | chat | any | chat line editing (§5.3) |

   "The binding runs": nothing if the key is unbound or bound to the empty string; a binding starting with `+` queues
   `<binding> <key number> <time>` and a newline; any other binding queues the binding text and a newline, and the
   same text becomes the client's *pending weapon command* (SPEC-003 "Client prediction").
   Consequences: the mouse wheel (no `C` flag) runs its binding while the console is open — the wheel does not scroll
   the console; `CTRL`, `ALT`, F-keys and mouse buttons run their bindings in the console.

#### 4.6 Releasing all keys
When the game window is activated or deactivated, every key that is held or has a non-zero press counter receives a
release event (with time 0, so `+` bindings get their `-` command), and all held flags, press counters and the "any
key held" counter are cleared.

### 5. Console, console line editing, chat

#### 5.1 `toggleconsole`
- Has no effect at all unless cvar `console` is non-zero. `console` is created with the no-set flag (SPEC-302), so in
  practice the console is only available when the game is started with `+set console 1` on the command line.
- While a cinematic freeze is active (SPEC-202 player-state field 27) and the key focus is the game, it does not open
  the console but requests a cinematic skip (§11).
- Otherwise as Quake II 3.19: during demo playback it queues `killserver`; when disconnected it queues the text `demo`
  (no such command or shipped alias exists in 1.07f, so this normally prints the unknown-command message; UNVERIFIED);
  else it
  clears the input line and the notify lines and toggles between console and game focus, closing menus; when opening
  in a single-player game (`maxclients` 1 with a local server) it sets `paused` 1, when closing it sets `paused` 0.
  (SoF additionally captures the savegame thumbnail when the console opens.)
- `togglechat`, `messagemode`, `messagemode2`, `clear` behave as in Quake II 3.19.

#### 5.2 Console line editing
The console keeps 32 input lines (a ring) of at most 255 characters; each starts with the prompt `]`. On a key press
with console line editing (§4 rule 7):

| Key / condition | Effect |
|---|---|
| a Shift key held and `INS` or `KP_INS` held, or a Ctrl key held and `V` held | paste: the clipboard text, cut at its first CR, LF or backspace character, is appended, truncated so the line stays below 256 characters |
| a Ctrl key held and `L` held | queues `clear` |
| `ENTER`, `KP_ENTER` | the line without the prompt — and without one leading `\` or `/` if present — is queued as a command followed by a newline; the full line including the prompt is printed to the console; a new empty line starts and becomes the history position; when disconnected the screen is redrawn immediately |
| `TAB` | completion (below) |
| `BACKSPACE`, `LEFTARROW` | deletes the last character (never the prompt) |
| `UPARROW` / `DOWNARROW` | history: previous / next non-empty line is copied into the input line; going past the newest line gives an empty line |
| `PGUP` / `PGDN` | scroll the console text up / down by 2 lines (not below the newest line) |
| `HOME` / `END` | scroll to the oldest / newest text |
| any other key whose character (§2) is 32–255 | the character is appended (line limit 255) |

Differences from Quake II: characters come from the layout table, so non-ASCII Latin-1 characters can be typed; keypad
keys type their digits and operators (they are not mapped to cursor keys); `CTRL`+`H` is not backspace.
The same editor serves the text console of a dedicated server (SPEC-301).

**Completion (`TAB`).** The text after the prompt (and after one leading `\` or `/`) is the prefix P.
- First `TAB` (or the first after any other key): P is remembered; the first match is searched among commands (exact
  name match first, then the first command whose name starts with P; aliases are searched after commands), and if
  nothing matches, among cvars (exact, then prefix; hidden cvars excluded). A match replaces the line with
  `/<match> ` (slash, the match, one space).
- Each further `TAB` (with no other key in between) continues from the last match with the remembered prefix P: the
  next command/alias after the last one in list order whose name starts with P, then the cvars starting with P, then
  wrapping to the first command again — so repeated `TAB` cycles through all commands and cvars starting with P. List
  order is the engine's registration list order (newest first, as `cmdlist`/`cvarlist` print them; UNVERIFIED for
  aliases).

#### 5.3 Chat line
`messagemode` / `messagemode2` set key focus to chat (all / team). Key presses:
- `ENTER`, `KP_ENTER`: queues `say "<text>"` (or `say_team "<text>"`) and a newline — the text is not escaped — then
  clears the line and returns focus to the game.
- `ESCAPE` (or any key during demo playback, §4 rule 4): clears the line, focus to the game.
- `BACKSPACE`: deletes the last character.
- Other keys: the key's layout character (§2) is appended unless it is 0, LF or CR; at most 255 characters.

### 6. Bindings and key names
- `bind <key> [command …]`, `unbind <key>`, `unbindall`, `bindlist` as in Quake II 3.19, with these differences:
  - Key names are looked up case-insensitively among the names of §1. There is **no** single-character shortcut: a
    name that is not in the table (e.g. `~`, `;`) prints `"<name>" isn't a valid key`.
  - `unbind` and `unbindall` set the binding to the empty string instead of removing it. `bind <key>` then prints
    `"<key>" = ""`; `bindlist` and the config writer skip empty bindings.
  - While a menu has key focus, bindings of menu-bound keys (`ESCAPE`, `PAUSE`, `F1`–`F12`) are not changed by
    `bind`, `unbind` or `unbindall` (silently).
  - Any binding change marks the bindings as changed (menus re-read them, SPEC-802 `setkey`).
- Messages: `bind <key> [command] : attach a command to a key`, `"%s" isn't a valid key`, `"%s" is not bound`,
  `"%s" = "%s"`, `unbind <key> : remove all commands from a key`; `bindlist` prints `<name> "<binding>"` per key in
  key-number order.
- Key number → name: the table name; −1 → `<KEY NOT FOUND>`; a number without a name → `<UNKNOWN KEYNUM>`.
- **Localised key names** (shown by menus, SPEC-802): the string-package entry `KEY_<name>`, i.e. entry `<name>` of
  the package `KEY` (`strip/key.sp`), in the current language. If the entry does not exist the localised name is the
  empty string. The shipped package has no entries for `F1`–`F12` and none for `AUX26` (its `AUX25` appears twice),
  so these keys show an empty name (UNVERIFIED in game — see test 7).
- Bindings are written to the config file by name in key-number order (SPEC-301).

### 7. Button commands
Button commands are the console commands registered in pairs `+name` / `-name`. Each has the Quake II 3.19 state:
up to two keys holding it, the time it went down, the milliseconds it was down during the current frame, a state
(down, "went down this frame", "went up this frame"), plus one SoF addition — a **held flag** that is set when the
button goes down and cleared when its last holding key is released by a key event. (A `-name` typed without arguments
releases the button as in Quake II but leaves the held flag set; UNVERIFIED in game.)
`+name` and `-name` take optional arguments `<key number> <time>`; with no key number the command counts as typed at
the console (Quake II rules: "continuous down" / release everything). "Three keys down for a button!" is printed when
a third key presses an already doubly-held button.

The fraction of the frame a button was down (Quake II "key state") = milliseconds down this frame ÷ frame milliseconds,
clamped to 0–1; reading it resets the counted milliseconds. Frame milliseconds = system time since the previous user
command, clamped to 1–200.

| Command pair | Purpose (SoF) |
|---|---|
| `+forward` `+back` `+moveleft` `+moveright` `+moveup` `+movedown` `+left` `+right` `+lookup` `+lookdown` `+speed` `+strafe` `+klook` | as Quake II, with the changes in §10 |
| `+attack` | button bit 0x01; its release also stops all force-feedback effects (§12) |
| `+altattack` | button bit 0x04 (alternate fire); its release also stops all force-feedback effects |
| `+weaponExtra1`, `+weaponExtra2` | button bits 0x08 and 0x10 |
| `+use` | button bit 0x02; also the lean modifier (§10) and the cinematic/death "skip" (§10) |
| `+leanleft`, `+leanright` | the lean field (§10) |
| `+mlook` | mouse look (registered with the mouse code; Quake II semantics) |

Quake II's `impulse` command does not exist (the user command has no impulse field). `centerview` sets the view pitch
to the negative of the player state's pitch delta angle (Quake II). Releasing `+klook` or `+mlook` recentres the view
like `centerview` when `freelook` is 0 and `lookspring` is non-zero (Quake II does this only for `+mlook`).

### 8. Mouse
Mouse input is read only while the mouse is "active": cvar `in_mouse` non-zero, the mouse was initialised (cvar
`in_initmouse`, command line only), and the game window is the active window. While active the pointer is hidden,
captured, confined to the window rectangle (clipped to the screen) and moved back to the window centre after every
reading; Windows' mouse acceleration parameters are set to (0, 0, 1) and restored on release, as in Quake II. Unlike
Quake II, the mouse stays captured while the console or a menu is shown in windowed mode.

**Buttons.** See §3. After a press of button n, if the previous press of the same button happened less than
`m_doublespeed` milliseconds (message time) earlier, a press and a release of `MOUSE`n`DBL` (key 127 + n) follow
immediately. This happens in game and console as well as in menus, so `MOUSE1DBL` etc. can be bound.

**Movement in the game.** Done once per user command, only while the key focus is the game (in menus the pointer moves
instead, SPEC-801 §7.2; in the console mouse movement is not used). Per reading:
1. dx, dy = pointer offset from the window centre in pixels (right and down positive).
2. If `m_filter` ≠ 0: dx := trunc((dx + previous dx) × 0.5), dy likewise, where "previous" is the unfiltered offset of
   the previous reading.
3. Zoom factor z = (F + 85) / 180 × F / 95, where F is the horizontal field of view (degrees) of the view last
   rendered (so z = 1 at the default `fov` 95 and z < 1 while zoomed, e.g. sniper scopes). The original evaluates
   steps 3–4 in double precision with the constants 1/180 and 1/95; z is then exactly 1 at F = 95.
4. mx = trunc(`m_sensitivity_x` × dx × z), my = trunc(`m_sensitivity_y` × dy × z) (truncation toward zero; both
   sensitivities are integer cvars). If `m_invert_x` ≠ 0, mx := −mx; if `m_invert_y` ≠ 0, my := −my.
5. Horizontal: if `+strafe` is down, or `lookstrafe` ≠ 0 and `+mlook` is down: sidemove := trunc(sidemove + mx ×
   `m_side`); else view yaw −= mx × `m_yaw`.
6. Vertical: if (`+mlook` is down or `freelook` ≠ 0) and `+strafe` is not down: view pitch += my × `m_pitch`; else
   forwardmove := trunc(forwardmove − my × `m_forward`).
Differences from Quake II: the single float `sensitivity` is replaced by the two integer per-axis sensitivities, the
zoom factor and the invert cvars, and the intermediate values are truncated to integers.
The wheel cvar `m_sensitivity_wheel` is only used by menus (SPEC-801 §7.3).

### 9. Joystick
As Quake II 3.19 (`joyGetPosEx` polling, `in_joystick`, `joy_advanced`, `joy_advaxis*`, thresholds, sensitivities,
`joy_advancedupdate`, button and hat keys of §3, polled only while a joystick was detected — cvar `joy_avail` 1),
with these differences:
- Defaults: `joy_advanced` 1, `joy_advaxisx` 3 (side), `joy_advaxisy` 1 (forward), `joy_advaxisr` 4 (turn), other
  axes 0; all thresholds 0; sensitivities forward −2, side 2, up −2, pitch −2, yaw −2 (SPEC-302).
- Axis functions: 0 none, 1 forward, 2 look (pitch), 3 side, 4 turn (yaw), 5 up; value + 16 = relative axis.
  Axis value a = (raw − 32768) / 32768.
- Movement axes no longer use the run multiplier: forward: forwardmove := trunc(forwardmove + a × sensitivity ×
  `cl_forwardspeed`); side: trunc(sidemove + a × sens × `cl_sidespeed`); up: trunc(upmove + a × sens × `cl_upspeed`);
  turn used as strafe: trunc(sidemove − a × sens × `cl_sidespeed`).
- Angle axes keep Quake II's speed factor s (2 when `+speed` XOR `cl_run`, else 1) and frame time t, and the look and
  turn axes are multiplied by the zoom factor z of §8: absolute look: pitch += t·s × z × sens × a × `cl_pitchspeed`;
  relative look: pitch += s × z × sens × a × 180; turn likewise with yaw, `cl_yawspeed` and the yaw sensitivity.
  (The forward axis used as look when `joy_advanced` is 0 and `+mlook` is down has no zoom factor, as in Quake II.)
- Side axis while `+use` is held: instead of strafing, a positive value beyond the side threshold marks "move right
  held" and a negative one "move left held" for the lean rule (§10); whenever the side axis is beyond the threshold
  both marks are first cleared.
- Joystick input is applied only while the key focus is the game.
- Detection messages: `joystick not found -- no valid joysticks (%x)`, `joystick not found -- invalid joystick
  capabilities (%x)`, `joystick detected`, `<joy_name> configured` (when `joy_advanced` is set and `joy_name` is not
  `joystick`). Without any joystick driver nothing is printed.

### 10. Building the user command
Once per client frame (while connected) the client builds one user command (fields: SPEC-202 §6.1), stores it in its
64-entry command ring at the outgoing sequence number and as the "current command", and — when fully connected —
sends the move message of SPEC-202 §6. When merely connected, a packet is sent only if reliable data is pending or more
than 1000 ms passed since the last one (Quake II).

**A. Cinematic freeze.** While the player state's cinematic-freeze field is non-zero (SPEC-202 §4 field 27), steps B
and C are skipped (no movement, turning or mouse input; the command starts all zero) and, if a cinematic skip was
requested since the last command (§11), button bit 0x40 is set. Step D still runs.

**B. Keyboard movement** (the command starts all zero; the speed key multiplies only the turning speed; every
addition to or subtraction from a movement field is truncated toward zero immediately, one button at a time):
1. Turn speed T = frame time (s) × `cl_anglespeedkey` if `+speed` is down, else the frame time.
2. Pitch: pitch −= T × `cl_pitchspeed` × state(`+lookup`); pitch += T × `cl_pitchspeed` × state(`+lookdown`)
   (state = fraction of the frame the button was down, §7).
3. Left/right: skipped completely if `+use`, `+strafe` and (`+left` or `+right`) are all held (that combination
   leans instead). Otherwise, in strafe mode — `+strafe` down, or `lookstrafe` ≠ 0 and `+klook` down —
   sidemove += `cl_sidespeed` × state(`+right`), then −= `cl_sidespeed` × state(`+left`); else yaw −= T × `cl_yawspeed` × state(`+right`), yaw += T × `cl_yawspeed` × state(`+left`).
4. `+moveright` / `+moveleft` add / subtract `cl_sidespeed` × state — **only while `+use` is not down** (with `+use`
   down they lean instead).
5. `+moveup` / `+movedown` add / subtract `cl_upspeed` × state.
6. `+forward` / `+back`: if `+klook` is down they change pitch (−= / += T × `cl_pitchspeed` × state), else they add /
   subtract `cl_forwardspeed` × state to forwardmove.
7. **Run:** button bit 0x20 is set when (`+speed` down) XOR (integer part of `cl_run`) is 1. The movement values are
   **not** doubled (Quake II doubles them); the game applies running (SPEC-202 §6.1).
The view angles used here are the client's view angles; they persist across frames.

**C. Mouse and joystick** (only while the key focus is the game): §8, §9.

**D. Finishing:**
1. Bit 0x40 ("use/skip"): set when the player state's movement type is "dead" (3) and `+use` or `+attack` is down or
   went down this frame; or when a cinematic freeze is active and `+use` is down or went down this frame.
2. Only when the player state's remote-view type is 0 (none) or 1: bits 0x01 (`+attack`), 0x04 (`+altattack`), 0x08
   (`+weaponExtra1`), 0x10 (`+weaponExtra2`) are set when the button is down or went down this frame; and, when client
   weapon prediction is active (SPEC-003), the fire time / alternate fire time fields are set to the client's
   game-time estimate (SPEC-202 §5) if the predictor reports a primary / alternate attack this frame (otherwise 0).
3. Bit 0x02 (`+use`) when `+use` is down or went down this frame.
4. The "went down this frame" marks of attack, alt-attack, both extras and use are cleared.
5. Bit 0x80 when at least one key is held (§4 rule 5) and the key focus is the game.
6. msec = trunc(frame time (s) × 1000); a value above 250 becomes 100.
7. Pitch clamp as Quake II: with D = the player state's pitch delta angle in degrees (values above 180 taken minus
   360), view pitch is limited so that pitch + D stays within −89…+89.
8. angles = trunc(view angle × 65536 / 360), low 16 bits. If `cl_pitchdebug` ≠ 0 the line
   `cl.viewangles[PITCH]=<pitch>, delta_angles[PITCH]=<D>` is printed (both `%f`).
9. **Lean** (−1 right, 0 none, +1 left; later rules override earlier ones):
   a. If `+use` was down at any time this frame: lean = −1 when `+moveright` was down this frame, or "move right
      held" is marked (key held flag or joystick, §9), or `+right` and `+strafe` are both held; then lean = +1 when the
      same holds for `+moveleft` / "move left held" / (`+left` and `+strafe`).
   b. lean = −1 if `+leanright` was down at any time this frame.
   c. lean = +1 if `+leanleft` was down at any time this frame.
   So `+leanleft` wins over `+leanright`, and `+use` + a strafe key is an alternative way to lean.
10. lightlevel = the integer part of cvar `r_lightlevel` (written by the renderer; Quake II uses `cl_lightlevel`).
11. **Diagonal correction:** if both forwardmove and sidemove are non-zero, the pair (sidemove, forwardmove) is
    scaled to length M = max(|forwardmove|, |sidemove|): each becomes trunc(component / √(f² + s²) × M). Running
    diagonally is therefore no faster than running straight.
The sender then clamps the movement values (SPEC-202 §6.2).

### 11. Cinematic skip and "menu during cinematic"
While a cinematic freeze is active, opening a menu (e.g. `ESCAPE` → `menu main`, SPEC-801) and `toggleconsole` with
game key focus do not open anything; instead they request a cinematic skip, which sets button bit 0x40 in the next user
command (§10 A). The server reacts as described in SPEC-202 §6.1.

### 12. Force feedback (optional for OpenMerc)
Hardware: an Immersion "FEELit" force-feedback mouse or a DirectInput force-feedback joystick. Nothing in the game
depends on it.
- **Start-up** (at client start, preferring the mouse) prints `------- Force Feedback Initialization -------`
  (preceded by a newline and a bell character 0x07). If cvar `use_ff` (archived, default 1) is 0:
  `...inhibited, not initializing`. Else `Creating feedback device:`; a FEELit mouse is tried first, then a DirectInput
  force-feedback device; none → `...no feedback devices found`. FEELit mouse → `...found FEELit Mouse`.
  DirectInput device → `...found feedback device`, then a centring spring effect is created (`...device return spring
  ok` or `...(no device return spring)`) and set to tension 1000.
- Restart / shutdown prints `------- Force Feedback Shutdown -------` (newline and bell first), `...closing return
  spring`, `...closing feedback device`; shutdown happens at client shutdown.
- Commands: `ff_usemouse` — restart preferring the FEELit mouse; `ff_usejoy` — restart with a DirectInput device only;
  `ff_tension <0..3>` — spring strength 0 (spring off), 1000, 5000, 10000, printing `Setting tension %d`;
  `ff_spring <0..10000>` — spring strength directly, printing `Setting spring to %d`. Without a spring device both
  print `No spring device`; bad arguments print `ff_tension <tension [0..3]>` / `ff_spring <0..10000>`.
- **Playing effects.** Effects are triggered only by effect files' view-feedback elements (SPEC-400 type 4, force-
  feedback ids 1–32; the engine numbers them id − 1). Ids 10 (MachineGun), 20 (Laser2), 22 (Laser4) and 28 (GatlingGun)
  play only while the current user command has button bit 0x01 or 0x04 set (fire held); the others always play. Nothing
  plays when `use_ff` is 0 or there is no device.
- The engine keeps four effect slots. Playing effect E: if a slot already holds E, E is restarted; otherwise the first
  slot whose effect has finished playing is reused (its effect released), otherwise the first empty slot, otherwise
  slot 2 (of 1–4) is stopped, released and reused. An effect is played once per trigger.
- Releasing `+attack` or `+altattack` stops every playing effect.
- Effect strength is scaled by cvar `cl_ffmult` (archived, default 3.0). Each effect is one to three periodic or
  constant forces with fixed magnitudes, periods, durations, directions and envelopes; these per-effect parameters are
  not specified here (open question 1) — any rumble that matches the effect names of SPEC-400 is acceptable.

## Constants
| Name | Value |
|---|---|
| Key numbers | 0–255 (Windows virtual-key codes plus the engine numbers of §1) |
| Layout columns | 0 English (US), 1 German, 2 French, 3 British (= `sp_language`) |
| Console lines / line length | 32 lines, 255 characters + prompt `]` |
| Chat line length | 255 |
| Console scroll per PGUP/PGDN | 2 lines |
| Frame milliseconds clamp | 1 … 200 |
| msec field | trunc(frame time × 1000); > 250 → 100 |
| Pitch clamp | ±89° relative to the pitch delta angle |
| Angle conversion | 65536 / 360 |
| Default speeds | `cl_forwardspeed` 200, `cl_sidespeed` 160, `cl_upspeed` 200, `cl_yawspeed` 140, `cl_pitchspeed` 150, `cl_anglespeedkey` 1.5 |
| Mouse zoom factor | (F + 85) / 180 × F / 95, F = rendered horizontal FOV in degrees |
| Mouse defaults (engine) | `m_sensitivity_x` 6, `m_sensitivity_y` 6 (integers), `m_yaw` 0.022, `m_pitch` 0.022, `m_side` 1, `m_forward` 1, `m_filter` 0, `m_doublespeed` 200 ms (shipped config values: SPEC-302) |
| Double-click keys | `MOUSE`n`DBL` = 127 + n |
| Joystick axis value | (raw − 32768) / 32768 |
| Joystick keys | buttons 1–4 → 193–196; button n ≥ 5 → 223 + n; hat → 252–255 |
| Force-feedback slots | 4 |
| Force-feedback tension table | 0, 1000, 5000, 10000 (initial 1000) |
| Force-feedback continuous ids | 10, 20, 22, 28 (play only while bit 0x01 or 0x04 is set) |

## Test vectors / how to verify
1. **Diagonal correction.** Defaults, `cl_run 0`, hold `+forward` and `+moveright` for several frames, no mouse:
   every user command has forwardmove 156, sidemove 124, buttons 0x80 (keys held), lean 0. Holding only `+forward`:
   forwardmove 200, sidemove 0. With `cl_run 1`: same movement values and buttons 0xA0. Verify with a packet capture
   decoded per SPEC-202 §6.2 or with a server-side print.
2. **Lean.** Hold `+use` and `+moveright`: lean −1, sidemove 0, buttons include 0x02. Hold `+leanright` and
   `+leanleft` together: lean +1.
3. **Run with speed key.** `cl_run 1` and `+speed` held → bit 0x20 clear.
4. **Mouse.** `m_sensitivity_x 6`, `m_yaw 0.022`, `fov 95`, move the mouse 10 pixels right in one frame: yaw decreases
   by 6 × 10 × 0.022 = 1.32°. With a zoomed view at F = 20: z = 105/180 × 20/95 = 0.1228, mx = trunc(7.37) = 7,
   yaw decreases by 0.154°.
5. **Console key.** With `sp_language 1` (German) the key left of "1" (VK 220) opens the console and the US back-quote
   key (VK 192) types "ö"; with `sp_language 0`, Shift+back-quote does not open the console.
6. **Console availability.** Start without `+set console 1`: `toggleconsole` and the console key do nothing; with it
   they work.
7. **Localised key names.** In a menu with a `setkey` row for a command bound to `F1`, the key text is empty in the
   original (expected from the package contents); bound to `MOUSE1` with `sp_language 1` it reads `MAUS 1`.
8. **Unbind.** `bind x "echo hi"`, `unbind x`, `bind x` → prints `"x" = ""`; `bindlist` does not list `X`.
9. **Double click.** `bind mouse1dbl "echo dbl"`; two clicks within 200 ms print `dbl` once.
10. **Wheel in console.** With `bind mwheelup "echo up"`, scrolling the wheel while the console is open prints `up`.
11. **Cinematic skip.** In a level with an in-engine cinematic, press `ESCAPE` during the freeze: no menu opens, the
    cinematic is skipped (server sets `sv_jumpcinematic`, SPEC-202).
12. **Force feedback** (only with a device): an effect file with view-feedback id 10 triggers rumble only while fire is
    held; releasing fire stops all rumble.

## Open questions
1. Per-effect force-feedback parameters (waveform, magnitude, period, duration, direction, envelope for each of the 32
   ids) and the exact Immersion argument units — not extracted. Needed only for faithful force feedback.
2. Whether the held flag left set by an argument-less `-moveleft`/`-moveright`/`-left`/`-right`/`-use` typed at the
   console makes later `+use` presses lean (expected yes) — test in game.
3. After the console was open (mouse not read, pointer not recentred), whether the first game frame applies the
   pointer's accumulated offset as one large turn (expected: yes, up to half the window size) — test in windowed mode.
4. Remote-view type 1's meaning (weapon buttons are still sent for it) — see SPEC-202 field 20.

## Proposed amendments to approved specs (for G)
- **SPEC-400 Type 4 / Open questions:** force-feedback ids 10, 20, 22, 28 are played only while the current user
  command has button bit 0x01 or 0x04 set (resolves the UNVERIFIED "device options" statement); releasing `+attack` or
  `+altattack` stops all force-feedback effects.
- **SPEC-801 §7.6:** confirmed — joystick axes never move the menu pointer (joystick axes are read only while the key
  focus is the game). §7.2: the `MOUSE`n`DBL` events are generated for all key focuses, not only menus.
- **SPEC-802 `input`:** "keys that type characters" (click sound) = keys with flag `T` in §1 of this spec (includes
  `ESCAPE`, `PAUSE`, `BACKSPACE`, `TAB`, `ENTER`, arrows, `INS`, `DEL`); the pasted clipboard text is cut at its first
  CR, LF or backspace (0x08) character.
- **SPEC-302 cvar `console`:** meaning is "console allowed": when 0, `toggleconsole` does nothing (and the console is
  never drawn); it can only be set on the command line (`+set console 1`).
- **SPEC-202 §6.1:** no change needed; field derivations are in §10 of this spec (lightlevel from `r_lightlevel`).
