# SPEC-803: Menu widgets with engine-side data (models, lobby, server browser, file and save lists, bots)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (menu system)
Depends on: SPEC-800 (menu runtime core: pages, frames, flow placement, screen scale, fonts, colours, common item
options, key commands, menu command queue, tab stops), SPEC-002 (renderer 2D calls), SPEC-010 / SPEC-011 (Ghoul
interfaces), SPEC-130 (menu file data), SPEC-131 (string packages), SPEC-133 (`.lst`, `.bpf`, `botlist.txt`),
SPEC-201 / SPEC-203 (server query replies), SPEC-300 (file system), SPEC-302 (cvars and commands), SPEC-303 (savegames)

## Purpose

SPEC-130 lists the menu tags found in the data; SPEC-800 specifies how a page is parsed, laid out and drawn and the
options every item accepts. This spec covers the tags whose content comes from the engine rather than from the menu
file: 3D Ghoul models (`ghoul`, `bghoul`, `gpm`), the lobby chat widgets (`chat`, `users`, `rooms`), the server
browser (`serverbox`, `serverdetail`, `players`, `translate`), file and savegame choosers (`filebox`, `filereq`,
`loadbox`), the map-list and bot widgets (`listfile`, `botlist`, `bot`). It resolves the SPEC-130 items marked [hyp]
or UNKNOWN for these tags.

Conventions used below:
- *s* = the screen scale of SPEC-800 (video width / 640, or 1.0 inside a `<stm resize>` window).
- *L* = the line height of the widget's font. Every widget in this spec takes the page's current font (and its `tint`
  / `atint` colours) at the moment its tag is read. With the stock fonts L = 10 (`small`), 12 (`medium`), 18
  (`title`) (SPEC-800).
- "Text is drawn" = one call of the proportional-font text routine (SPEC-002 slot 0xc8) with the widget's font,
  the stated colour and the "keep colour" argument 0 unless stated otherwise.
- "Tint", "atint", "btint", "ctint", "dtint" = the widget colours of SPEC-800 (atint = colour used while the item or row
  is under the mouse).
- "The widget's cvar" = the cvar named by the common `cvar`/`cvari` option (SPEC-800) or by the positional argument
  stated for the tag.
- "Key commands" = the commands attached with the common `key`/`ckey`/`ikey` options. When they run, each matching
  entry plays `menu/small/select.wav` and queues its command (SPEC-800 menu command queue).
- Coordinates are screen pixels; "x, y" of a widget = its top-left inside its border (SPEC-800).
- A *variable* widget is one after which the page accepts no further placed items; later placed tags print
  `No rects allowed after a variable one` (SPEC-800).

## Interface / layout

### Tags and their own options

Common item options (SPEC-800) are accepted by every tag below unless stated. An option that is neither the tag's own
nor common prints `Unrecognised <tag> keyword` (developer print, SPEC-800) and ends the tag.

| Tag | Positional arguments | Own options | Variable | Default size |
|---|---|---|---|---|
| `ghoul` | model | `yaw` C, `pitch` C, `roll` C, `scale` F, `oneshot`, `control`, `time` F, `animate`, `rotate` a b c, `rotation` a b c, `mousex` C f, `mousey` C f, `gbolt` A B, `bgbolt` A B, `end` "cmd", `cvar2` C | no | from the model's bounds |
| `bghoul` | model | same as `ghoul` | — (not placed) | — |
| `gpm` | skin-cvar [team-cvar] | same as `ghoul` (in practice `cvari`, `cvar2`, `bgbolt`, `scale`, `rotate`) | no | — |
| `chat` | — | `private` | yes | rest of the frame |
| `users` | — | — | yes | rest of the frame |
| `rooms` | — | — | yes | rest of the frame |
| `serverbox` | — | `column` i "title", `hostname` C, `address` C | yes | width = tab stop *n*, height 200 |
| `serverdetail` | — | `title` "a,b,…" | no | frame width, 10 L high |
| `players` | — | `title` "a,b,…" | yes | frame width, 8 L high |
| `translate` | game-type N | `conv` key "label", `convbits` key "label" "names" (repeatable) | — (not placed) | — |
| `filebox` | dir pattern ext | `backfill` T, `sort` K, `talign` A | yes | 16 L wide, n·L high |
| `filereq` | dir pattern ext | `backfill` T, `sort` K, `talign` A | no | 16 L wide, n·L high |
| `loadbox` | — | `backfill` T, `sort` K, `spacing` N, `saving` | no | 128·s wide (or `width`·s) |
| `listfile` | cvar | — | yes | frame width, n·L high |
| `botlist` | — | — | yes | frame width, n·L high |
| `bot` | cvar | `headings` "a,b,…" | yes | frame width, (number of headings)·L high |

"Frame width" = the frame's inner width minus the width of the page's scroll bar if it has one. "Rest of the frame" =
inner width minus scroll bar, inner height. An explicit `width`/`height` overrides these defaults (a default height
is applied only when it is larger than the item's minimum height, SPEC-800).

The stock data also contains the tags `emote`, `semote`, `demote` and `dmnames` (SPEC-130). **SoF.exe 1.07f has no
handler for them**: each is an unknown tag (developer print `Unknown keyword`, the rest of the tag is skipped) and has no
effect. A reimplementation should accept and ignore them.

### Server list record

The engine keeps one list of servers for all browser widgets. Each record holds:

| Field | Source / meaning |
|---|---|
| address | the reply's network address (records are matched by full address) |
| host name | text |
| map | text |
| players text | text `a/b/c`; number of players = a; maximum = the number after the second `/` (0 when there is no second `/`) |
| type | the "game" field when it is not empty (a mod directory), otherwise the "game type" field |
| flags | integer; bit 5 (value 32) = pure server |
| ping text | `local` for LAN replies until a ping answer arrives, then the measured milliseconds as decimal text; for master-server entries the ping reported by the master list |
| ping | integer milliseconds (0 until known) |
| status text | the last `print` reply received from that address (`serverstatus`) |

Records are created from a `+`-separated text with these fields (leading spaces of the text are skipped; a field ends
at the next `+`, so a `+` inside a host name shifts the following fields):

`host + map + players text + game + game type + flags + ping text`

- LAN servers: the server's `info` reply (SPEC-201: `SOF+…`), with `SOF+` removed, the final newline removed and
  `+local+` appended. Its players text is `players/spectators/maxclients` and its flags field is the server's
  `sv_violence` value.
- Master-server servers (GameSpy list): `<hostname>+<mapname>+<numplayers>/<numbots>/<maxplayers>++<gametype>+<violence>+<ping>+`
  built from the server's query keys (default values when a key is missing: hostname `localhost`, numbers `0`,
  gametype `DM`, violence `0`).

## Behaviour

### 1. Ghoul models in menus (`ghoul`, `bghoul`)

**Loading.** `<ghoul "name" …>` creates a new model object on the engine's menu Ghoul interface (SPEC-010 system
method 0), registers the sequence `ghoul/menus/<name>.ghl` on it (object method 0), finishes registration with all
skins (object method 1 with true), prepares it for drawing (object method 2 with true) and creates one instance
(object method 24). If any step fails the tag prints `Invalid ghoul model` (developer print) and is dropped.

**Placement.**
- `<ghoul>` is a placed item (SPEC-800 flow) unless it is bolted (below). When placed successfully it becomes the
  page's *current model*.
- `<bghoul>` is not placed. It belongs to the page background (the `backdrop` object of SPEC-800, created with no
  picture if the page has none). Its item position is (0, 0) inside the frame, so its screen point is the frame's inner
  top-left plus `xoff`, `yoff`. It also becomes the page's current model. Background models are drawn after the
  backdrop colour and picture, in tag order, before the page's items.
- The common option `bolt NAME` attaches an item to a bolt of the page's current model; `bbolt NAME` attaches it to a
  bolt of the page's **first** `<bghoul>` (this is the difference between the two, SPEC-130 open question).
  Errors: `No ghoul model to bolt to`, `Could not find part to bolt to`.

**Size and origin.** Before options are read the model's scale is *s*. Option values are applied in tag order:
- `xoff`/`yoff` (common options) are multiplied by the scale current at that moment; stock menus write them before
  `scale`, so they are multiplied by *s* only.
- `scale F` sets the scale to F·*s* (to F alone for a `gbolt`/`bgbolt` model).
- After the tag the model is posed at time 0 of its sequence and its bounding box is taken (SPEC-011 instance method 51,
  all parts, with each axis that has a non-zero `rotate` rate turned by π/4 to allow for the spin). If `xoff` is 0 it
  becomes −mins.x · scale; if `yoff` is 0 it becomes −mins.y · scale; if width is 0 it becomes (maxs.x − mins.x); if
  height is 0 it becomes (maxs.y − mins.y) (all truncated to integers). So by default the model's box fills the item
  rectangle with its minimum corner at the item's top-left.
- The model's screen point (its origin) = the item's screen x, y plus `xoff`, `yoff`.

**Transform.** Every frame the instance transform is set to a rotation built from three angles in radians, then scaled
uniformly by the model scale:
- angle 1 = `rotation` a + `rotate` a · T + value of the `roll` cvar,
- angle 2 = `rotation` b + `rotate` b · T + value of the `yaw` cvar,
- angle 3 = `rotation` c + `rotate` c · T + value of the `pitch` cvar,

where T = the engine's millisecond clock (time since start, not since the menu opened), so `rotate` rates are radians
per millisecond (stock `rotate 0 0 -0.001` = −1 rad/s about the third axis). The three cvars' numeric values are
reduced modulo 2π in place (their text is not changed). Angle 1/2/3 rotate about the model's first/second/third axis;
the composition order is UNVERIFIED.

**Animation.**
- Default: the sequence loops (SPEC-011 playback mode 0, restart) from the moment the item is first drawn.
- `oneshot`: played once and held on the last pose (mode 1).
- `time F`: shown as a static pose at offset F seconds (mode 3), no playback.
- `animate`: starts as a static pose at offset `time` (default 0); while the mouse is over the item the sequence plays
  forward once (mode 1, phase-matched); when the mouse leaves it plays backward once to the start. (Not used by the
  stock menus; their button models use `time 0.0`, a static pose.)
- Events (SPEC-011 instance method 45) are processed while drawing, using the menu clock, except while the current
  menu screen is in its "waiting for an animation" state (SPEC-800 `<stm waitanim>`).
- Note `EOS` (end of sequence) is required: a model without it prints `Could not find EOS note token`. On `EOS`:
  if the model has `control`, the screen's waiting state is ended (SPEC-800 `waitanim`); if it has `end "cmd"`, the
  command is queued (`cmd` followed by a newline). Example: `intro … oneshot end "menu amain" control` opens the main
  menu when the intro animation finishes.
- Note `sound`: every argument of every `sound` note in the model's sequences is registered as `<arg>.wav` when the model
  loads; when such a note fires, `<arg>.wav` is played as a local (2D) sound.

**Bolting one model to another.** `gbolt A B` attaches this model to the page's current model, `bgbolt A B` to the
page's first `<bghoul>`. A = name of the bolt (part) on the parent model, B = name of a part of **this** model that is
placed on it (SPEC-011 instance method 28 with my-bolt = A, child-bolt = B). This resolves SPEC-130's hypothesis: the
second name (`a_ring1`, `A_HOME_PAGE`) is a part of the child model, not an animation. Errors: `Could not owning ghoul
model` (no parent), `Could not find bolt <A>`, `Could not find nut <B>`. A bolted model is drawn by its parent's
rendering (as a bolt-on), never on its own; its hit rectangle follows the projected bolt position (below), so a bolted
model with `key` or `animate` works as a 3D button. `gbolt`/`bgbolt` reset the model scale to 1.0; a later `scale F` sets
it to F (not multiplied by *s*).

**Bolt projection** (items attached with `bolt`/`bbolt` and models attached with `gbolt`/`bgbolt`). The parent's bolt
matrix is taken through all parents (SPEC-011 instance method 50, matrix mode 5). Its translation (x, y, z) gives the
item's screen point = parent model's screen point + (x, y) (+ border widths), then the item's `xoff`, `yoff` are added
— multiplied by *s* for model items, unscaled for other items. The projection also reports whether the normalised
direction of (x, y, z) has z < 0.05 ("the bolt faces the viewer"). A model attached with `bolt`/`bbolt` is rendered only
while its bolt faces the viewer; the item's hit rectangle follows the projected position every frame regardless (a
`blank` on a bolt ignores the facing result and stays clickable; other widgets: SPEC-802).

**Mouse rotation.** `mousex C f` / `mousey C f` make the model interactive: while mouse button 1 is held over it,
cvar C is increased by f × (horizontal / vertical mouse movement in pixels since the button went down or since the
previous frame). Combine with `yaw C` etc. to spin a model by dragging. `cvar2 C` stores a second cvar (used by
`gpm`). Common `inc`/`mod` options also work on a model (SPEC-800).

**Rendering.** For each drawn model:
1. The renderer's menu-model mode is entered (SPEC-002 slot 0xb8) with argument true when the model has a part named
   `forced_camera`, false otherwise.
2. Instance method 41 (SPEC-011) is called with a render-parameter block: time = menu clock; light colour (0, 0, 0);
   directional vector (x + 50, y + 50, 200) where (x, y) is the screen point; entity origin (0, 0, 0); option bytes
   all 0 except "specular" = 1 and "no texture stack" = (`ghl_no_texture_stack` > 0.5); fog visibility 1.0; shadow
   mode 0; maximum lights 0; view forward (0, 0, 1).
   - Without `forced_camera`: entity→world matrix = translation (x, y, 0); view origin (x, y, −200).
   - With `forced_camera`: entity→world matrix = that part's bolt matrix (instance method 50, matrix mode 7, through
     parents); view origin (0, 0, 0).
3. The 2D state is restored (SPEC-002 slot 0xb4) with the same true/false argument.
4. If the item has a `border`, it is drawn after the model.
The exact projection the renderer applies in menu-model mode (how model units map to pixels) is UNVERIFIED; the
bounds rule above implies 1 model unit ≈ scale pixels.

### 2. Player model preview (`gpm`)

`<gpm skin-cvar team-cvar …>` shows one player model chosen from all `ghoul/pmodels/*.gpm` files (SPEC-010
player-model description). With a single positional argument (`<gpm name>`) only `ghoul/pmodels/<name>.gpm` is offered
and no skin/team cvars are written.

**Catalogue.** The `.gpm` files of all search paths are listed (order = file-system listing order, UNVERIFIED), each is
loaded; files that fail to load are skipped. Each model's bolt-on models are loaded too. A *team list* is built from
the models' team names (player-model method 12) in order of first appearance; each team remembers the index of its
first model. Model index = position in the catalogue (0-based); team index = position in the team list.

**Cvars.** `cvari C` = model index cvar (stock `menu_gpm`), `cvar2 C` = team index cvar (stock `menu_team`), positional
skin cvar (stock `menu_temp_skin`) and team cvar (stock `menu_temp_team`).

**Every frame**, before drawing:
1. Team index t = value of `cvar2`, wrapped into [0, teams−1] (−1 → last, beyond the end → 0). If t differs from the
   team shown: the model index cvar is set to that team's first model and the model is rebuilt.
2. Model index m = value of the `cvari` cvar, wrapped the same way. If m differs from the model shown, m is moved in the
   direction of the change (±1, wrapping) until a model whose team equals the team cvar's text is found; the `cvari`
   cvar is set to it and the model is rebuilt.
3. `menu_semaphore` is set to 0 (this releases the click guard used by the stock arrow buttons, SPEC-130
   `ckey … menu_semaphore …`).
4. The model is drawn as in section 1.

**Rebuilding** the model: the first rebuild after the page opens (and after a page refresh) first looks up the model
whose file name (without `ghoul/pmodels/` and `.gpm`) equals the skin cvar and makes it current. Then: a new instance of
the model with its menu sequence playing, its skins applied per part, its bolt-on models attached to their bolts (scaled
as the description says) and its part on/off flags applied; skin cvar := the model's file base name; team cvar := its
team name; `cvar2` := its team index; the cvar named `<cvar2 name>_image` (stock `menu_team_image`) := `teamicons/<team>`.
The model is then re-attached to its `bgbolt` parent.

### 3. Lobby widgets (`chat`, `users`, `rooms`)

The engine keeps, while menus exist: a public chat log and a private chat log (each at most 100 messages; a new
message is added at the front and the oldest are dropped), a sorted room list and a sorted user list (byte-wise
ascending order of the whole entry text), and the name of the selected user. They are filled by the GameSpy chat
commands (`gsc_*`, SPEC-302); room names are channel names without their first character (channels starting with
`*` are not listed); user entries are a colour code, a mode character and the nick: `\x01@nick` (operator),
`\x04+nick` (voice), `\x1c nick` (normal) — so operators sort first. Chat lines start with a colour code (SPEC-002
colour table).

**`<chat>`** shows the public log; with `private` it shows the private log, and creating it clears the private log and
adds `\x04Started private chat with <selected user>`. Only one chat widget per page: a second one prints
`Error : Only one chat allowed per page` (always printed).
- Drawing: messages from newest to oldest are laid out bottom-up starting at y + height − 4. Each message is word-
  wrapped to the widget width: words are separated by space, tab, CR, LF; a word is added to the current line while
  (line width + word width + space width) < widget width, otherwise a new line starts with that word (the width of the
  new line then counts the word plus two spaces). A first word wider than the widget produces an empty line before it.
  Lines are drawn top to bottom inside each message, clipped on the right at the frame's inner right edge minus the
  scroll bar, with colour argument white and "keep colour" 1 (so a line continues the colour of the previous line
  unless it starts with a colour code). Drawing stops when the next message would start above the widget.
- When the total number of lines changes, the page's content height becomes lines·L and the page scrolls to the bottom.

**`<users>`** draws one entry per line (the full entry including its colour code and mode character) in tint. When the
number of entries changes the page's content height becomes entries·L (no automatic scrolling). Mouse button 1 on a
line: the widget's cvar and the selected user := the entry without its first two characters; then key commands run
for any button (stock: `key mouse1 "menu private"`).

**`<rooms>`** draws one room per line, the line under the mouse in atint, others in tint; content height as for users.
Mouse button 1 on a line sets the widget's cvar to the room name. No key commands and no sound are processed by this
widget.

### 4. Server browser

**List maintenance.**
- `pingservers` (SPEC-302) clears the list (and sets `menu_server` to −1) before sending its queries.
- Each LAN `info` reply and each master-list entry is added or, if a record with the same address exists, replaces that
  record's fields.
- A new server whose flags value does not contain all bits of the client's `cl_violence` (flags AND cl_violence ≠
  cl_violence) is **not listed** but counted as locked.
- After each addition the cvars `player_summary` = `<total players>/<total maximum>` (text `%d/%d`),
  `server_summary` = number of listed servers, `locked_summary` = number of locked servers are set. Clearing sets them
  to `0/0`, `0`, `0`.
- A ping answer (`ack`, SPEC-201) from a listed address sets its ping and ping text; a `print` reply from a listed address
  stores the text as the record's status text and sets cvar `serverinfo` to it.
- While the master list is being fetched the browser's host-name cvar `menu_servername` shows `Querying GameSpy
  Master`, `Transferring Server List`, then `Query Complete`.

**`<serverbox column i "title" … cvar C hostname H address A height h>`**
- Columns are shown in the order of the `column` options. `i` (clamped to 0–5) selects the field: 0 host name, 1 map,
  2 players text, 3 type, 4 ping text, 5 pure (`^MENU_DM_GENERIC_PURE_YES^` "Yes" when flags bit 5 is set, else
  `^MENU_DM_GENERIC_PURE_NO^` "No"). Display column j starts at the page's tab stop j (SPEC-800; tab stop 0 = the item's
  left edge) and is clipped at tab stop j+1. The widget width = tab stop *n* (n = number of columns); default height
  200.
- When the page is built and the list is empty, `pingservers` is queued (forced).
- Drawing, top to bottom: the column titles in white on line 0; a 1-pixel line in colour (128,128,128,255) from x to
  x + width at y + 1.5·L (SPEC-002 slot 0x7c); then one server per line from y + 2·L, in tint, the line under the mouse
  in atint. The page's content height = (servers + 2)·L, updated when the count changes.
- If the widget's cvar equals −1 when drawing: the host-name cvar is set to the value of cvar `menu_nss`, the address
  cvar to "" and the widget's cvar to −2 ("no selection").
- Mouse: line index = (mouse y within the widget) / L − 2.
  - Index −2 (title line) with button 1: the column under the mouse is sorted: ascending on the first click of that
    column, then alternating descending/ascending; the widget's cvar is set to −1 and `menu/small/select.wav` plays.
    Sort keys: host name, map and type compare case-insensitively; players compare the player count; ping compares the
    ping number. Equal keys keep no particular order. The sort direction is remembered per column for the whole run.
    Clicking a title of a "pure" column uses an undefined sort key (UNVERIFIED; the original reads past its table).
  - A server line with button 1 or 2: host-name cvar := host name, address cvar := the address as text, the command
    `serverstatus <address>` is queued, the widget's cvar := the line index, `menu/small/select.wav` plays.
  - Then key commands run (the stock join pages attach none to the box itself).
- The selected server is not highlighted (only the line under the mouse is).

**`<translate N conv "key" "label" convbits "key" "label" "name0,name1,…" …>`** defines, for the current menu screen,
how server rules are shown by `serverdetail`. N = game type (`deathmatch` value) the entries apply to; 0 = all game
types. Entries keep their declaration order. The bit-name list is split at commas (at most 255 characters).

**`<serverdetail cvar C title "Key,Value" tab>`** shows the rules of the server whose list index is the widget's cvar
(stock `menu_server`). Its rows are rebuilt from the record's status text when the index changes (and every frame while
that produces no rows, i.e. until the status reply has arrived). The status text starts with the server's info string
(SPEC-203): `deathmatch` is read from it, then for every translate entry whose N is 0 or equals that value:
- `conv key "label"`: if the key's value is not empty → row (label, value);
- `convbits key "label" "names"`: the value is read as an integer; for each set bit i (from 0) whose name i exists and is
  not empty → row (label, name i) for the first such bit and ("", name i) for the next ones.
Drawing: the comma-separated titles on line 0 at tab stops 0, 1, …; the grey line at y + 1.5·L as for the server box;
rows from y + 2·L, two cells per row at tab stops 0 and 1 (clipped at the next stop), colour tint (atint while the mouse
is over the widget). Content height = (rows + 2)·L.

**`<players cvar C title "Name,Frag,Ping">`** shows the players of the selected server from its status text: every line
after the first newline is read as three tokens (Quake II token rules): frags, ping, quoted name; lines with an empty
name are skipped; status order is kept. Cells: name, frags, ping (decimal) at tab stops 0, 1, 2. Layout and colours as
for `serverdetail`; content height (players + 2)·L.

### 5. File and savegame choosers

**`<filebox "dir" "pattern" "ext" …>`** lists the files matching `dir/pattern.ext` in **all** search paths (SPEC-300;
directories are not listed; a name present in several search paths may appear more than once, UNVERIFIED). The text
shown and stored is the path relative to `dir/`, with `.ext` removed when `ext` contains no wildcard (stock
`filebox "maps" "dm/*" "bsp"` → `dm/nycdm1`). Names from archives may be upper case (SPEC-300, UNVERIFIED for display).
- Sort keys (`sort`): `name` (byte-wise ascending, case-sensitive), `rname` (descending), `size`, `rsize`, `time`,
  `rtime` (by file size / modification time, ascending / descending; archive entries have time 0). Without `sort`, or
  with another word, the order is unspecified (UNVERIFIED; the original sorts with a comparator that always answers
  "less").
- `backfill T` = background colour; default = the page backdrop colour, or transparent black if the page has none.
- `talign left|right|center` is accepted (`Invalid align parameter` → left) but has no effect; the text alignment
  inside the box is the item's `align` (SPEC-800): left, centred ((width − text width)/2) or right.
- Drawing: the whole box is filled with the backfill colour (SPEC-002 slot 0x74); one file per line; the line under the
  mouse gets a bar of height L filled with btint and its text in atint; other lines in tint.
- Mouse button 1 on a line: widget cvar := the name, `menu/small/select.wav`; then key commands (stock `fxfile`:
  `key mouse1dbl "menuoff"`).

**`<filereq "dir" "pattern" "ext" …>`** (not used by the stock menus) is a file box with directory navigation over the
first search path only: when inside a sub-directory the first line is `..`; then the sub-directories (colours ctint /
dtint, both default (255,0,255,255) magenta), then the files (tint/atint). Button 1 on `..` goes up one directory level,
on a directory enters it; on a file sets the widget's cvar to its name relative to `dir` and runs the key commands for
button 1. Exact naming rules UNVERIFIED.

**`<loadbox …>`** shows the savegames (SPEC-303) as a grid of thumbnails.
- When the widget is created the savegame `current` is wiped and `save/*.sof` is listed in the **first search path
  only** (the user directory). Saves that cannot be opened are dropped; with `saving`, the automatic save `auto` is
  dropped too (it cannot be overwritten). `saving` also requires that saving is currently possible; otherwise the whole
  page fails to open (SPEC-800).
- For each save: its comment `<time>/<skill>/<level>` and its 128×128 thumbnail (registered as a picture named after
  the save, SPEC-303) are loaded. Default sort `rtime` (newest first); `sort` as for `filebox`.
- Size: width default 128; at the end of the tag the width is multiplied by *s* (stock `width 500` → 500·*s*). Cells
  are not scaled.
- Layout: cell width c = spacing + 128; columns k = max(1, width / c) (integer division); margin
  m = (width − c·k) / (k + 1); row height r = 3·L + 96 + spacing. Cell (col, row) top-left =
  (x + spacing + m + col·(c + m), y + spacing + row·r). The page content height = ceil(saves / k) · r.
- Drawing: the box is filled with the backfill colour; each cell shows the thumbnail stretched to 128×96 in white
  (SPEC-002 slot 0x54, last argument 6 — see Open questions) and three text lines centred on x_cell + 64: the level at
  y_cell + 96, the time at y_cell + 96 + L, the skill at y_cell + 96 + 2·L; colour tint, atint for the cell under the
  mouse.
- Mouse: the cell under the mouse is found from the same grid (horizontal offset (m + spacing)/2, vertical offset
  spacing/2). Button 1 or 2 on a cell: widget cvar := the save name, `menu/small/select.wav`. Button 2 on `auto`
  additionally plays `menu/small/invalid.wav` and nothing else happens. Otherwise key commands run (stock load menu:
  button 1 loads, button 2 opens the wipe dialog).
- A page refresh (SPEC-800 `refresh`) re-lists the saves and recomputes the layout.

### 6. Map list and bots

**`<listfile cvar …>`** (stock `<listfile sv_maplistfile align center cvar menu_map>`): the positional cvar names a map
list; `maplists/<value>.lst` (SPEC-133) is read as pairs of tokens (map, description). Lines show the descriptions,
aligned as the item's `align`; the line under the mouse uses atint. Whenever the positional cvar changes the list is
reloaded. When the page is built the widget's cvar is set to the first map of the list, or `dm/nycdm1` if the list is
empty or missing. Mouse button 1 on a line sets the widget's cvar to that line's map (no sound, no key commands).

**`<botlist cvar C>`** lists the tokens of `bot/botlist.txt` (SPEC-133), aligned as the item's `align`, line under the
mouse in atint. Mouse button 1 sets the widget's cvar to the name. The same file is used by the console command
`bot_add` without a name: it picks a uniformly random entry (`??????` when the file is empty).

**`<bot cvar headings "Name,Accuracy,FOV,Turn Speed,View,Intelligence,Can Chat" tab>`** shows the profile of the bot
named by its positional cvar, read from `bot/bots/<value>.bpf` (re-read whenever the cvar changes). Lines are read with
leading white space removed; lines starting with `#` are comments; a line is recognised by its case-insensitive prefix,
and its value is the text after the first `=`:

| Row | Line prefix | Value shown |
|---|---|---|
| 0 | `userinfo` | the `name` key of that info string; the portrait is `ghoul/pmodels/portraits/<skin key>` |
| 1 | `accuracy` | text |
| 2 | `fov` | text |
| 3 | `turning_speed` | text |
| 4 | `view_distance` | text |
| 5 | `general_intelligence` | text |
| 6 | `canchat` | text |

Drawing (only after a file has been read): the portrait stretched to 64×64 in white, centred horizontally at the top of
the widget; then from y + 72 one row per heading: heading at tab stop 0, value at tab stop 1, in tint (atint while the
mouse is over the widget). Rows = min(number of headings, 7).

### 7. Errors printed by these tags

All are developer prints in the SPEC-800 tag-error format (`<line> : Command "<tag>" : <message>`), except the chat
message, which is always printed:

| Message | When |
|---|---|
| `Invalid ghoul model` | model file or sequence missing, or a bolted model whose bolt/nut was not found |
| `Could not find EOS note token` | model has no `EOS` note |
| `Could not owning ghoul model` | `gbolt`/`bgbolt` with no parent model on the page |
| `Could not find bolt <A>` / `Could not find nut <B>` | part not found on parent / on this model |
| `No ghoul model to bolt to`, `Could not find part to bolt to` | common `bolt`/`bbolt` (SPEC-800) |
| `Unrecognised <tag> keyword` | unknown option (tag names: ghoul, chat, users, rooms, serverbox, serverdetail, players, translate, filebox, filereq, loadbox, listfile, botlist, bot) |
| `Invalid align parameter` | bad `talign` value |
| `Error : Only one chat allowed per page` | second `chat` on a page (console, always) |

## Constants

| Name | Value |
|---|---|
| Menu model sequence path | `ghoul/menus/<name>.ghl` |
| Player model files | `ghoul/pmodels/*.gpm`; portraits `ghoul/pmodels/portraits/<skin>`; team icons `teamicons/<team>` |
| Model bounds turn for spinning axes | π/4 rad |
| Bolt facing threshold (models attached with `bolt`/`bbolt` are rendered only when facing) | normalised z < 0.05 |
| Model render: light vector offset | (+50, +50, 200) from the screen point; view origin z −200 |
| Chat log size | 100 messages each (public, private) |
| Chat bottom margin | 4 pixels |
| Server columns | fields 0–5; title line + separator = 2 lines; separator colour (128,128,128,255), width 1.0, at 1.5·L |
| Server box default height | 200 |
| Server detail / players default height | 10·L / 8·L |
| File box default width | 16·L |
| Save grid | thumbnail 128×96, cell width 128 + spacing, row height 3·L + 96 + spacing |
| Bot portrait | 64×64, rows start 72 pixels below the widget top |
| Sounds | `menu/small/select.wav`, `menu/small/invalid.wav`; model notes `<arg>.wav` |

## Test vectors / how to verify

1. **No emote handler.** With `developer 1`, open the pages that contain `emote`/`semote`/`demote`
   (`chatmessages`, `a_chathelp`) and `dmnames` (`m_join`): the console shows `Unknown keyword` lines for those tags
   and the pages work normally.
2. **Server columns** (gs `m_join.rmf`, `tab 170 214 255 332 465 520`, 640×480): host name at x+0, ping at x+170, pure
   at x+214, type at x+255, map at x+332, players at x+465; box width 520; first server line at y + 2·12 = y + 24
   (medium font). Verify with a screenshot of the join page with one LAN server.
3. **LAN record.** A server replying `SOF+           noname+    dm/x+1/0/16++DM+0` appears with host `noname`
   (leading spaces stripped), map `dm/x`, players `1/0/16`, type `DM`, pure `No`, ping `local`; selecting it changes
   the ping to a number after the `ack`, and `player_summary` reads `1/16`.
4. **Violence filter.** With `cl_violence 3` and a server with `sv_violence 0`, the server is not listed and
   `locked_summary` = 1.
5. **Sort.** Click the "Players" title twice: ascending, then descending player counts; `menu_server` becomes −1 and
   the host-name cvar shows the text of `menu_nss`.
6. **Server detail.** For a CTF server (`deathmatch 4`) the stock `m_servers.rmf` shows rows for maxclients, fraglimit,
   timelimit, maxspectators, cheats, sv_suicidepenalty (translate 0), ctf_team_red/blue, ctf_loops (translate 4),
   g_movescale, then sv_violence bits (translate 0) and dmflags bits (translate 4), in that order, values that are empty
   on the server omitted.
7. **Save grid.** Load menu (`width 500`, `spacing 16`, small font L = 10) at 640×480: c = 144, k = 3, m = 17; cells at
   x + 33, x + 194, x + 355; rows every 142 pixels starting at y + 16; thumbnails 128×96.
8. **Auto save.** In the load menu press mouse 2 on `auto`: `invalid.wav` plays and the wipe dialog does not open.
9. **Player preview.** In the player setup page click the right model arrow: `menu_gpm` increases, skipping models of
   other teams; `menu_temp_skin`, `menu_temp_team`, `menu_team`, `menu_team_image` follow; the model spins at about
   −1 rad/s.
10. **Static pose / bolts.** On the main menu the button models (`time 0.0`, `bgbolt "b_button01" "a_button01"`) stay
    in their first pose and follow the background model's bolt while the background animates; the `blank` hot spots
    with `bbolt "NULL_…"` move with their bolts (hover tool tips appear at the moved position).
11. **Intro end.** `menu intro`: when the intro sequence ends (`EOS`), `menu amain` is opened (stock `intro.rmf`).
12. **Chat wrap.** Widget width 300, small font: a message of 60 words wraps so that no drawn line exceeds 300 pixels;
    the newest message is at the bottom, 4 pixels above the widget's lower edge.
13. **Bot profile.** `set menu_newbot <name>` for an existing `.bpf`: the bot page shows its portrait and the seven
    values exactly as written after `=` in the file.

## Open questions

1. Projection used by the renderer's menu-model mode (pixel size of one model unit, field of view) and the meaning of the
   argument of SPEC-002 slot 0xb8 beyond "the model has a `forced_camera` part". Check with a probe renderer: log the
   render-parameter block and the GL projection for a known model.
2. Order in which the three rotation angles are composed. Check: a model with `rotation 0.5 0.5 0` and a screenshot
   compared with both orders.
3. Order of the file-list call (SPEC-300 does not define listing order) and whether duplicate names from several search
   paths appear twice in `filebox`. Check: put a loose `maps/dm/nycdm1.bsp` next to the paks and open the map picker.
4. Last argument 6 of the thumbnail stretch call: SPEC-002 describes the last argument as a rotation 0–3; the save grid
   passes 6. Check with a probe renderer what the thumbnail looks like (orientation) and whether the argument also
   selects the image type.
5. Exact effect of "keep colour" for chat lines (SPEC-002 slot 0xc8).
6. Sort order of `filebox` without `sort` (stock menus always give `sort name`).
7. Whether names from archives are shown in upper case in `filebox` (depends on SPEC-300 name normalisation of listed
   entries).
