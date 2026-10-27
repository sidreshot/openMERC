# SPEC-132 — `.fnt` bitmap fonts

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — the 7 `pics/menus/fonts/*.fnt` and their `.m32` pages: `pak0` font1, font2, font4, inttext; `zmain` font1, font2, font4. No game binary was consulted.
Implementation: `tools/formats/fnt.py` (page decoding via `tools/formats/m32.py`, SPEC-100)
Related: SPEC-130 (`<font type …>` in menus), SPEC-131 (character set of the texts)

## Purpose

Menu text is drawn from bitmap fonts. Each font is a glyph table `X.fnt` plus a texture page `X.m32` with the
same base name in `pics/menus/fonts/`. The glyph table gives, per character code, the glyph's rectangle on the
page, its drawn size and its baseline.

## Layout

No header. The file is an array of 28-byte records; record *i* describes character code **32 + i**. The record
count is `file size / 28`: 224 (codes 32–255) for font1/2/4, 32 (codes 32–63) for `inttext`. All integers and
floats little-endian.

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | float32 | `s0` | left edge of the glyph on the page, 0..1 of page width |
| 4 | 4 | float32 | `t0` | top edge, 0..1 of page height (rows top to bottom) |
| 8 | 4 | float32 | `s1` | right edge (exclusive) |
| 12 | 4 | float32 | `t1` | bottom edge (exclusive) |
| 16 | 4 | int32 | `width` | drawn width in pixels = horizontal advance |
| 20 | 4 | int32 | `height` | drawn height in pixels |
| 24 | 4 | int32 | `baseline` | rows from the top of the glyph to the baseline row; −1 in `inttext` |

A record of all zeros = no glyph (the space, 32, is always such a record).

### Stock fonts

| File | Page (SPEC-100) | Glyphs | Codes with glyphs | Cap / x height (rows) | Advance | Alpha |
|---|---|---|---|---|---|---|
| pak0 `font1` | 128×128 | 163 | 33–126, 159, 163, 169, 174, 191–255 | 7 / 5 | 3–9 | 0/255 (pixel font) |
| pak0 `font4` | 128×128 | 163 | same | 8 / 6 | 4–13 | anti-aliased |
| pak0 `font2` | 256×128 | 163 | same | 11 / 8 | 3–21 | anti-aliased |
| pak0 `inttext` | 128×128 | 10 | 48–57 (`0`–`9`) | 32×32 cells | 32 | coloured |
| zmain `font1`, `font4` (same `.fnt` bytes and same pixels; only the `.m32` name field differs) | 256×256 | 164 | 33–127, 159, 163, 169, 174, 191–255 | fixed 11-row cells, baseline 7 | 4–15 | anti-aliased |
| zmain `font2` | 256×256 | 164 | same | fixed 15-row cells, baseline 7 | 3–22 | anti-aliased |

- Pages are white (R = G = B = 255 where alpha > 0) with the glyph shape in alpha — except `inttext`, a green
  seven-segment LCD with dark unlit segments (RGB in the texture).
- The covered codes 159 Ÿ, 163 £, 169 ©, 174 ®, 191 ¿, 192–255 À–ÿ are exactly the printable Windows-1252
  letters/symbols the stock texts need; the Russian pages hold Cyrillic А–я at 192–255 (Windows-1251), with
  Ё/ё (168/184) missing.

## Semantics

Drawing a byte string (all **[data]** unless marked; checked by rendering, see Validation):

1. Byte *c* uses record *c* − 32. Bytes without a glyph (all-zero record) draw nothing.
2. The page rectangle (`s0`,`t0`)–(`s1`,`t1`) is drawn as a `width`×`height` pixel quad. In all pak0 records
   `(s1−s0)·page_width = width` and `(t1−t0)·page_height = height` exactly, and `s0`, `t0` fall on whole
   pixels. In zmain, 5 records (159, 163, 169, 174, 191) keep the original 128-pixel sizes while their
   rectangles are twice as large, i.e. the rectangle is scaled into `width`×`height`; these 5 rectangles point at
   unrelated parts of the new page (the localisation did not update them — visible as garbage in
   `zmain_font*_charset.png`).
3. The pen advances by `width` **[hyp]**. Rectangles already include blank columns, so no extra spacing is
   needed, but how many differs by font (counted over all glyphs): pak0 font1 and font4 have 1 blank column on
   each side (2-pixel gaps between letters; font1 `A` is 7 wide with ink in columns 1–5), pak0 font2 has 1 on the
   left and 0 on the right (1-pixel gaps), zmain fonts 0 left / 2 right. Rendered with advance = `width`, font2
   and font4 read with normal spacing and font1 looks slightly loose — an advance of `width − 1` cannot be ruled
   out from the data (open question 7).
4. Vertical placement: the glyph's top row is `baseline` rows above the pen's baseline row. Verified on glyph
   shapes in font1: `A` (h 8, baseline 6: ink rows 0–6), `a` (h 6, baseline 4), `g` (h 8, baseline 4, descender
   rows 5–6), `.` (baseline 0: the dot is on the baseline), `,` (baseline 0, tail below), `-` (h 4, baseline 2:
   dash 2 rows above the baseline), `_` (baseline 0, bar 2 rows below).
   `inttext` uses −1 everywhere: no baseline; drawn top-aligned **[hyp]**.
5. Colour: menus set `tint`/`atint` (0xAABBGGRR, SPEC-130); white glyphs are multiplied by it **[hyp]**.

Font type names used by menus (`<font type …>`): `small`, `medium`, `title`, `interface`. `interface` =
`inttext` **[hyp, strong]**: the font test page `menus/fonts.rmf` prints all character rows for small, medium
and title but only `"0123456789"` for interface, and `inttext` has only the ten digits. The assignment of
font1/font4/font2 to small/medium/title is UNKNOWN; by size font1 < font4 < font2, so small = font1,
medium = font4, title = font2 is the natural guess (open question 1).

The font test page also shows which characters menu text cannot contain: it writes spaces in place of `"`,
`<`, `>` and 127, and `.` in place of `^` and of codes without glyphs — `"`, `<`, `>` and `^` are markup in
SPEC-130/131.

## Validation

`python tools/formats/fnt.py validate --out <dir>`: **7 files parsed, 0 errors** (size multiple of 28; every
non-empty record has 0 ≤ s0 < s1 ≤ 1, 0 ≤ t0 < t1 ≤ 1, positive size, −1 ≤ baseline ≤ height; matching `.m32`
page parses under SPEC-100). Notes: the 15 zmain scaled records above (5 per font).

Renders (looked at): `openmerc-data/ui/fonts/<pak>_<font>_charset.png` (codes 32–255 in rows of 32, like
`fonts.rmf`), `<pak>_<font>_sample.png` (English/German/French sample; Russian + English for zmain;
digits for inttext), `<pak>_<font>_page.png` (raw page). In the samples, ascenders, descenders, commas,
hyphens and accented capitals sit correctly on a common baseline, which confirms rule 4.

## Open questions (with black-box checks)

1. Which file is `small`, `medium`, `title`. Check: open `menu fonts` (console) in the original at 640×480 and
   compare the screenshot with `fnt.py render` of each font (glyph shapes differ clearly: font1 is a pixel font,
   font2 is wide).
2. Advance of a space and of other empty records. Check: screenshot a menu line with spaces (`menu quit`) and
   measure the gap.
3. Line height used by `<br>`/`<hbr>` for each font (not in the file). Check: same screenshot, measure line pitch.
4. Scaling at resolutions other than 640×480 (whole-number scale or free?). Check: screenshots at 800×600 and
   1024×768.
5. How `inttext` is positioned (baseline −1) and where the game uses it (HUD counters?).
6. Whether the engine uses `width`/`height` or the rectangle size when they differ (only the zmain records
   above). Check: the © sign in a Russian-localised menu.
7. Horizontal advance (`width` or `width − 1`). Check: in a 640×480 screenshot of `menu quit` measure the
   distance between the left edges of two letters of known width (e.g. `MM`) in each font.
