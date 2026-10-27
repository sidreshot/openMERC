# SPEC-130 — `.rmf` menu files

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — all 492 `menus/**/*.rmf` in `pak0` (408), `pak2` (50), `gs` (34). No game binary was consulted.
Implementation: `tools/formats/rmf.py` (uses `tools/formats/vfs.py`)
Related: SPEC-131 (string packages, `^REF^`), SPEC-132 (fonts), SPEC-133 (`.cfg` scripts the menus execute), SPEC-100 (`.m32` pictures)

## Purpose

Every menu screen of the game (main menu, options, multiplayer browser, chat, load/save, outfitting,
mission briefings, end-of-level stats, loading plaque) is described by `.rmf` text files under `menus/`.
A file is a small HTML-like markup: flowing text and widgets, attributes bound to console variables (cvars),
console commands run on clicks, conditional includes, and a frame system that splits the 640×480 menu
screen into rectangles each showing another `.rmf` page. Animated 3D backgrounds and buttons are Ghoul models
(`ghoul/menus/*.ghb`) with named bolts.

## Layout

### Encoding and lexical rules

- Plain text, CR LF line ends (489 files; 3 one-line files have no line end). Bytes are single-byte; one file
  contains bytes ≥ 0x80 (`fonts.rmf`, a font test page). Decode as Latin-1/cp1252 for Western data.
- No comment syntax and no escape character exist in the stock data.
- The file is a stream of four kinds of item:

| Item | Form | Notes |
|---|---|---|
| tag | `<name tok tok …>` | name is case-insensitive (`<HBR>` occurs once). Tokens are separated by whitespace (including newlines — 26 tags span several lines). A token is either `"quoted"` (no escapes; may contain `<`, `>`, `;`, newlines) or a bare run of non-space, non-`"`, non-`>` characters. |
| closing tag | `</stm>` | the only closing tag in the data |
| quoted text | `"…"` outside a tag | displayed text; spaces inside are kept (`": "`) |
| bare text | words outside a tag | displayed text, whitespace-separated words (e.g. `* For tech support and FAQ go to`) |

- Inside text and string arguments, `^NAME^` is a string-package reference (SPEC-131). `$cvar` and `#cvar`
  occur inside command strings and `<set>` values (see Semantics).

### File kinds

| Kind | Count | Form |
|---|---|---|
| document | 376 | `<stm [opts]>` … `</stm>`; `<stm>` is the first item |
| fragment | 116 | no `<stm>`: only included by other files (`tints`, `control`, `globe`, `fl_*`, `dm_game_*` …) |

One document has items after `</stm>` (`pak0/menus/m_scale.rmf`: a fog `<list>` whose string `^MENU_SCALE_FOG^`
does not exist anywhere). Whether the original reads past `</stm>` is UNKNOWN (open question 9).

### Tag grammar

`<name positional… option…>`. Positional arguments come first in a fixed order. Options are a keyword
followed by a fixed number of values (0 for flags). Options may appear in any order and may repeat
(`key`, `column`, `conv`, `command`). All stock keywords are lower case (the tool compares case-insensitively). Value types:
`num` = number literal (`-0.001`, `.5`, `320`), `int`, `tint` = name defined by `<tint>` or a literal
`0xAABBGGRR`, `cvar` = cvar name, `cmd` = console command string (`;`-separated), `menu` = menu name
(`menus/<name>.rmf`), others are plain strings.

The parser in `rmf.py` checks every tag against the table below; unknown tags, unknown options, missing values
and non-numeric numbers are errors. All 492 stock files pass.

#### Option keywords (value count and types)

| Keyword | Values | Keyword | Values |
|---|---|---|---|
| `key` | key, cmd | `ckey` | key, cvar, cmd, cmd |
| `tip` | str | `ifne` / `ifgt` | cvar, num |
| `cvar` / `cvari` / `cvar2` | cvar | `setimage` / `hostname` / `address` | cvar |
| `match` | str (comma list) | `bitmask` | int |
| `files` | dir, pattern, ext | `min` / `max` / `step` | num |
| `maxchars` | int | `width` / `height` / `xoff` / `yoff` / `scale` / `spacing` | num |
| `resize` | num, num | `rotate` | num, num, num |
| `border` | num, num, tint | `tint` / `atint` / `btint` / `ctint` / `dtint` / `backfill` / `bgcolor` / `tiptint` / `tipbtint` | tint |
| `type` / `tipfont` | font type | `atext` / `title` / `headings` | str |
| `column` | int, str | `conv` | str, str |
| `convbits` | str, str, str | `sort` / `align` | word |
| `bolt` / `bbolt` | bolt name | `bgbolt` / `gbolt` | bolt name, animation name **[hyp]** (`b_ring1`, `a_ring1`) |
| `time` / `speed` / `delay` | num | `end` | cmd |
| `page` | menu | `cpage` | cvar (holds a menu name) |
| `cut` | frame name | `tile` / `stretch` | picture |
| `command` | cmd | flags (no value) | `oneshot control global hidden history invisible noshade regular transient noscale saving tab private weapons items center nopush waitanim fragile` |

#### Tag inventory (all files, all paks)

Uses = occurrences; Files = files containing the tag. Option counts are occurrences of each keyword.

| Tag | Uses | Files | Positional args | Options seen (uses) |
|---|---|---|---|---|
| `hbr` | 2064 | 285 | — | — |
| `font` | 1468 | 363 | — | tint(1426), type(1405), atint(1377) |
| `br` | 729 | 36 | — | — |
| `set` | 379 | 73 | cvar value | — |
| `stm` | 376 | 376 | — | global(38), resize(30), nopush(12), command(7), waitanim(3), fragile(1) |
| `center` | 307 | 245 | — | — |
| `einclude` | 305 | 52 | cvar value menu | — |
| `strings` | 295 | 223 | package | — |
| `include` | 280 | 169 | menu | — |
| `text` | 266 | 130 | str | key(197), border(121), bbolt(39), yoff(18), bolt(9), xoff(7), regular(6), tint(4), tip(3), ifne(2), atint(1) |
| `frame` | 205 | 69 | name width height | page(114), cut(102), border(79), backfill(63), center(32), cpage(7) |
| `image` | 184 | 67 | picture | tint(150), atint(150), tip(144), key(136), bbolt(88), bolt(62), border(12), setimage(9), transient(2), xoff(2), yoff(2), noscale(2), width(1), height(1) |
| `vbar` | 182 | 182 | — | tint(182) |
| `ctext` | 147 | 46 | cvar | key(142), atext(98), ifgt(30), invisible(13), noshade(12), bbolt(9), width(3), yoff(2), ckey(1) |
| `normal` | 146 | 134 | — | — |
| `list` | 133 | 45 | labels | atext(125), cvar(111), match(44), key(33), cvari(22), bitmask(16), tip(8), files(4), width(3), ifne(1) |
| `backdrop` | 104 | 104 | — | bgcolor(61), tile(43), global(4), stretch(2) |
| `hr` | 87 | 59 | — | tint(87), width(86) |
| `setkey` | 57 | 1 | command | atext(57), tab(57), tip(2) |
| `tint` | 57 | 3 | name 0xAABBGGRR | — |
| `emote` | 56 | 1 | name format | — |
| `slider` | 56 | 14 | — | step(54), cvar(41), min(40), max(40), cvari(15), key(8) |
| `ghoul` | 50 | 19 | model | bgbolt(46), time(4), gbolt(3), xoff(2), yoff(2), width(1), height(1), oneshot(1), end(1), control(1) |
| `ticker` | 48 | 48 | str | bbolt(48), speed(48), width(48), xoff(48), yoff(48), delay(3) |
| `bghoul` | 44 | 43 | model | xoff(44), yoff(44), scale(44), time(38), oneshot(23), control(3), width(3), height(3), end(2) |
| `blank` | 40 | 20 | width height | bbolt(28), tip(20), key(17), ckey(11) |
| `input` | 40 | 27 | — | width(40), cvar(40), border(34), global(18), maxchars(10), hidden(8), bbolt(3), yoff(3), history(3), xoff(2), tip(1), btint(1) |
| `cinclude` | 39 | 7 | cvar menu | — |
| `key` | 37 | 31 | key cmd | — |
| `demote` | 34 | 1 | name | — |
| `left` | 26 | 22 | — | — |
| `cninclude` | 22 | 15 | cvar menu | — |
| `right` | 19 | 19 | — | — |
| `exinclude` | 18 | 16 | cvar menu-if-true menu-if-false | — |
| `cursor` | 13 | 13 | int | — |
| `cbuf` | 12 | 10 | cmd | — |
| `translate` | 12 | 1 | int | conv(13), convbits(6) |
| `config` | 8 | 8 | cfg | — |
| `semote` | 7 | 1 | event text | — |
| `tab` | 7 | 7 | num… (1–6 values) | — |
| `ikey` | 6 | 3 | bound-command cmd | — |
| `timeout` | 6 | 6 | seconds cmd | — |
| `exitcfg` | 3 | 3 | cfg | — |
| `includecvar` | 3 | 3 | cvar | — |
| `onexit` | 3 | 3 | cvar menu | — |
| `serverbox` | 3 | 3 | — | column(17), cvar(3), hostname(3), address(3), height(3) |
| `chat` | 2 | 2 | — | cvar(1), private(1) |
| `defaults` | 2 | 2 | — | tiptint(2), tipbtint(2), tipfont(2) |
| `dmnames` | 2 | 2 | str | — |
| `filebox` | 2 | 2 | dir pattern ext | sort(2), cvar(2), align(1), width(1), tint(1), btint(1), atint(1), key(1) |
| `gpm` | 2 | 2 | skin-cvar team-cvar | cvari(2), cvar2(2), bgbolt(2), scale(2), rotate(2) |
| `hbar` | 2 | 2 | frame-picture fill-picture | cvar(2), invisible(1) |
| `loadbox` | 2 | 2 | — | key(4), width(2), cvar(2), spacing(2), saving(1) |
| `selection` | 2 | 1 | picture width height | tint(2), ctint(2), dtint(2), bolt(2), xoff(2), yoff(2), weapons(1), items(1) |
| `users` | 2 | 2 | — | cvar(1), key(1), btint(1) |
| `alias` | 1 | 1 | name cmd | — |
| `autoscroll` | 1 | 1 | num | — |
| `bot` | 1 | 1 | cvar | tab(1), headings(1), align(1) |
| `botlist` | 1 | 1 | — | cvar(1) |
| `listfile` | 1 | 1 | cvar | align(1), cvar(1) |
| `players` | 1 | 1 | — | cvar(1), title(1) |
| `rooms` | 1 | 1 | — | cvar(1) |
| `serverdetail` | 1 | 1 | — | cvar(1), tab(1), title(1) |

Plus 1832 text runs (quoted or bare) outside tags.

Value vocabularies seen: key names `mouse1` (476), `mouse2` (76), `enter` (20), `n`, `y`, `any`, `space`,
`mouse1dbl`, `tab`, `c`; font types `title` (644), `small` (482), `medium` (278), `interface` (1);
`sort name`; `align center`.

## Semantics

Confidence markers: **[data]** = follows directly from the files; **[hyp]** = inferred, consistent with every
use, but not observed in the running game. Everything else that matters is in Open questions.

### Pak override

Several menus exist in more than one pak (gs and pak0: 27 files; pak2 and pak0: 8; all three: 2).
The newer copies are later revisions (gs = GameSpy browser/chat replacing WON; pak2 = Gold edition with
Control/Conquer modes and bots; pak3 = Platinum). **[data]** A loader must look a menu up by path through the
pak search order; the order of `gs`/`zmain`/`zsond` relative to `pak0`–`pak3` is UNKNOWN (open question 1).

### Document level

- `<stm>` options **[hyp]**: `resize W H` = the menu is a W×H window centred on the screen (equals the first
  frame's size in 29 of 30 files, e.g. `quit.rmf`: `resize 400 240` + `<frame main 400 240 …>`; exception
  `gs/private.rmf`: window 600×400, frame 600×256); `command "cmd"` = run
  when the menu opens (`gsc_getusers #menu_room`, `getgamestats`); `nopush` = do not put the menu on the
  return stack (used by the accomplishment/loading/login splash screens); `waitanim` = wait for the Ghoul
  intro animation; `global` (38 topic/strip pages) and `fragile` (1) UNKNOWN.
- `<strings pkg>` loads `strip/<pkg>.sp` **[data]** (112/112 exist). 1550 of the 2403 resolvable `^REF^`
  uses in menus (64.5 %, in 198 files) sit in a file that does not load the package itself, so packages loaded
  by a parent frame or an earlier menu stay visible **[hyp]** (or some packages are always loaded — UNKNOWN).
- `<include m>` inserts `menus/<m>.rmf` in place **[data]** (fragments are written to be inserted mid-flow).
  Conditional forms **[hyp, consistent with all names]**:
  `<cinclude cvar m>` if cvar ≠ 0; `<cninclude cvar m>` if cvar = 0;
  `<einclude cvar value m>` if cvar equals value (numbers `deathmatch 1..7`, strings `mapname arm1`);
  `<exinclude cvar a b>` a if cvar ≠ 0 else b (`low_violence b_main_lv b_main`);
  `<includecvar cvar>` includes the menu whose name is the cvar's value.
- `<set cvar value>` sets a cvar when the menu loads **[data]**; `$name` inside the value is replaced by that
  cvar's value (`<set menu_m_page m_language$sp_language>` → `m_language0..3`, all four files exist).
  This matches Quake II console macro expansion (`Cmd_MacroExpandString` in the GPL source: `$` followed by a
  token, anywhere in a line, not inside quotes) **[hyp: the tag is executed as a console `set`]**.
- `<tint name 0xAABBGGRR>` defines a colour name. Byte order **[data]**: `red 0xff0000ff`, `blue 0xffff0000`,
  `cyan 0xffffff00`, `yellow 0xff00ffff`, `orange 0xff2080ff` only make sense as A,B,G,R from the high byte;
  `smokey 0xbf000000` / `transblack 0x7f000000` are translucent black. `purple` is defined equal to `green`
  (data quirk). `tints.rmf` (included by 99 files) defines 28 names; `yellow` is defined locally in `newmotd.rmf`.
  All 25 tint names used anywhere are defined; 3 more uses are literals (`filebox … tint 0xffff0000`).
  `clear` (alpha 0) is used to suppress borders/backfills.
- `<defaults tiptint T tipbtint T tipfont F>`: tool-tip text colour, tool-tip box colour, tool-tip font **[hyp]**.
- `<alias name "cmd">`: defines a console alias (used as a command later: `checklogin`) **[data]**.
- `<cbuf "cmd">`: console command run when the menu loads **[hyp]**.
- `<config cfg>`: exec `<cfg>.cfg` when the menu opens; `<exitcfg cfg>`: exec `menus/<cfg>.cfg` when it
  closes **[hyp, strongly supported]**: `menus/video.cfg` copies real cvars into `menu_*` shadow cvars
  (`set menu_driver $gl_driver` …), `menus/applyskin.cfg` copies `menu_temp_*` back (`set skin #menu_temp_skin`).
- `<onexit cvar menu>`: on leaving, if cvar ≠ 0 open `menu` (`menu_settings_changed` → `altervid`/`altersnd`
  "apply your changes?") **[hyp]**.
- `<key K "cmd">` menu hot key (`any` = any key); `<ikey "+attack" "cmd">` runs cmd when the key bound to the
  given command is pressed; `<timeout 3.0 "cmd">` runs cmd after N seconds; `<cursor 0>` hides the mouse
  pointer; `<autoscroll 5>` scrolls the page by itself (credits) **[hyp]**.

### Frames (screen layout)

Frames split the virtual 640×480 menu screen (or the `<stm resize W H>` window) into named rectangles; `page`
loads a menu into a frame. Model **[hyp]** — implemented as `rmf.layout_frames()`; it lays out 66 of the 69
stock files with frames without conflict (see Validation):

- `<frame name W H [cut parent] …>` takes a W×H piece from the free area of `parent` (or of the screen when
  `cut` is absent). `W = 0` means the full free width — the piece is a strip cut from the **top**; `H = 0`
  means the full free height — a strip cut from the **left**; `0 0` takes everything left.
  Example `chat.rmf`: `main 640 372 border 40 …` leaves 560×292 inside; `users 128 0` (left column),
  `chat 0 272` (top strip) and `input 0 20` (next strip) add up to exactly 292 rows; `join.rmf`:
  `control 160`, `left 208`, `right 0` = 560 columns.
- `border P T tint`: P pixels of padding inside the frame, a T-pixel outline in `tint`
  (`border 24 3 cyan` marks the selected language flag, `border 24 1 gray` the others) **[data]**.
  `backfill tint` fills the frame.
- `page m` shows `menus/m.rmf` in the frame; `cpage cvar` shows the menu named by the cvar **[data]**.
  `center` UNKNOWN (centres the page content?).
- The console command `menu <page> <frame>` loads a page into a named frame of the current screen
  (`menu players/$menu_temp_skin text` → frame `text 270 116` of `player.rmf`) **[data]**.
- Not covered by the model: a piece with both sides non-zero that fills neither free side (`player.rmf`
  `text 270 116` in a 576×189 free area), requests larger than the free area (`gs/join.rmf` `logo 0 92` after
  `logopad 0 80` leaves 12 rows; `gs/private.rmf` `input 0 20` with 18 rows left — clipped? overlapping?), and
  whether top-level frames are stacked from the top or centred (`topic 0 92` + `main 640 372` leave 16 rows):
  UNKNOWN (open question 2).

### Flow layout inside a page

Content flows like HTML: text runs and widgets are placed left to right; `<br>` ends a line; `<hbr>` ends a
line with a smaller gap **[hyp]** (2064 uses, typically between list rows); `<hr width W tint T>` horizontal
rule; `<center>`, `<left>`, `<right>`, `<normal>` set the alignment of what follows **[hyp]** —
`<left>`…item…`<right>`…item… puts two items on the same row at the two edges (`a_quit`: No/Yes,
`m_language*`: US/UK flags); `<tab x1 x2 …>` sets column stops used by items with the `tab` flag
(`setkey … tab`, `serverdetail tab`, `bot tab`) **[hyp]**; `<vbar tint T>` adds a vertical scroll bar for
pages longer than their frame **[hyp]** (182 pages, always `vbargray`).
`<font type T tint A atint B>` sets the font (`small`/`medium`/`title`/`interface`, see SPEC-132), the normal
colour A and the colour B of the item under the mouse **[hyp: atint = "active"]**; attributes omitted keep
their previous value. `<backdrop tile|stretch "pic" bgcolor T [global]>`: page background, tiled or stretched
picture from `pics/menus/` over a solid colour.

### Widgets

| Tag | Function (all **[hyp]** unless noted) |
|---|---|
| `text "s"` | text item; with `key` it is a button; `border P T tint` draws a box around it; `regular`, `tip` |
| `ctext cvar` | shows the cvar's value, prefixed by `atext`; `invisible` (hide when empty?), `noshade` UNKNOWN |
| `image "pic"` | picture `pics/menus/<pic>.m32` **[data: 55/55 resolve there]**; `setimage cvar` = the cvar names the picture to show; `noscale`, `transient` UNKNOWN |
| `blank W H` | invisible W×H hot spot, normally placed on a Ghoul bolt (`bbolt`) over a 3D button |
| `list "a,b,c"` | cycles through comma-separated labels; stores the matching entry of `match "v1,v2,v3"` in `cvar` (empty entries allowed: `match ",defsnd.dll,…"` = autodetect); without `match` the stored value is the index 0,1,2… (`"16 Bit,8 Bit"` ↔ `s_loadas8bit 0/1`); `bitmask N` toggles bit N (0-based) of the cvar **[data: `fl_bullet` bitmask 13 = "Bullet only weapons", 14th flag in `dedicated.cfg`; `dmflags 4100` = bits 2+12 = "weapon stay and spinning weapons"]**; `files "dir" "pat" "ext"` lists files instead of labels (`maplists/*.lst` → value without extension, cf. `set sv_maplistfile "RavDM"`); `cvari` = integer cvar. Labels may be a `^REF^` whose text is the comma list (`^MENU_DM_GENERIC_NO_YES^` = "No,Yes") **[data]** |
| `slider` | horizontal slider on `cvar`/`cvari` between `min` and `max` in `step`s; default range when min/max are absent UNKNOWN |
| `input` | text field editing `cvar`; `width` px, `maxchars`, `hidden` (passwords), `history` (chat line history) |
| `setkey "+cmd"` | key-binding row: label `atext`, shows/changes the keys bound to the command (all 57 in `m_keys.rmf`) |
| `hbar "frame" "fill"` | progress bar from two pictures driven by `cvar` (`menu_loading`, `cl_download_percent`); value range UNKNOWN |
| `ticker "text"` | scrolling marquee of `width` px at `speed`, after `delay` s, placed on a bolt |
| `ghoul "model"` | Ghoul model `ghoul/menus/<model>.ghb` **[data: 41/41 exist]** attached by `bgbolt "bolt" "seq"` / `gbolt` to the background model; `time` start offset (s) |
| `bghoul "model"` | background Ghoul model centred at `xoff yoff` (always 320 224) scaled by `scale`; `time` = start time (s), `oneshot` = play once, `end "cmd"` = command when the animation ends, `control` UNKNOWN |
| `selection "pic" W H` | outfitting grid (`weapons` / `items`) with normal/`ctint`/`dtint` colours |
| `gpm skin team` | 3D preview of a player model (`.gpm`, `cvari` index, `cvar2` team index, `rotate` spin) |
| `filebox "dir" "pat" "ext"` | file chooser (maps, sprites); `key mouse1dbl` = double click |
| `loadbox` | save-game list (`saving` = save mode) |
| `listfile cvar` | lists the maps of the map list named by the cvar (SPEC-133 `.lst`) |
| `serverbox` | server browser; `column i "title"` maps server field i (0 host, 1 map, 2 players, 3 game type, 4 ping, 5 pure — from the column titles) |
| `serverdetail`, `players` | rules / players of the selected server; `<translate N conv "key" "label" convbits "cvar" "label" "bit names">` renames server rules for game type N; `<dmnames "Standard,Assassin,…">` names game types |
| `chat`, `users`, `rooms` | lobby chat (WON / GameSpy) widgets; `<emote cmd "format">` (`%n` = sender, `%p` = target), `<semote event "text">` system messages, `<demote name>` prints an emote's help line |
| `bot cvar`, `botlist` | bot table with `headings` (fields of the `.bpf` files, SPEC-133) |

Common item options: `key K "cmd"` runs cmd on that key/mouse button over the item;
`ckey K cvar "cmd1" "cmd2"` runs cmd1 when the cvar is 0 and cmd2 otherwise
**[hyp; supported by `ckey mouse1 menu_semaphore "set menu_semaphore 1;…" ""` (a guard that must run only
while the semaphore is 0) and by `menu_animated` choosing `main`/`amain`]**; `tip "s"` tool tip;
`ifne cvar v` / `ifgt cvar v` show the item only if cvar ≠ v / cvar > v (bot rows `bot_name5 ifgt
menu_maxclients 5`); `bolt`/`bbolt "name"` + `xoff`/`yoff` place the item at a bolt of the Ghoul background;
the difference between `bolt` and `bbolt` is UNKNOWN.

### Commands and cvars used by menus

- Commands are console text, split on `;` (Quake II command-buffer rules, SPEC-133). 68 distinct command words
  are executed from menus; 36 more appear only as the bound command of `setkey`/`ikey`:
  `add addfave bot_add bot_rem changepass checklogin checkpass connect disconnect exec freshgame gamemap
  getgamestats gsc_connect gsc_disconnect gsc_getchannels gsc_getusers gsc_join gsc_part gsc_private gsc_talk
  killmenu load loading maxfpsfromrate menu menuoff newgame newsave pingservers play popmenu quit refresh
  reloadall remfave return save saveconfig savesleft select set snd_restart sset stopsound turnoffmissionmsg
  tutorial unbindall updateinven updateinvfinal vid_restart wait wave weaponselect winstart wipe
  won_checklogin won_create_account won_create_room won_exit_room won_get_motd won_join_room won_list_rooms
  won_login won_markconnection won_set_key won_talk zero`.
  Menu-specific meanings **[hyp]**: `menu m [frame]` open/replace; `popmenu` back one level; `killmenu` close
  the current window; `menuoff` close all menus; `sset cvar ^REF^` set a cvar to a localised string;
  `zero cvar` clear a cvar; `add cvar n` add to a cvar.
- `$name` = cvar value, expanded when the command executes (Quake II rule) — 51 distinct names.
  `#name` (9 names: `menu_room`, `won_username`, `menu_chat` …, also in `.cfg`: `set skin #menu_temp_skin`)
  is a second substitution used exactly where the value may contain spaces (chat lines, passwords, names)
  **[hyp: `#` = value inserted as one quoted argument]**.
- 291 distinct cvars are bound to widgets. Menus edit copies named `menu_*` and apply them via `.cfg` scripts.

### Asset references (resolution rules used by the tool)

| Reference | Resolves to | Stock result |
|---|---|---|
| `include`/`*include`/`page`/`onexit`/command `menu X` | `menus/X.rmf` (case-insensitive; subdirectories allowed: `maps/arm1subject`, `players/hawk`) | 273 found; **2 missing**: `b_gr` (pak2 `m_start`), `gr` (gs `m_advjoin`) — global-rankings pages never shipped; 3 built from cvars (`$menu_menu`, `players/$skin` …) |
| `strings X` | `strip/X.sp` | 112/112 |
| `config X` / `exitcfg X` / command `exec X` | `X.cfg` / `menus/X.cfg` / `X` | 16/16 (+1 from a cvar) |
| `image` / `backdrop tile|stretch` / `hbar` / `selection` | `pics/menus/X.m32` | 55/55 |
| `ghoul` / `bghoul` | `ghoul/menus/X.ghb` | 41/41 |
| command `play X` | `sound/X` | 2/2 |
| `files` / `filebox` directory | directory in the paks | 4/4 |
| `^REF^` | string package entry (SPEC-131) | 2403/2409 (6 never defined) |
| bolt names (66 distinct), animation names (30) | bolts / sequences inside the Ghoul models | not checked (needs the `.ghb` spec) |

## Validation

`python tools/formats/rmf.py validate --out <dir>` (stock paks, default order of `vfs.py`):

- **492 files parsed, 0 errors** under the strict schema (62 tags, 0 unknown tags/options). 376 documents,
  116 fragments, 1 file with content after `</stm>`.
- Negative tests (unknown tag, unknown option, missing option value, non-numeric size, unterminated quote/tag,
  missing `</stm>`) are rejected.
- Reference check as in the table above; tint names: 25/25 defined (+3 literals).
- Frame model: 69 files with frames; 3 with issues under the hypothesis above (`gs/join.rmf` overflow,
  `gs/private.rmf` overflow, `player.rmf` ambiguous). Popup windows are laid out inside their `resize` size.
- Output: JSON tree per file in `openmerc-data/ui/rmf/json/<pak>/menus/…json`, inventory and reference report in
  `openmerc-data/ui/rmf/rmf_inventory.json`, console report in `openmerc-data/ui/rmf_validation.txt`.

## Open questions (with black-box checks)

1. **Pak search order** for `gs`, `zmain`, `zsond` vs `pak0`–`pak3`. Check: start the original; the main menu
   ticker shows pak3's "PLATINUM EDITION" text, pak2's "GOLD EDITION", or zmain's Russian text; the
   multiplayer *Join* page shows a "Pure" column only if gs/pak2's `m_join.rmf` wins over pak0's.
2. **Frame placement** when both W and H are non-zero (`topic 480 92 cut topicarea`, `text 270 116`) and for
   top-level frames (`topic 0 92` + `main 640 372` leave 16 rows unused: top-aligned or centred?).
   Check: screenshot `menu join`, `menu player`, `menu chat` at 640×480 and measure the rectangles.
3. **`<br>` vs `<hbr>` line heights**, `<normal>` vs `<left>`. Check: screenshot `menu fonts` (the font test
   page) and `menu video`; measure line pitch per font.
4. `ckey` order (cmd1 when cvar = 0?). Check: in the player setup, click a team arrow twice quickly; and
   `set menu_animated 0`, open a sub-page, click the home icon → which main page opens.
5. `#cvar` vs `$cvar`. Check: `set menu_chat "a b"` then run a command bound via `#menu_chat` with
   `echo`-like effect (e.g. `alias t "echo #menu_chat"` from a menu `<key>`), compare console output.
6. Flags `global` (stm, input, backdrop), `fragile`, `control`, `regular`, `noshade`, `invisible`,
   `transient`, `noscale`, frame `center`; `bolt` vs `bbolt`. Check: compare screenshots of pages that differ
   only by the flag (e.g. `m_game` input with `global` vs `m_advjoin` without).
7. `slider` default range; `hbar` value range; `ticker` `speed` units; `autoscroll` units. Check: set the
   cvar to known values and screenshot; time the ticker.
8. Which duplicate `REFERENCE` wins inside a package (see SPEC-131) — visible as the title of `menu quit`.
9. Whether content after `</stm>` is read: open `menu scale` and look for a "Fog" row.
10. Exact `stm resize` window placement and the meaning of `nopush`/`waitanim`.
