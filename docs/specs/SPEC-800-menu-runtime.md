# SPEC-800: Menu runtime core — loading, parsing, frames, flow layout and drawing
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (menu system), version 1.07f; font metrics of ref_gl.dll 1.07f
Depends on: SPEC-002 (renderer 2D calls, fonts, clipping), SPEC-130 (`.rmf` grammar and inventory), SPEC-131 (string
packages, `^REF^`), SPEC-132 (`.fnt` files), SPEC-300 (file system), SPEC-302 (cvars, cvar flags, command buffer),
SPEC-801 (menu stack, input, command queue, frame loop), SPEC-802 (standard widgets), SPEC-803 (engine-data widgets)

## Purpose

SPEC-130 describes the `.rmf` menu files as data. This spec describes what the 1.07f engine does with them: how a file
is read and tokenised, how string references and includes are expanded, what every document-level tag does, how the
screen is split into frames, how widgets are placed by the flow layout, which options every widget accepts, and how
pages, frames, backgrounds and borders are drawn. Widget-specific behaviour is in SPEC-802 and SPEC-803; the menu
stack, input routing and the per-frame loop are in SPEC-801. Where SPEC-130 marks an item **[hyp]** or UNKNOWN, the
answer found in the engine is given here and summarised under *Resolved SPEC-130 items*.

## Interface / layout

### Terms

| Term | Meaning |
|---|---|
| menu system | exists while any menu is open; owns the screen stack (SPEC-801) |
| screen | one level of the menu stack, created by `menu <name>`; owns a list of frames, a tint table, tooltip settings, a timeout and the "open time" |
| frame | a named rectangle of a screen. Every screen starts with one frame named `default` that covers the whole video mode (or the `<stm resize>` window). `<frame>` tags create more frames by cutting them out of an existing frame |
| page | one parsed `.rmf` document shown in a frame. A frame keeps a history of pages; only the newest is drawn and receives input |
| widget | one placed item of a page (bare text, `<text>`, `<image>`, …), with a rectangle in page coordinates |
| s | the screen scale (below) |

### Coordinates and scale

- All drawing is in real screen pixels of the current video mode (W_vid × H_vid). There is no virtual 640×480
  coordinate system; instead some quantities are multiplied by the **screen scale** s:
  - s = W_vid × 0.0015625 (= W_vid / 640), stored as a 32-bit float (1.6 is 1.60000002…), for a normal screen;
  - s = 1.0 for a screen whose document has `<stm resize …>` (popup windows are never scaled).
- Scaled by s (result truncated toward zero): frame width and height, frame border padding, `<tab>` stops, the mouse
  cursor picture size and hot spot (SPEC-801), and the units listed as scaled in SPEC-802/803.
- **Not** scaled: text (fonts are drawn at their native pixel size), widget sizes, widget border padding, frame border
  line thickness, `xoff`/`yoff`/`width`/`height` options (these use the widget's own scale, normally 1.0).
- Truncation (`int()`) below always means conversion toward zero.

Example: at 1024×768, s = 1.6; `<frame f 0 92>` is 147 pixels high (int(92 × 1.6)); a title-font line is 18 pixels
high at every resolution.

### Fonts (renderer, ref_gl.dll)

The font name given by `<font type …>` is passed to the renderer unchanged. The renderer knows exactly four names;
their line height and baseline offset are fixed in the renderer (not read from the `.fnt` file):

| Name | Glyph file (SPEC-132) | Character codes | Line height FH | Baseline offset from line top |
|---|---|---|---|---|
| `small` | `pics/menus/fonts/font1` | 32–255 | 10 | 8 |
| `medium` | `pics/menus/fonts/font4` | 32–255 | 12 | 10 |
| `title` | `pics/menus/fonts/font2` | 32–255 | 18 | 14 |
| `interface` | `pics/menus/fonts/inttext` | 32–63 | 32 | 24 |

String width SW(text, font) = sum over the bytes of: FH ÷ 2 (integer) for a space; 0 for a byte outside the font's
range (this includes the colour-code bytes 1–31 and byte 10); the `.fnt` record's `width` for a glyph; the width of `.`
for a byte inside the range whose record has width 0. Drawing uses the same advances; a glyph's top row is placed
`baseline` rows (from its `.fnt` record) above the line's baseline. Any other font name draws nothing and measures 0.
These resolve SPEC-132 open questions 1, 2, 3 and 7 (advance = `width`).

### Colours

- Colours are RGBA with red in the lowest byte; in `.rmf` text a colour is written `0xAABBGGRR` (SPEC-130).
- A colour argument is resolved per screen: if a `<tint>` of that name was defined earlier in the same screen (any page
  or frame of it), its value is used; otherwise the argument itself is converted as a C unsigned number with automatic
  base (`0x…` hexadecimal, leading `0` octal, else decimal). An unknown name therefore gives 0 = fully transparent
  black (nothing visible). Names are case-sensitive (`Red` and `red` are different tints).
- `<tint name value>` stores the value converted the same way; redefining a name replaces its value. The table is
  emptied when the screen is closed.

## Behaviour

### 1. Loading a document

1. A page named `n` is read from `menus/n.rmf` through the file system (SPEC-300). (A name that already ends in `.rmf`
   and contains `base\menus\` is opened from the text after `base\`; not used by stock data.)
2. Only the text from the **first** occurrence of `<stm` up to and including the first occurrence of `</stm` is used
   (the closing `>` and everything after it are ignored — this answers SPEC-130 open question 9: the fog row after
   `</stm>` in `m_scale.rmf` is never shown). A file without `<stm` or without `</stm` is rejected with the message
   `File is not in STM format` and the page is not created (for `menu …` this means the error of SPEC-801).
3. The document is then parsed into a page (section 3). Parsing creates widgets, frames (whose own pages are parsed
   at that moment, recursively) and has side effects (cvars set, commands run).
4. At the end of every page parse the screen's *open time* is set to the current time (used by `<timeout>` and
   `<autoscroll>`).

### 2. Tokeniser

The text is read as a stream of tokens. A read asks for N tokens at once (N = the number of arguments a tag or option
expects).

- Separators: space, tab, CR, LF. Leading separators are skipped. Each LF increments the line counter (starts at 1).
- `"` toggles quoting. Separators inside quotes are kept, each converted to one space character. A closing quote ends
  the token immediately (so `""` is an empty token and `ab"c d"e` gives the tokens `abc d` and `e`).
- `<` and `>` are control characters **even inside quotes**. When met at the start of a token they form a token of their
  own and end the read; the remaining tokens of the request are empty strings. If this happens after at least one
  token of the request was read, the error `Unexpected control char (not enough params)` is printed. When met inside a
  token, they end the token and are read again next time.
- `^NAME^` anywhere (inside or outside quotes) is a string reference (SPEC-131): the characters up to the next `^` are
  looked up in the loaded string packages in the current language and the resulting text is **inserted into the
  stream at that point and read again** (so it continues the current quoted string, may contain several words, and may
  itself contain tags or references). A space, tab, CR, LF or `"` inside the name prints `Unexpected control char
  (invalid character in package token)`, drops that character and ends the name. A name whose text is missing or empty
  prints `Could not find token '<name>'` and is removed. Line breaks inside an inserted text become separators (or
  spaces inside quotes): menu text cannot contain line breaks.
- Tokens longer than 4096 characters are truncated.

### 3. Page parsing

The page reads tokens one at a time until the end of the text:

- A token that is not a control character is **bare text**: it becomes one text widget (SPEC-802) with no options,
  placed by the flow layout (section 6). `Hello world` gives two widgets; `"Hello world"` gives one.
- A control character (`<` or `>`, both treated alike) is followed by the tag name; the tag is executed (sections 4,
  5, 7 and SPEC-802/803). Tag names and option keywords are compared case-insensitively.
- An unknown tag prints `Unknown keyword` and everything up to the next control character is skipped.
- After the whole text was read, every widget performs its end-of-page step (SPEC-802/803); widgets that fail it are
  removed.

The tags `emote`, `semote`, `demote` and `dmnames` of the stock chat/server pages are **unknown** to 1.07f (they are
skipped with `Unknown keyword`); `</stm` itself is the last token and does nothing.

### 4. Error reporting

All parse errors are printed only when the `developer` cvar is non-zero (developer print), in the form

`<line> : Command "<tag name>" : <message>`

where `<line>` is the tokeniser's line counter (lines of the main file and of every included or inserted text are
counted together) and `<tag name>` is the last tag name read. Nothing is shown to the player and parsing continues.

| Message | Cause |
|---|---|
| `File is not in STM format` | section 1 |
| `Unknown keyword` | unknown tag |
| `Spurious parameters` | a tag that takes a fixed number of arguments is not closed by `>` after them; the next token is consumed |
| `Unexpected control char (not enough params)` | `>` or `<` where an argument was expected |
| `Unexpected control char (invalid character in package token)`, `Could not find token '…'` | section 2 |
| `Unable to include <name>` | `include` file missing |
| `Unrecognised <x> keyword` | unknown option of widget `<x>` (SPEC-802/803) |
| `Unrecognised font keyword`, `Unrecognised default parameter` | sections 5.3, 5.4 |
| `Invalid align parameter` | `align` value other than `left`/`right`/`center` (treated as `left`) |
| `No ghoul model to bolt to`, `Could not find part to bolt to` | `bolt`/`bbolt` (section 7) |
| `Object too wide to place` | section 6 |
| `No rects allowed after a variable one` | section 6 |
| `Not allowed to alter default frame`, `Could not find parent for "<name>"`, `Could not create "<name>"` | section 8 |
| `Can only resize default frame` | `<stm resize>` in a page that is not in the `default` frame |
| `Only 1 popup allowed per page` | SPEC-802 |

After an unrecognised widget or `<font>` option the rest of that tag is **not** consumed: the remaining words are read
as page text and the closing `>` as the start of another tag (an error cascade). Stock data never triggers this.

Errors that do not come from the parser are printed with a normal (always visible) console print:
`Error : Cannot find frame "<frame>"`, `Error : Could not create "<page>"`,
`Error : Only one selection allowed per type per page`, `Error : Only one chat allowed per page` (SPEC-801/802/803).

### 5. Document-level tags

#### 5.1 Inclusion (all are textual: the included text is inserted after the tag's `>` and parsed next)

| Tag | Effect |
|---|---|
| `<include m>` | inserts the **whole** file `menus/m.rmf` (no `<stm>` window is applied to included files) |
| `<cinclude cvar m>` | includes `m` if the cvar's numeric value ≠ 0 |
| `<cninclude cvar m>` | includes `m` if the cvar's numeric value = 0 |
| `<exinclude cvar a b>` | includes `a` if the value ≠ 0, else `b` |
| `<einclude cvar value m>` | includes `m` if the cvar's **string** equals `value` ignoring case (so `deathmatch` must read exactly `1`, not `1.0`) |
| `<includecvar cvar>` | inserts the cvar's **string value itself** as markup (not a file name). Used by the error pages to show `menu_won_error` |

A missing or unset cvar reads as 0 / empty string.

#### 5.2 Commands, cvars and configuration

| Tag | Effect, at the moment the tag is parsed |
|---|---|
| `<set c v>` | executes the console text `set c v` immediately (Quake II `$` macro expansion applies; `v` is not quoted, so a value with spaces passes extra arguments to `set`, see SPEC-302). An empty `v` executes `set c ""` |
| `<fset c v flags>` | executes `set c v flags` |
| `<alias n "cmd">` | executes `alias "n" "cmd"` |
| `<cbuf "cmd">` | adds `cmd` to the menu command queue (SPEC-801; forced, split at `;`, executed one per frame) |
| `<config x>` | remembers `x` as the screen's configuration and executes `exec x.cfg` immediately; the screen executes it again every time it is refreshed (SPEC-801) |
| `<exitcfg x>` | remembers `x`; when the screen is left by `popmenu`/ESC/`return`, or replaced by opening another full-screen menu, `exec menus/x` is queued (SPEC-801); the console `exec` command appends `.cfg` to names longer than four characters that lack it, so `<exitcfg applyskin>` runs `menus/applyskin.cfg` |
| `<onexit cvar m>` | remembers the cvar (created as `"0"` if missing) and `m`; while the cvar ≠ 0, leaving the screen opens `m` instead (SPEC-801) |
| `<timeout t "cmd">` | when t seconds have passed since the screen's open time, `cmd` is appended to the console buffer once |
| `<strings p>` | loads string package `strip/p.sp` (SPEC-131) marked as menu-owned. Packages stay loaded (and usable by every later page and screen) until the whole menu system is closed, when menu-owned packages are released |
| `<comment …>` | everything up to `>` is ignored |

#### 5.3 Text appearance and layout state

| Tag | Effect |
|---|---|
| `<font type F tint A atint B>` | sets, for the rest of the page, the font name F, the normal colour A and the hover colour B used by text-like widgets created afterwards. Any subset of the three options, any order. Page defaults: `small`, opaque white, opaque white. Another keyword prints `Unrecognised font keyword` and stops processing the tag |
| `<br>` | line break (section 6) |
| `<hbr>` | line break that also sets the **alignment anchor** (section 6). It is not a smaller break: `<br>` and `<hbr>` move down by the same amount |
| `<normal>`, `<center>`, `<left>`, `<right>` | set the alignment of following widgets and move the current line back up to the anchor (section 6) |
| `<tab x1 x2 …>` | appends tab stops int(xi × s) to the page's tab list (which starts as one stop at 0); several `<tab>` tags keep appending |
| `<autoscroll r>` | the page scrolls by itself (section 9) |
| `<cursor n>` | n ≤ 0 → the mouse pointer is hidden while over this page's frame; n ≥ 1 → standard pointer (SPEC-801). Also becomes the screen's pointer setting |
| `<defaults …>` | options: `border P T tint` — every widget created afterwards in this page gets this border (padding P not scaled); `noborder` cancels it; `tiptint T`, `tipbtint T`, `tipfont F` — tooltip text colour, box colour and font for the whole screen (defaults opaque black `0xff000000`, light blue `0xffffa0a0`, `small`). Other words: `Unrecognised default parameter` |
| `<backdrop …>` | page background (section 9) |

#### 5.4 Keys

`<key K "cmd">`, `<ikey "bound" "cmd">` and `<ckey K cvar "cmd0" "cmd1">` add page-level key bindings; the same three
keywords are also widget options (section 7). Rules common to both levels:

- K is a key name as for `bind` (SPEC-302), or `any` (matches every key).
- `ikey`: one binding is created for every key currently bound to the console command `bound` (keys are found by
  their binding text, compared without case); no binding if the command is unbound.
- `ckey`: the cvar is created as `"0"` if missing; when the key fires, `cmd0` is used if the cvar's value is 0,
  otherwise `cmd1` (confirms SPEC-130's `ckey` hypothesis).
- When a key is delivered to a page (SPEC-801), every page-level binding whose key matches queues its command
  (non-forced, SPEC-801) and marks the screen as ready (this lets a key skip a `waitanim` animation).

#### 5.5 `<stm …>` options

| Option | Effect |
|---|---|
| `resize W H` | only in the page of the `default` frame: the `default` frame becomes W × H pixels (unscaled), centred: x = (W_vid − W) ÷ 2, y = (H_vid − H) ÷ 2 (integer, arithmetic shift); the screen scale becomes 1.0; the screen is drawn over the screen below it and can be dragged with the right mouse button (SPEC-801) |
| `nopush` | the screen is skipped when going back (SPEC-801) |
| `waitanim` | the screen starts "not ready" and ignores input and new menus until its Ghoul animation ends or a page key binding fires (SPEC-801/803) |
| `fragile` | input is accepted even while not ready |
| `global` | this page draws and accepts the mouse over the whole screen, not only inside its frame (section 9) |
| `command "cmd"` | executes `cmd` immediately |

Unknown `<stm>` options are ignored silently.

### 6. Flow layout

Each page keeps a layout state, in page coordinates (origin = top-left of the frame's content area, i.e. inside the
frame's border padding; y grows downwards and includes the page scroll):

| State | Initial | Meaning |
|---|---|---|
| pen x | 0 | right edge of the last placed widget |
| pen y | 0 | top of the last placed widget |
| line top | 0 | top of the current line |
| line bottom | 0 | lowest bottom edge on the current line |
| anchor | 0 | line top recorded by the last `<hbr>` |
| content height | frame content height | grows to the lowest line bottom; used for scrolling |
| alignment | normal | set by `<normal>`/`<center>`/`<left>`/`<right>` and per widget by `align` |

Widths below are *total* widths (inner width × widget scale + 2 × border padding, truncated). The **available width**
A = frame width − 2 × frame border padding − width of the page's vertical bar (0 if none), using the frame's size at the
moment the widget is placed.

**Line breaks.** `<br>`: pen x := 0; if line bottom = line top (nothing on the line) line bottom += FH of the current
page font; then line top := pen y := line bottom. `<hbr>` does the same and then sets anchor := line top.

**Alignment tags.** `<normal>`, `<center>`, `<left>`, `<right>` set the alignment and set line top := line bottom :=
anchor. Pen x and pen y are not changed. This is how `<left>` item `<right>` item puts two items on one row: the
second item starts again at the anchor line.

**Placing a widget** (not bolted):

1. If its total width > A: `Object too wide to place`; the widget is discarded.
2. Start position: x = pen x; y = line top — except bare text and `<text>` without `regular`, which start at y = pen y
   (the top of the previous item, so words of a sentence stay on one row even when an earlier item on the row was
   taller or lower).
3. Collision: a widget *overlaps* another placed, non-bolted widget of the same page if their rectangles intersect
   (strict inequalities on both axes). The first overlapping widget in creation order is used.
4. By alignment:
   - normal: if the widget does not fit (x + w > A) it *wraps*: x becomes 0 and y grows by FH of the current page
     font. While it overlaps a widget o it moves right to x = o.x + o.w; each time it no longer fits it wraps again.
     Its final position is the first one that fits and overlaps nothing.
   - center: x = (A − w) ÷ 2 (integer, rounding down); while it overlaps a widget o it moves down to y = o.bottom.
   - left: x starts at 0; while it overlaps a widget o it moves right to x = o.x + o.w. If it then no longer fits,
     it moves to x = 0 and down to the largest bottom edge among the widgets it overlapped in this attempt (vertical
     bars not counted), and the attempt starts again.
   - right: x starts at A − w; while it overlaps a widget o it moves left to x = o.x − w. If x becomes negative, it
     moves to x = A − w and down to the largest bottom edge among the widgets it overlapped in this attempt (vertical
     bars not counted), and the attempt starts again.
5. Then: pen x := x + w; pen y := y; line bottom := max(line bottom, y + h); content height := max(content height,
   line bottom). The widget is appended to the page's widget list (creation order = drawing order).

Notes: a wrap moves down by exactly one font line from the current y, not to the line bottom; tall items on the
previous row are avoided only through the overlap rule (the new item is pushed to their right). Nothing ever moves
widgets up.

**Variable-height widgets.** After a `users`, `chat`, `rooms`, `filebox`, `serverbox`, `players`, `listfile`,
`botlist` or `bot` widget the page accepts no further widget of any kind:
each later widget or bare word prints `No rects allowed after a variable one` and is not created.

**Bolted widgets** (`bolt`/`bbolt`, section 7) are not placed by the flow and do not take part in overlap tests; their
position comes from the Ghoul bolt every frame (SPEC-803). If their alignment is `center`, xoff := int(−w ÷ 2 × scale)
and yoff := int(−h ÷ 2 × scale) (w, h total, ÷ by arithmetic shift); if `right`, xoff := int(−w × scale) and
yoff := int(−h × scale); otherwise the given offsets stay.

**Conditions.** A widget whose condition options (section 7) evaluate false is not created at all (it takes no space).

### 7. Common item options

Every widget tag accepts these options after its own arguments (SPEC-802/803 list exceptions). They are processed
left to right; later options override earlier ones.

| Option | Values | Effect |
|---|---|---|
| `key` | K cmd | widget key binding (section 5.4); makes the widget interactive. Fires only when the key reaches the widget, i.e. while it is hovered or holds the keyboard focus (SPEC-801); plays `menu/small/select.wav` |
| `ckey` | K cvar cmd0 cmd1 | as section 5.4 |
| `ikey` | bound cmd | as section 5.4 |
| `next` / `prev` | — | shorthand for `key mouse1 "menu $menu_next"` / `key mouse1 "menu $menu_prev"` |
| `tip` | text | tooltip text (SPEC-801); makes the widget interactive |
| `inc` | f | while the widget is activated with the left button (SPEC-801), value := cvar value + f; makes the widget interactive |
| `mod` | f | with `inc`: value := value modulo f (C `fmod`), plus f if negative; the cvar is then set to the value |
| `cvar` | name | the widget's cvar; created with an empty value and no flags if missing. If it cannot be created (e.g. a hidden cvar while hidden cvars are locked, SPEC-801 `checkpass`) the whole **page fails** to be created |
| `cvari` | name | as `cvar`, but created with value `"0"` and the integer flag (0x20, SPEC-302) |
| `tint`, `atint`, `btint`, `ctint`, `dtint` | colour | normal, hover, background (hover fill), and two extra colours used by some widgets (SPEC-802) |
| `noshade` | — | label text is not darkened (SPEC-802) |
| `noscale` | — | widget-specific (SPEC-802 `image`) |
| `border` | P T colour | P pixels of padding around the inner area (not scaled) with a T-pixel outline; total size = inner × scale + 2P |
| `noborder` | — | removes a border (also one from `<defaults>`) |
| `width` | W | inner width := W |
| `height` | H | inner height := H, only if H is greater than the widget's current minimum height; the minimum then becomes H |
| `xoff`, `yoff` | n | drawing offset int(n × widget scale). Offsets move only the drawing: the mouse area stays at the layout position. For bolted widgets they offset the bolt position |
| `tab` | — | the widget uses tab stop 1 (the first value of `<tab>`) to separate label and value (SPEC-802) |
| `align` | `left`/`right`/`center` | alignment of this widget only; another value prints `Invalid align parameter` and means `left`. Default: the page's current alignment |
| `bolt` | name | attach to bolt `name` of the Ghoul model created most recently in this page by `<ghoul>` or `<bghoul>` (SPEC-803); error `No ghoul model to bolt to` if the page has none yet, `Could not find part to bolt to` if the bolt does not exist |
| `bbolt` | name | attach to bolt `name` of the page's **first** background model (`<bghoul>`); same errors |
| `ifeq`, `ifne`, `iflt`, `ifgt`, `ifle`, `ifge` | cvar v | condition on the cvar's numeric value compared with v (=, ≠, <, >, ≤, ≥) |
| `ifset`, `ifclr` | cvar bit | condition: bit `bit` (0-based) of the integer value is set / clear |

Conditions: each condition option overwrites the result of the previous one (the **last** condition decides; they are
not combined). Without a condition a widget is created. SPEC-802 lists widgets that ignore conditions.

Interactive widgets (those with `key`, `ckey`, `ikey`, `tip`, `inc`, `next`, `prev`, and widgets interactive by nature
such as lists and inputs) take part in TAB navigation (SPEC-801).

### 8. Frames

`<frame name W H [options]>`:

- `name` = `default` → `Not allowed to alter default frame`, tag skipped.
- If a frame of that name already exists in the screen, its options are processed again and its page is loaded
  (page history rules of SPEC-801 §2); the size arguments are ignored and it is not cut again.
- Otherwise a frame of size w = int(W × s), h = int(H × s) is created with parent `default`, then its options are read,
  then it is cut out of its parent (below), then its page (if any) is loaded and parsed immediately. If the parent does
  not exist: `Could not find parent for "<name>"`. If the page cannot be created: `Could not create "<name>"` and the
  screen is marked invalid (the `menu` command that created it then fails, SPEC-801).

Options (read until `>`; unknown words are **ignored without a message** — so the `center` option found on 32 stock
frames has no effect):

| Option | Effect |
|---|---|
| `border P T colour` | padding int(P × s), outline thickness T (not scaled), outline colour; replaces an earlier border |
| `backfill colour` | fill colour of the padding ring (default opaque black); only takes effect if a `border` was given **before** it in the same tag |
| `page m` | the page shown in the frame |
| `cpage cvar` | the page name is the cvar's string (cvar created empty if missing); see SPEC-801 refresh for later changes |
| `cut parent` | the frame is cut out of frame `parent` instead of `default` |

**Cutting.** Each frame has a *free rectangle* (initially its own rectangle). A new child is cut from its parent's
free rectangle (x_f, y_f, w_f, h_f) with the parent's padding P_p:

- child position = (x_f + P_p, y_f + P_p) (plus the screen origin, normally 0, SPEC-801);
- if the parent's free width or height is 0, the child is 0 × 0;
- child width: the requested w if w ≥ 1, else w_f − 2·P_p + w (so 0 = "all", a negative value = "all but");
  child height likewise from h_f;
- then the parent's free rectangle shrinks: if the **requested** W was non-zero, a column is removed from the left
  (x_f += child width, w_f −= child width); if W was 0, a strip is removed from the top (y_f += child height,
  h_f −= child height); if both W and H were 0, both happen.

So W ≠ 0 always cuts a column (even when H ≠ 0, e.g. `<frame main 640 372>` cuts a 640-wide column, 372 high, at the
top of it); W = 0 cuts a full-width strip from the top. The frame's outline and fill use the rectangle the frame had
when it was created. A frame's page is laid out against the frame's free rectangle as it is when each widget is placed
and is drawn in the free rectangle as it is at draw time; frames cut later from the same frame therefore shrink and
shift the area of a page that is already laid out — stock documents declare child frames before any content.

The `default` frame of a screen has no border and covers (0, 0, W_vid, H_vid), or the `resize` window.

Frames are drawn in creation order (`default` first): for each frame, its current page (section 9), then its border
(section 10). The border's padding ring is filled with the backfill colour only when the frame has no page (a page
paints its own background over the whole frame rectangle).

### 9. Page drawing

Every frame, for the newest page of each frame:

1. Clipping (SPEC-002 slot 0x8c) is set to the frame's free rectangle; for a `global` page it is set to a rectangle
   starting at the frame's top-left corner with the width and height of the screen's `default` frame.
2. Background (`<backdrop>`, if the page has one): `bgcolor C` fills the clip rectangle with C (slot 0x74) when C ≠ 0;
   then the picture (from `pics/menus/`, registered when the tag is parsed; if it cannot be found its name is cleared
   and the console message `Warning: Invalid background image` is printed) is drawn in opaque white according to the last of these options:

   | Option | Drawing (rectangle R = clip rectangle, picture size pw × ph) |
   |---|---|
   | `tile p` | copies at (R.x + i·pw, R.y + j·ph) covering R |
   | `stretch p` | one copy stretched to R (slot 0x54) |
   | `center p` | one copy at (R.x + (R.w − pw) ÷ 2, R.y + (R.h − ph) ÷ 2) |
   | `left p` | one copy at (R.x, R.y) |
   | `right p` | one copy at (R.w − pw, R.y) — note: R.w, not R.x + R.w (original behaviour) |

   Other words (e.g. the stock `global`) are ignored. Several `<backdrop>` tags in one page modify the same background.
   Background Ghoul models (`<bghoul>`) are drawn after the picture (SPEC-803).
3. Autoscroll: if the page has `<autoscroll r>`, its scroll offset := int(((now − screen open time) in seconds × r × 10)
   mod L), L = content height − (frame height − 2 × padding); a negative result is increased by L. (r is in units of
   10 pixels per second; the page wraps around.)
4. For each widget in creation order: its per-frame update (SPEC-802/803), then, if it is in view, its drawing.
   In view = bolted, or its vertical extent (screen y … y + total height) reaches into the frame's content area
   (partially visible widgets are drawn; the clip cuts them).
5. Clipping is reset to the whole screen (slot 0x90).

Screen position of a non-bolted widget's inner area (used for drawing):
x = frame free x + frame padding + widget border padding + widget x + xoff (+ screen origin),
y = frame free y + frame padding + widget border padding + widget y + yoff − page scroll (+ screen origin).

### 10. Borders

A border with padding P, thickness T, outline colour C and fill F around the rectangle (x, y, w, h) (a frame's
rectangle, or a widget's total rectangle on screen):

- Fill (frames without a page only): four filled rectangles (slot 0x74) covering the padding ring:
  (x, y, w, P), (x, y + h, w, −P), (x, y + P, P, h − 2P), (x + w, y + P, −P, h − 2P) (negative sizes extend left/up).
- Outline (if T ≠ 0 and C ≠ 0x00000000): with i = (P − T) ÷ 2 (arithmetic shift), four thick lines (slot 0x7c) of
  width T through the corners (x + i, y + i), (x + i + w − (P − T), y + i), (x + i + w − (P − T), y + i + h − (P − T)),
  (x + i, y + i + h − (P − T)), in the order top, right, bottom, left.

Widgets draw their own border after their content (SPEC-802/803). A widget border is never filled.

## Constants

| Name | Value |
|---|---|
| screen scale | W_vid × 0.0015625 (1.0 for `resize` screens) |
| font line heights / baselines | small 10/8, medium 12/10, title 18/14, interface 32/24 |
| space advance | FH ÷ 2 |
| default page font / colours | `small`, 0xFFFFFFFF, 0xFFFFFFFF |
| default frame backfill | 0xFF000000 (opaque black) |
| default tooltip colours / font | text 0xFF000000, box 0xFFFFA0A0, `small` |
| maximum token length | 4096 bytes |
| autoscroll unit | r × 10 pixels per second |
| page window | text from first `<stm` to first `</stm` |

## Test vectors / how to verify

All vectors assume 640×480 (s = 1) unless stated, stock fonts of pak0 (SPEC-132 widths: font1 `A` 7, `B` 6, `C` 6,
`D` 6, `E` 6), and a frame without border and without vertical bar.

1. **Bare words.** Page text `<font type small> ABC DE <br> A` → three text widgets: `ABC` at (0, 0) size 24 × 10
   (19 + 5), `DE` at (24, 0) size 17 × 10, `A` at (0, 10) size 12 × 10.
2. **Empty line.** `A <br> <br> B` → `A` at (0, 0), `B` at (0, 20) (the second `<br>` adds one line height).
3. **Wrap.** In a frame 40 pixels wide: `ABC ABC` → first at (0, 0); second does not fit (24 + 24 > 40) and moves
   to (0, 10).
4. **Centre.** In a frame 100 wide: `<center> "AB"` → x = (100 − 18) ÷ 2 = 41.
5. **Left/right row.** `<left> <text "AB" border 16 1 red> <right> <text "AB" border 16 1 red>` in a frame 320
   wide: both buttons have total size 50 × 42 (18 + 32, 10 + 32); the first at (0, 0), the second at (270, 0).
   With a leading `<hbr>` both are at y = 10 (a break on an empty line advances one line height).
5a. **Anchor.** In a frame 100 wide: `AB <br> <right> <text "CD" regular>` → `CD` (17 wide) at (83, 0): `<right>`
   moved the line back to the anchor (0) and `regular` text starts at the line top. Without `regular` it is at
   (83, 10) (it starts at the previous item's y).
6. **Frame cuts at 640×480.** `<frame a 160 0> <frame b 0 92> <frame c 0 0>` → a = (0, 0, 160, 480),
   b = (160, 0, 480, 92), c = (160, 92, 480, 388). At 800×600 (s = 1.25): a = (0, 0, 200, 600),
   b = (200, 0, 600, 115), c = (200, 115, 600, 485).
7. **`W` ≠ 0 always cuts a column.** `<frame main 640 372 border 40 1 gray> <frame chat 0 272 cut main>` at 640×480
   → main = (0, 0, 640, 372); chat = (40, 40, 560, 272).
8. **Popup window.** `quit.rmf` (`<stm resize 400 240>`, `<frame main 400 240 border 40 1 gray backfill smokey>`,
   `<frame alert 0 0 page a_quit cut main>`) at 1024×768 → window at (312, 264); main = (312, 264, 400, 240) with a
   40-pixel smokey ring and a 1-pixel gray outline through (331, 283)–(692, 283)–(692, 484)–(331, 484); alert =
   (352, 304, 320, 160); nothing in the window is scaled.
9. **Tints.** `<tint red 0xff0000ff>`: `tint red` gives bytes FF 00 00 FF (opaque red). `tint 255` gives FF 00 00 00
   (red, alpha 0: invisible). `tint nosuchname` gives 0 (invisible).
10. **Window.** A file `<stm> A </stm> B` shows only `A`. A file without `<stm` fails to open (`File is not in STM
    format` with `developer 1`).
11. **includecvar.** `set menu_x "<font type title>Hello"` then a page with `<includecvar menu_x>` shows `Hello` in
    the title font.
12. **String reference outside quotes.** With `MENU_GENERIC_SURE` = `Are you sure?`, the bare reference
    `^MENU_GENERIC_SURE^` makes three text widgets; `"^MENU_GENERIC_SURE^"` makes one.
13. **Errors.** With `developer 1`, a page containing `<frobnicate>` on line 3 prints
    `3 : Command "frobnicate" : Unknown keyword`.
14. **Frame option `center` is ignored.** A frame with and without `center` produce identical screenshots.
15. **Last condition wins.** `<text "X" ifne a 0 ifeq a 0>` with `a` = 1 → not created; with `a` = 0 → created.

## Resolved SPEC-130 items

- Frame model (open question 2): column-from-left when W ≠ 0, strip-from-top when W = 0; non-zero both = column with
  that height at the top; requests larger than the free area are not clipped (the child simply extends past it);
  top-level frames are stacked from the top-left, not centred. `center` on frames is ignored.
- `<br>`/`<hbr>` (open question 3): same vertical step; `<hbr>` additionally sets the anchor used by alignment tags.
- Fonts (SPEC-132 open questions 1–3, 7): table above.
- `ckey`: cmd0 when the cvar is 0, cmd1 otherwise.
- `bolt` vs `bbolt`: the page's most recent `<ghoul>`/`<bghoul>` model vs its first background model (SPEC-803).
- `<includecvar>` inserts the cvar's text, not a menu.
- `<einclude>` compares strings without case.
- Content after `</stm>` (open question 9): never read.
- `<stm>` `global`, `fragile`, `nopush`, `waitanim` (open questions 6, 10): table 5.5 and SPEC-801.
- `<strings>` visibility: packages stay loaded until the menu system closes.
- `<cbuf>` runs through the menu command queue, `<set>`/`<alias>`/`<config>`/`<stm command>` immediately.

## Open questions

1. Whether `$` macro expansion inside `<set>` values also applies to `#` (SPEC-130 open question 5): the engine passes
   the text to the console unchanged; `#name` handling, if any, is in the console code, not in the menu system
   (UNVERIFIED — check `alias t "echo #menu_chat"` from a menu key).
2. Exact appearance of the thick outline for even/odd T (renderer slot 0x7c placement of the line width) —
   compare a screenshot of `quit.rmf` at 640×480 with test vector 8.
