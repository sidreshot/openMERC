# SPEC-802: Menu widgets — text, values, pictures, controls
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (menu system), version 1.07f; one renderer detail of ref_gl.dll (picture flips)
Depends on: SPEC-800 (menu runtime core: page coordinates, flow layout, fonts, colours, screen scale, common item
options, error reporting), SPEC-801 (menu stack, input routing, command queue, sounds), SPEC-002 (renderer 2D calls),
SPEC-130 (`.rmf` grammar and inventory), SPEC-300 (file listing), SPEC-302 (cvars, cvar flags, key binding commands).
SPEC-803 covers the engine-data widgets (file/save/server/bot/chat lists, Ghoul models).

## Purpose

Defines, for each "plain" widget tag of the `.rmf` menu language, what the original engine does at run time: which
arguments and options it accepts (including options the stock data never uses), how big it is, how and in which colour
it is drawn, how it reacts to keys and the mouse, when it reads and writes its cvar, which sounds it plays and which
errors it reports. Together with SPEC-800 a reimplementation can render these widgets pixel-identically.

Covered tags: `text` (and bare text), `ctext`, `list`, `slider`, `input`, `setkey`, `image`, `hbar`, `vbar`,
`ticker`, `hr`, `blank`, `selection`, `popup`.

## Interface / layout

### Conventions used in this spec

- *x, y*: the widget's top-left drawing position on the screen, i.e. its page position converted by SPEC-800
  (frame origin + frame padding + widget border padding + offsets − page scroll). All 2D calls receive screen pixels.
- *W, H*: the widget's own (inner) width and height. *Total size* = W·scale + 2·border padding, truncated toward zero
  (SPEC-800 common item option `border`; scale is 1.0 except for `image … scale`). Flow layout and mouse hit tests use the
  total size; drawing uses W, H.
- *FH(font)*: renderer line height of a font (SPEC-002 slot 0xd0); *SW(text, font)*: renderer string width (slot 0xcc).
- *The page font*: the font name, normal colour and hover colour current at the point of the tag (`<font …>`,
  SPEC-800). Widgets marked "page font" copy all three when created: their `tint` = the font's `tint`, their `atint` =
  the font's `atint`. All other widgets start with tint = atint = opaque white.
- Default colours of every widget (before options): `tint` 0xFFFFFFFF, `atint` 0xFFFFFFFF, `btint` 0x00000000
  (fully transparent), `ctint` 0xFFFFFFFF, `dtint` 0xFFFFFFFF. Colours are RGBA, red in the lowest byte
  (`0xAABBGGRR` in `.rmf` notation).
- *Screen scale* `s`: SPEC-800 (video width / 640; 1.0 inside a `<stm resize>` window).
- *Hovered*: the mouse is over the widget's total rectangle (SPEC-801). *Disabled*: the widget's cvar has the latch flag
  (0x0010, SPEC-302) while the client is connected to a server (client connection state ≥ "connected"; UNVERIFIED which
  exact state — check: run a local game, open the video options, see the latched rows greyed).
- Key names are the binding names of SPEC-302 (`MOUSE1`, `MOUSE2`, `LEFTARROW`, `RIGHTARROW`, `UPARROW`, `DOWNARROW`,
  `BACKSPACE`, `DEL`, `ENTER`, `KP_ENTER`, `ESCAPE`, `INS`, `KP_INS`). SPEC-801 says which keys reach which widget
  (ENTER is delivered as a MOUSE1 press followed by ENTER).
- *Key list*: the `key`/`ckey`/`ikey` bindings of the widget (SPEC-800). "Run the key list" = for every binding whose
  key equals the pressed key or is `any`, play `menu/small/select.wav` and queue its command (SPEC-801 command queue).
  Unless a widget section says otherwise, a widget runs its key list for every key it receives.
- Sounds are local 2D sounds: `select` = `menu/small/select.wav`, `invalid` = `menu/small/invalid.wav`,
  `click` = `menu/small/click.wav`.

### The label/value text renderer (shared by text, ctext, list, setkey)

These widgets draw one line of text made of a *label* followed by a *value*, in the widget's font:

1. Base colour C = `tint`, or `atint` while hovered.
2. Label colour = C with its red, green and blue bytes multiplied by 0.7 (alpha unchanged) unless the widget has the
   `noshade` option, in which case label colour = C.
3. Value colour = C. While the widget is the one waiting for a key press (only `setkey`, see below), the value colour's
   alpha byte is (milliseconds since start + 128) mod 256 — a sawtooth blink with a 256 ms period.
4. If the widget is disabled: hover is cancelled and both colours become `tint` with alpha 0x60.
5. Without a tab stop: clipping is narrowed to end at the right edge of the frame's content area (minus the page's
   vertical bar width); while hovered a filled rectangle (SPEC-002 slot 0x74) of size W × FH in `btint` is drawn at
   (x, y); the label is drawn at (x, y) and the value at (x + SW(label), y); clipping is then restored.
6. With the `tab` option (tab stop T, SPEC-800): the label is drawn at (x, y) clipped to end at x + T, the value at
   (x + T, y) clipped at the frame's content edge; no hover rectangle is drawn.
Strings are drawn with SPEC-002 slot 0xc8 (proportional font, colour codes per SPEC-002).

### Row helpers (also used by SPEC-803 widgets)

- *Columns*: a row of strings, column i drawn at x + tabstop(i) and clipped to end at x + tabstop(i+1) (the right edge
  of the frame content when that tab stop does not exist), all in `tint` (`atint` while hovered).
- *Rows*: strings drawn one below the other, one FH apart, the selected row in `atint`, the others in `tint`.
- *Row under the mouse*: column offset = mouse x − widget x − border padding (must be 0…W), row offset = mouse y +
  page scroll − widget y − border padding (must be 0…H); row = row offset ÷ FH (integer); −1 when outside or past the
  last row.

### Comma lists

Label lists (`list`, `popup`, `match`) are split at commas. The text (at most 255 characters are used) has a comma
appended, and every comma ends one item: empty items are kept (`",a"` → `""`, `"a"`), and a trailing comma produces an
empty last item. `^REF^` string references are already expanded by the tokenizer (SPEC-800), so a reference whose text
is `"No,Yes"` gives two labels.

### Picture flips (renderer, extension of SPEC-002 slot 0x54)

The rotation argument of the stretched-picture call accepts 8 values. 0–3 are SPEC-002's quarter turns; 4–7 select
these texture coordinates for the corners (x, y), (x+w, y), (x+w, y+h), (x, y+h):

| Value | Texture coordinates | Effect |
|---|---|---|
| 4 | (0,0) (1,0) (1,1) (0,1) | unchanged |
| 5 | (1,0) (0,0) (0,1) (1,1) | mirrored left–right |
| 6 | (0,1) (1,1) (1,0) (0,0) | mirrored top–bottom |
| 7 | (1,1) (0,1) (0,0) (1,0) | both (180° turn) |

All menu pictures are named relative to `pics/menus/` (`"misc/bar"` → `pics/menus/misc/bar`).

## Behaviour

### text — bare words and `<text "s">`

- Every bare word or quoted string outside a tag (SPEC-130) becomes one text widget; `<text "s" …options>` creates one
  with options. Font, tint and atint: page font.
- Size: W = SW(s) + ⌊FH/2⌋ (the text plus one space advance), H = FH.
- Option `regular` (in addition to SPEC-800 common options): the widget starts at the top of the current layout line
  instead of at the vertical position of the previously placed item (SPEC-800 flow layout). Unknown words: error
  `Unrecognised text keyword` (the tag's remaining options are skipped).
- Drawn with the label/value renderer, label empty, value = the text — the text therefore always uses the unshaded
  colour; `noshade` has no visible effect on a text widget.
- Not interactive by itself; with `key`/`ckey`/`ikey`/`inc` it becomes a button (SPEC-800).
- A text widget whose condition options (`ifne` …) are false is not created.

### ctext — `<ctext cvar …>`

- Shows the current string value of `cvar` (created empty with flags 0 if missing). Page font.
- Options: `atext "s"` (label printed before the value), `invisible`, common options; unknown → `Unrecognised ctext
  keyword`.
- Size: while parsing W = SW(value) + ⌊FH/2⌋; after the options W = SW(value) + SW(atext) (no trailing space), H = FH.
- Each frame, if the cvar changed since the last check, W is recomputed and, for `<center>`/`<right>` alignment (or
  `align`), the horizontal position is recomputed: centre = (content width − total width) ÷ 2, right = content width −
  total width, where content width = frame inner width − vertical bar width. When placed on a bolt, the horizontal
  offset becomes −total/2 (centre) or −total (right) instead. The check consumes the cvar's "modified" flag.
- `invisible`: nothing (not even the border) is drawn while the value is empty.
- Drawn with the label/value renderer: label = atext (shaded unless `noshade`), value = cvar value.
- Keys: ignored entirely while the value is empty; otherwise the key list runs.

### list — `<list "labels" …>`

- Labels: comma list (above). Page font. Always interactive.
- Options (plus common options, `cvar`/`cvari` select the cvar):

| Option | Effect |
|---|---|
| `atext "s"` | label printed before the value |
| `match "v0,v1,…"` | cvar values stored for each label; if the count differs from the label count the error `Different number of match parameters to list parameters` is printed but the list is kept |
| `bitmask N` | the list edits bit N (mask = 2^N) of the cvar's integer value; label 0 = bit clear, label 1 = bit set |
| `files "dir" "pattern" "ext"` | labels and match values are replaced by the file names found by listing `dir/pattern.ext` (SPEC-300): directories are skipped; the leading `dir/` is removed; if `ext` is non-empty, does not start with `.` and contains no `*`, the last `.` and everything after it are removed too; names that become equal to an earlier one are dropped (UNVERIFIED: listing order = search-path order, check with two map lists in different paks) |
| unknown | `Unrecognised list keyword` |

- Selected index from the cvar value v:
  - bitmask: 1 if (integer part of v) AND mask ≠ 0, else 0;
  - no match list: integer part of v, clamped to 0 … label count − 1;
  - match list: the first match entry equal to the cvar string (case-insensitive); 0 if none.
- Applying index i writes the cvar: bitmask → integer value with the bit set (i ≠ 0) or cleared; no match list →
  the number i; match list → the string match[i].
- When the page is built (after layout) and on every page refresh (SPEC-801 `refresh`) the index is read from the
  cvar **and written back** (so out-of-range or unmatched values are normalised immediately: `s_loadas8bit 5` becomes
  `1`; an unmatched string becomes match[0]).
- Size: A = T if the `tab` option is used, else SW(atext). For layout W = A + widest label; after the first apply
  W = A + SW(selected label) (the hover area and centring follow the shown label). The common option `width` is
  overridden. H = FH. Centred/right-aligned lists are re-positioned after each apply as for ctext.
- Drawn with the label/value renderer: label = atext, value = selected label.
- Keys: if disabled, every key is ignored (including the key list). MOUSE1 → next label (after the last: the first);
  MOUSE2 → previous label (before the first: the last). The new index is applied, then re-read from the cvar; the
  `invalid` sound plays if the index did not change (e.g. a one-label list), else `select`. Other keys do not change the
  value. Then the key list runs.
- A list with no labels is not created.

### slider — `<slider …>`

- Options (plus common options): `min F` (default 0), `max F` (default 1), `cvarmax cvar` (max = that cvar's value at
  load time), `step F` (default: 1.0 if max − min > 2, otherwise 0.05), `vertical`, `horizontal` (default),
  `bar "pic"`, `button "pic"`, `cap "pic"`. Defaults: horizontal `misc/bar`, `misc/button`, `misc/cap`; vertical
  `misc/barv`, `misc/buttonv`, `misc/capv`. Unknown → `Unrecognised slider keyword`. Always interactive.
- Picture sizes (bw×bh bar, kw×kh button, cw×ch cap) come from the pictures.
  Horizontal: W = bw + 2·ch (the cap *height* — faithful to the original), H = bh.
  Vertical: W = bw, H = bh + 2·ch.
- At page build: v = cvar value clamped to [min, max] and written back to the cvar. If min > max the error
  `Cleanup : min > max` is printed and the slider is removed.
- Drawing (value v = cvar clamped to [min, max]; caps and bar in `tint`; the button in `tint`, or `tint` with alpha
  0x60 when disabled; all with the stretched-picture call):
  - horizontal: left cap at (x, y) size cw×ch flip 0; bar at (x + cw, y) bw×bh; right cap at (x + cw + bw, y) cw×ch
    mirrored left–right (value 5); button at
    (⌊(v − min)·(bw − kw − 2)/(max − min) + cw + x⌋ + 1, y + ⌊(bh − kh)/2⌋), size kw×kh;
  - vertical: top cap (x, y) cw×ch; bar (x, y + ch) bw×bh; bottom cap (x, y + ch + bh) mirrored top–bottom (value 6);
    button at (x + ⌊(bw − kw)/2⌋, ⌊(max − (v − min))·(bh − kh − 2)/(max − min) + ch + y⌋ + 1). The vertical formula is
    the original's: it puts `max` at the top only when min = 0.
  - Hover does not change the slider's colours; there is no hover rectangle.
- Keys: if disabled, every key is ignored. Horizontal: LEFTARROW → cvar − step, RIGHTARROW → cvar + step;
  vertical: UPARROW → + step, DOWNARROW → − step; a change plays `select`. For **every** key (also unrelated ones) the
  slider then clamps its stored value to [min, max] and writes it to the cvar, then runs the key list.
- Mouse (MOUSE1 press and drag, SPEC-801): with the mouse at offset mx/my from the slider's top-left (inside its
  border padding):
  - horizontal: mx < cw → cvar − step; mx > cw + bw → cvar + step; otherwise
    v = min + (max − min)·p/(bw − kw) with p = clamp(mx − ⌊kw/2⌋ − cw, 0, bw − kw);
  - vertical: my < ch → cvar + step; my > ch + bh → cvar − step; otherwise
    v = min + (max − min)·p/(bh − kh) with p = clamp(⌊kh/2⌋ + H − kh − ch − my, 0, bh − kh) (H = bh + 2·ch);
  - the result is clamped and written to the cvar immediately; no sound. Values are not rounded to `step` (an
    integer cvar from `cvari` is truncated by the cvar system, SPEC-302).

### input — `<input …>`

- Options (plus common options; `cvar` gives the edited cvar, `width` the field width W): `global`, `hidden`,
  `maxchars N`, `history`. Unknown → `Unrecognised input keyword`. Page font; H = FH. When placed on a bolt the drawn
  width is W·s (the mouse area keeps the unscaled width; UNVERIFIED) and the horizontal offset is scaled by s.
- `global`: the field receives all keys of its page even when the mouse is elsewhere (keyboard focus, SPEC-801), and is
  always drawn as active. Without it the field receives keys only while hovered.
- Fitting rule (applied at page build, then after every edit): with `maxchars N` (N > 0), a value longer than N loses
  its character at position N; otherwise characters are removed from the end until SW(value) ≤ W − SW("_") (with
  `hidden`: until count·SW("*") ≤ W − SW("_")). At page build the fitted value is written back to the cvar.
- Editing (every key the field receives): if the key types characters, play `click`. Then:
  BACKSPACE or DEL → delete the last character; ENTER, KP_ENTER, ESCAPE → no change; UPARROW / DOWNARROW (only with
  `history` and a non-empty history) → previous / next history line (wrapping) replaces the text; shift+INS, shift+KP_INS,
  ctrl+V → append the clipboard text (cut at the first line break, UNVERIFIED which characters end it); any other key →
  append the character the key produces for the current language and shift state (nothing for keys without a
  character). The text is fitted and written to the cvar immediately (every keystroke changes the cvar). Then the key
  list runs (stock chat lines use `key enter "…"` to send).
- History: the field remembers the value it last wrote. When the cvar changes by any other means (typically a command
  clears it after sending), the remembered value is appended to the history and the history position is reset to the
  end. Repeated identical lines are all kept (no limit found).
- Drawing: if `global` or hovered, a filled W × FH rectangle in `btint` at (x, y). The value (or one `*` per
  character for `hidden`) in `tint`; if the text is wider than W − SW("_") it is shifted left so that its end stays
  visible and it is clipped at x. If `global` or hovered, a `_` cursor is drawn right after the text while
  (milliseconds mod P) > P/2, P = cvar `cursor_blink_speed` (default 500); P = 0 shows no cursor.

### setkey — `<setkey "command" …>`

- Shows which keys are bound to `command` and lets the player rebind them. Options: `atext "s"`, `talign
  left|right|center` (accepted, no effect; an invalid word prints `Invalid align parameter`), common options; unknown →
  `Unrecognised setkey keyword`. Page font, H = FH, always interactive.
- Key text: the localized names (SPEC-302 key names via the `key` string package) of the first two keys, in ascending
  key-number order, whose binding equals `command` (case-insensitive); two keys are joined by `" / "`; no key gives
  `"???"`. The text is rebuilt whenever key bindings change.
- Size: W = SW(key text) + SW(atext); with `tab`, W becomes T + SW(key text) after the first draw.
- Drawn with the label/value renderer: label = atext, value = key text; the value blinks while this widget waits for a
  key.
- Keys (while no setkey of the window is waiting): MOUSE1 → if two keys are already bound to the command, both are
  unbound first; the widget starts waiting; `select`. DEL → every key bound to the command is unbound. While waiting,
  the next key the menu receives (SPEC-801; ESCAPE cancels instead) is bound by queuing the console text
  `bind "<keyname>" "<command>"`, and waiting ends. The key list runs in all cases.
- Test: with `+attack` bound to MOUSE1 and CTRL, `<setkey "+attack" atext "Fire: ">` shows `Fire: ` (70 % brightness)
  followed by the localized names of MOUSE1 and CTRL separated by ` / `; clicking it unbinds both and blinks.

### image — `<image "pic" …>`

- Shows `pics/menus/<pic>`; size W×H = the picture's size. If the picture cannot be found (size ≤ 0) the error
  `Unable to find "<pic>"` is printed and the whole tag is skipped. This check happens **before** options are read, so
  condition options (`ifne` …) have no effect on images.
- Options (plus common options; unknown → `Unrecognised pic keyword`):

| Option | Effect |
|---|---|
| `abs X Y` | fixed position (X, Y) in page coordinates; the image takes no part in flow layout and is always considered in view |
| `alt "pic2"` | picture shown while hovered (default: the same picture) |
| `hflip`, `vflip` | draw mirrored left–right / top–bottom (flip value 4 + 1·hflip + 2·vflip) |
| `scale F` | drawn size and total size are W·F × H·F |
| `text "s" X Y` | overlay string at (x + X, y + Y) in the page font, colour = the page font's tint |
| `cvartext cvar X Y` | overlay the cvar's value (created as `"0"` if missing) at (x + X, y + Y), page font and tint |
| `setimage cvar` | the cvar names the picture to show (created holding the image's own picture name; if it is empty at page build it is set to that name) |
| `transient` | video-settings preview (below) |

- `noscale`: images placed on a bolt are normally scaled by s (W·s, H·s); `noscale` prevents it. Flow-layout images are
  never scaled by s.
- Drawing: picture = setimage cvar's value when it differs from the current picture (the new picture is loaded), else
  the image's picture; colour = `tint`; while hovered and without `setimage`: colour = `atint` and picture = `alt`.
  One stretched-picture call at (x, y), size ⌊W·scale⌋ × ⌊H·scale⌋, flip value per `hflip`/`vflip`. Then the text
  overlays, then the border.
- `transient`: whenever any of `menu_gamma`, `menu_contrast`, `menu_brightness`, `menu_modulate` changed, the picture
  is freed (SPEC-002 slot 0x24), the renderer's colour-correction table is rebuilt (slot 0xc0) with
  (`menu_gamma`, `menu_brightness`, `menu_contrast`, `menu_modulate` × 0.5), the picture is loaded again, and the table
  is rebuilt with (`vid_gamma`, `vid_brightness`, `vid_contrast`, 1.0); the four cvars' changed flags are consumed.
  Effect: the picture previews the pending video settings.
- Keys: the key list. Mouse: SPEC-800 `inc`/`mod`.

### hbar — `<hbar A B …>`

- Progress bar driven by its cvar value clamped to 0…1. A and B are read as integers (C `atol`); if both read as 0
  (picture names), A is the frame picture and B the fill picture, W×H = frame picture size and fw = fill picture width
  (*picture bar*); otherwise it is a *box bar* of W = A, H = B (a box bar with W or H = 0 is not created).
  Tint and atint: page font.
- Options: `invisible` (draw nothing, border included, while the value is 0), common options; unknown →
  `Unrecognised hbar keyword`. A picture bar is not created when a picture size is not positive.
- Drawing with value v: box bar → if v ≠ 0, a shaded box (SPEC-002 slot 0x78) at (x, y) of ⌊W·v⌋ × H in `tint`;
  picture bar → n = ⌊W·v / fw⌋ copies of the fill picture at (x + i·fw, y), i = 0…n−1, then the frame picture at
  (x, y) on top, both opaque white (unscaled picture call, slot 0x48).
- Test: `<hbar 100 8 cvar menu_loading>` with `menu_loading 0.5` draws a 50×8 box; with `1.7` a 100×8 box.

### vbar — `<vbar …>`

- Vertical scroll bar of the page. Pictures `misc/vbar` (track) and `misc/vend` (end cap, ew×eh). W = 16,
  H = frame inner height; it sits at the right edge of the frame content (x = content width − 16 − width of an
  already existing vbar), y = 0, and does not take part in flow layout or scroll with the page. The content width used
  by every later widget shrinks by 16. Options: common options; unknown → `Unrecognised vbar keyword`.
- Thumb length L = (inner height − 6)·(inner height − 2·eh) ÷ content height (integer). If inner height − 2·eh ≤ 0,
  inner height − 6 ≤ 0 or the content height ≤ 0 at page build: error `Cleanup : Page too small to place vbar`, the bar
  is removed. L is recomputed when the page content grows.
- Drawing (`tint`): top cap (x, y) ew×eh; bottom cap (x, y + H − eh) mirrored top–bottom; track (x, y + eh) ew×(H − 2·eh);
  thumb = shaded box at (x + 3, y + eh + 2 + scroll·(H − 2·eh − 2) ÷ content height), size 10 × L.
- Mouse (press/drag, mouse offset my from the bar top): my < eh → scroll − 4; my > H − eh → scroll + 4; otherwise the
  thumb centre jumps to the mouse: scroll = ((max(my, eh) − eh)·H ÷ (H − 2·eh) − L/2)·content height ÷ H; the bar then
  keeps the mouse captured while the button is held. Scroll is clamped by SPEC-800.
- Test: frame inner height 300, `misc/vend` 16 pixels high, content height 600 → L = 294·268 ÷ 600 = 131.

### ticker — `<ticker "text" …>`

- Horizontal marquee. Page font, H = FH, W = frame content width (normally overridden by `width`).
- Options: `speed N` (integer, default 1), `delay F` (seconds, default 1.0), common options; unknown →
  `Unrecognised ticker keyword`. A ticker on a bolt without `noscale` gets W = ⌊W·s⌋, its delay multiplied by s
  (faithful to the original) and its horizontal bolt offset multiplied by s.
- Not drawn while the window is still waiting for its opening animation (SPEC-801 `waitanim`).
- Position: t = seconds since the menu page was loaded; p = ⌊(t − delay)·speed·10⌋; offset = (p mod (SW(text) + W)) − W
  with the remainder taking the sign of p. The text is drawn in `tint` at (x − offset, y), clipped to the widget
  rectangle intersected with the frame content area (only the widget rectangle on `global` pages). Speed unit:
  10 pixels per second.
- Test: speed 3, delay 1, W 200, text width 300, 2.5 s after opening: p = 45, offset = −155, text starts at x + 155.

### hr — `<hr …>`

- Forces a line break before and after itself (identical to `<hbr>`, SPEC-800). W = content width (frame inner width −
  vbar width), H = 8, line length = W, thickness 1.
- Own options only: `width N` (line length), `line N` (thickness), `tint T` (sets tint and atint), `height N` (raises H).
  Other words are ignored silently; common options (key, border, …) are **not** available.
- Drawing: a thick 2D line (SPEC-002 slot 0x7c) from (x + (totalW − length)/2, y + totalH/2) to that x + length, same
  y, of the given thickness, in `tint` (integer halving).

### blank — `<blank W H …>`

- Invisible W×H hot spot; common options; unknown → `Unrecognised blank keyword`. Draws only its border (if any).
  Keys: the key list.

### selection — `<selection "cellpic" W H weapons|items|ammo|misc …>`

- Outfitting strip: a single row of cells; cell size cw×ch = size of `cellpic`; columns = W ÷ cw. Category keyword:
  `weapons` (default), `items`, `ammo`, `misc`; unknown → `Unrecognised selection keyword`. A second selection of the
  same category in one window prints `Error : Only one selection allowed per type per page` to the console.
- Console command `select <cvar>` (SPEC-801) adds an item to the selection whose category matches the cvar's inventory
  flag (weapon 0x0100, item 0x0200, ammo 0x0400, anything else misc; SPEC-302):
  - if the cvar's value ≠ 0 → `invalid` sound;
  - the cvar `<cvar>_info` holds `picture,amount,ammo,cells` (SPEC-302, e.g. `weapons/w_pistol1,1,a9mm,1`); if its
    picture does not exist, nothing happens; if used cells + cells > columns → `invalid`;
  - otherwise the cvar is set to `amount`; if `ammo` ≠ `none` and the ammo cvar is 0, the ammo cvar is set to the
    amount from `<ammo>_info`; the item is appended and occupies `cells` cells.
- Drawing at (x, y): items left to right; each item's picture centred horizontally in its cells·cw span at the top,
  in `tint`; if the item cvar's value > 1 it is printed (`%d`, font `title`) at the item's left edge in `dtint`; if its
  ammo cvar's value ≠ 0 it is printed at (left edge, y + 40) in `ctint`. Remaining cells show `cellpic` in `tint`.
- MOUSE1: the item under the mouse (cell = mouse offset ÷ cw) is removed: its cvar is set to 0 (the ammo cvar is not
  changed) and its cells are freed; if no removable item is there → `invalid`. `select` plays afterwards in both cases.
  Other keys: the key list (MOUSE1 does not run the key list).
- Test: `<selection "outfit/cell" 320 64 weapons>` with a 64-pixel cell picture has 5 columns; `select pistol1_cvar`
  with info `weapons/w_pistol1,1,a9mm,1` and `a9mm` = 0 sets the weapon cvar to 1 and `a9mm` to its info amount.

### popup — `<popup "labels" …>`

- Accepted (one per page; a second prints `Only 1 popup allowed per page` and the tag is skipped). Size
  (SW of widest label + 8) × ((FH + 4)·count + 8); options: common options, unknown → `Unrecognised popup keyword`.
- MOUSE2 moves it to the mouse position (kept inside the frame), but in 1.07f it is **never drawn and never changes
  its cvar**: the feature is unfinished. A reimplementation must not show it. No stock menu uses it.

## Constants

| Item | Value |
|---|---|
| Label shading factor | 0.7 on R, G, B |
| Disabled alpha | 0x60 |
| Setkey blink | alpha = (ms + 128) mod 256 |
| Slider default range / steps | 0…1; step 1.0 if range > 2 else 0.05 |
| Slider default pictures | `misc/bar`, `misc/button`, `misc/cap` (vertical: `misc/barv`, `misc/buttonv`, `misc/capv`) |
| Vertical bar | width 16, pictures `misc/vbar`, `misc/vend`, thumb inset 3 px, thumb width 10, step 4 px |
| Ticker | default speed 1 (= 10 px/s), default delay 1.0 s |
| hr | height 8, thickness 1 |
| Input cursor | `_`, period `cursor_blink_speed` (default 500 ms), visible in the second half |
| Selection count offset | second number at y + 40 |
| Comma list buffer | 255 characters |

## Test vectors / how to verify

1. `<list "^MENU_DM_GENERIC_NO_YES^" bitmask 13 cvar menu_dmflags>` with `menu_dmflags 4100` shows `No`; MOUSE1 →
   `menu_dmflags` = 12292 and the label becomes `Yes`; MOUSE1 again → 4100.
2. `<list "16 Bit,8 Bit" cvar s_loadas8bit>` with `s_loadas8bit 5`: on opening, the cvar becomes `1` and `8 Bit` shows.
3. `<list "Half,Full" match "0.65,1.0" cvar g_movescale>` with `g_movescale 2`: shows `Half` and sets `0.65` on opening.
4. `<list "" files "maplists" "*" "lst" cvar sv_maplistfile>` lists `RavDM` for `maplists/RavDM.lst` and stores `RavDM`.
5. `<slider cvar m_cursor_speed min 4 max 50 step 2>` with value 60: on opening the cvar becomes 50; LEFTARROW over
   it → 48 with the select sound.
6. `<slider cvar joy_forwardthreshold step 0.05>`: range 0…1; clicking the middle of the bar sets ≈ 0.5 (not rounded).
7. `<input cvar menu_chat width 100 maxchars 4 global>`: typing `abcdef` leaves `abcd` in the cvar after each key.
8. `<ctext menu_room atext "Room: " invisible>` draws nothing while `menu_room` is empty; after `set menu_room x` it shows
   `Room: ` at 70 % brightness and `x` at full brightness.
9. `<image "nosuch">` prints `Unable to find "nosuch"` (developer print, SPEC-800) and nothing is placed.
10. `<hr width 200 line 2 tint red>` in a 560-pixel content area draws a red 2-pixel line from x + 180 to x + 380 at
    y + 4.
11. Ticker test in the ticker section; vbar thumb test in the vbar section.

Black-box checks: compare screenshots at 640×480 of `menu video` (sliders, lists, transient preview), `menu keys`
(setkey rows), the outfitting menu (selection) and the chat menu (input history: send two lines, press UPARROW).

## Open questions

- Exact client connection state at which latched-cvar widgets turn grey (check as described above).
- Clipboard paste: which characters end the pasted text.
- Bolted input fields: is the mouse area really unscaled? Check: a chat input on a bolt at 1024×768, hover its right half.
- `files` listing order when several paks/directories contribute names.
- Whether the horizontal slider width really uses the cap height (stock caps may be square, which hides the difference).
