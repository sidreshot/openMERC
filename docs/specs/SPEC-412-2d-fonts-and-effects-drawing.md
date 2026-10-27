# SPEC-412: 2D drawing, fonts and effects drawing (refresh module)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: ref_gl.dll, version 1.07f (the drawing behind the 2D, font, alpha-quad and quad-chain parts of the renderer
interface; screenshots, thumbnails and AVI capture)
Depends on: SPEC-002 (renderer interface: export slots, scene records, alpha-quad and quad-chain layouts and flags,
text colour table, font names), SPEC-100 (`.m32`), SPEC-132 (`.fnt`), SPEC-144 (`.raw`), SPEC-303 (save thumbnails),
SPEC-400 (client effects: who fills alpha quads and quad chains), SPEC-010 (Ghoul statistics). Forward references:
SPEC-410 (world rendering: fog volumes, translucent world surfaces, light sampling), SPEC-411 (model rendering),
SPEC-413 (renderer cvars).

## Purpose

SPEC-002 fixes the binary interface. This spec gives the observable drawing rules behind it, so that a clean renderer
produces the same pictures:
- render state and exact geometry of every 2D call, clipping rules;
- proportional fonts: which file is which font, line heights, advances, colour handling;
- world labels (names above players);
- alpha quads (billboards, oriented quads, beams), quad chains and their level-of-detail reduction;
- the depth-sorted translucent pass that draws alpha quads, quad chains, translucent world surfaces and all entities;
- full-screen overlays (night vision, screen tint);
- `screenshot`, `rawshot`, save-game thumbnails and the `avi` recorder.

It does not repeat SPEC-002 tables. Section numbers below are referenced by other specs.

## Interface / layout

Conventions used below:
- Screen coordinates: pixels, origin top-left, x to the right, y down. W × H is the current video mode size.
- Texture coordinates (s, t): (0, 0) is the first stored texel row and column. `.m32` stores rows top to bottom
  (SPEC-100), so t = 0 is the **top** of the picture.
- "Blend normal" = blend factors (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA). "Blend additive" = (GL_ONE, GL_ONE).
  "Blend darkening" = (GL_ZERO, GL_ONE_MINUS_SRC_COLOR). "Blend multiply" = (GL_DST_COLOR, GL_ZERO).
- "Modulate" = texture colour × vertex colour (GL_MODULATE). "Replace" = texture colour only (GL_REPLACE).
- "Alpha test" = fragments pass only if alpha > 0.05 (GL_GREATER, 0.05). This is the renderer's default alpha test
  (Quake II used 0.666).
- trunc(x) = conversion to integer by truncation toward zero.
- Camera axes: forward F, right R, up U, from the refdef view angles in the Quake II way; E = view origin.

New fixed data introduced here (renderer constants, not stored in files):

**Font table** (renderer-internal; the `.fnt`/`.m32` files of SPEC-132 supply glyphs only)

| Font name | Files (`.fnt` + `.m32`, SPEC-132) | First code | End code (exclusive) | Line height | Ascent |
|---|---|---|---|---|---|
| `small` | `pics/menus/fonts/font1` | 32 | 256 | 10 | 8 |
| `medium` | `pics/menus/fonts/font4` | 32 | 256 | 12 | 10 |
| `title` | `pics/menus/fonts/font2` | 32 | 256 | 18 | 14 |
| `interface` | `pics/menus/fonts/inttext` | 32 | 64 | 32 | 24 |

## Behaviour

### 1. Render-state model

The renderer keeps blending, alpha test, depth test, face culling, fog and polygon offset in a deferred state cache:
a call that "turns blending on" takes effect at the next primitive. Two consequences matter for matching pictures:
- **State left behind.** Some 2D calls leave blending on or the texture mode changed (marked "leaves" below). The next
  call that does not set that state itself inherits it.
- **Colour.** Every textured 2D call sets the vertex colour from its RGBA argument unless stated otherwise; the
  texture mode is modulate unless stated otherwise.

### 2. 2D mode

**Entering 2D after the 3D view** (end of RenderFrame and export slot 0xb4):
- orthographic projection over the whole window: x 0…W left to right, y 0…H top to bottom, depth range −9999…9999;
- depth test off, face culling off, blending off, alpha test on, colour white;
- slot 0xb4 also turns depth writes off and forgets which textures are bound.

**Slot 0xb8 (`flag`)** prepares 3D drawing inside a 2D screen (menu model views):
- always: depth test on, depth writes on, flat shading, back-face culling selected, texturing on;
- only if `flag` ≠ 0: the current projection is saved and replaced by a perspective projection with vertical field
  of view 33.75084377109428°, aspect 1.3333, near plane `r_nearclipdist`, far plane `r_farclipdist`; whether 2D
  clipping (section 4) was enabled is remembered and clipping is disabled.

**Slot 0xb4 (`restore`)**: with `restore` ≠ 0 the projection saved by 0xb8(1) is restored and clipping is re-enabled if
it was enabled at that time. Without it, only the 2D state above is applied (the projection is left as it is).

### 3. 2D primitives

All quads are emitted in the corner order top-left, top-right, bottom-right, bottom-left unless stated otherwise.
"Pic" = image registered as type *pic* (SPEC-002); a missing pic prints `Can't find pic: <name>` and draws nothing.

| Slot | Geometry | Texture coordinates | State |
|---|---|---|---|
| 0x48 pic | (x, y) to (x + w, y + h), w × h = the image's level-0 size | (0,0) (1,0) (1,1) (0,1) | culling off, flat shading, modulate, blend normal. **Leaves blending on.** |
| 0x4c scaled pic | size trunc(w·sx) × trunc(h·sy) at (x, y) | (0,0)…(1,1) | blend normal; afterwards replace, blending off |
| 0x50 pic part | size trunc(\|r−l\|) × trunc(\|b−t\|) at (x, y) | s from l/w to r/w, t from t/h to b/h (no half-texel offset; r < l or b < t mirrors) | blend normal; afterwards replace, blending off |
| 0x54 stretch pic | (x, y), (x+w, y), (x+w, y+h), (x, y+h) — negative w or h mirrors | rotation table of SPEC-002; rotation values other than 0–3 are undefined | blend normal, modulate; afterwards blending off (modulate stays) |
| 0x58 char | 8 × 8 at (x, y); nothing for code 32 or y ≤ −8 | column c mod 16, row c div 16 of the 16 × 16 conchars grid: s = col/16 … col/16 + 1/16, t likewise | modulate, culling off. **Blending is not changed**: after entering 2D it is off, so glyph edges are alpha-tested, not blended, unless an earlier call left blending on. |
| 0x5c / 0x60 / 0x64 strings | consecutive 0x58 glyphs 8 pixels apart | as 0x58 | as 0x58 |
| 0x6c float char | 8 × 8 at the fractional position (x, y); nothing for code 32 or y ≤ −8. **The scale argument is ignored.** | as 0x58 | blend normal, smooth shading, modulate; afterwards blending off, flat shading, replace |
| 0x70 tile clear | the rectangle | s = x/64 … (x+w)/64, t = y/64 … (y+h)/64 (screen-aligned repeat) | culling off; **colour, texture mode and blending are inherited** from the previous call |
| 0x74 fill | the rectangle, untextured | — | flat, blend normal; afterwards blending off |
| 0x78 bevel box | filled rectangle as 0x74 (smooth shading), then four 1-pixel lines (0x7c): right edge (x+w, y+h)→(x+w, y) and bottom edge (x+w, y+h)→(x, y+h) in the **dark** colour, top edge (x, y)→(x+w, y) and left edge (x, y)→(x, y+h) in the **light** colour | — | dark = (min(255, trunc(r·0.7)), …), light = (min(255, trunc(r·1.3)), …) for r, g, b; alpha unchanged |
| 0x7c line | rectangle of thickness `width` around the segment, extended by width/2 beyond **both** end points (square caps). Corners, with h = width/2, d = unit direction p1→p2 and n = (−d_y, d_x): p1 − h·d + h·n, p2 + h·d + h·n, p2 + h·d − h·n, p1 − h·d − h·n | — | untextured, flat, blend normal; afterwards blending off |
| 0x84 fade | (0, 0) to (W, H), untextured | — | blending on with the current blend factors (normal in practice); afterwards off |
| 0x88 polygon | polygon through the n vertices, z included | — | depth test on (GL_LESS against the existing depth buffer), culling off, smooth, untextured, blend normal; afterwards depth test and blending off. **Texturing is left disabled**, so following textured 2D calls draw untextured until something re-enables texturing (the engine never calls this slot). |

### 4. 2D clipping (slots 0x8c–0x9c)

- When cvar `gl_scissor_broken` ≠ 0, all five calls do nothing.
- 0x8c (x, y, w, h): ignored if w < 0 or h < 0; otherwise remembers the rectangle, sets the hardware clip rectangle
  to window rows H − y − h … H − y and columns x … x + w, and enables clipping.
- 0x90: remembered rectangle := (0, 0, W, H); clip set to the whole window; clipping disabled.
- 0x94 (x): if x ≥ remembered left edge, clip to columns left … x (rows unchanged). Does not enable clipping.
- 0x98 (x): if x ≥ remembered left edge, clip to columns x … left + width.
- 0x9c: clip to the remembered rectangle again.

### 5. Console text and colour codes

- Slots 0x60/0x64 treat **every** byte 1–31 as a colour code (including 10 and 13); `maxlen` counts drawn glyphs;
  code bytes are not counted.
- Slot 0x5c draws at most `maxlen` glyphs; `maxlen` 0 draws nothing; a negative `maxlen` draws up to the terminating
  zero.
- The table colours with alpha < 255 (entries 9, 11, 12, 14–18, 21) are not blended by slots 0x58–0x64 unless an
  earlier call left blending on; their alpha then only affects the alpha test (derived; UNVERIFIED in game).

### 6. Proportional fonts (slots 0xc8, 0xcc, 0xd0)

**Loading.** At renderer start-up the four fonts of the font table are loaded: `<file>.fnt` through the file system
and `<file>.m32` as an image of type *font*. A missing `.fnt` is a **fatal** error, `Unable to find : <file>.fnt`.
A missing page image draws glyphs with the default "notexture" pic. Renderer shutdown releases them.

**Lookup.** The font argument is matched exactly (case-sensitive) against the four names. Unknown name: slot 0xc8
draws nothing, slots 0xcc and 0xd0 return 0.

**Layout of one call to slot 0xc8 (x, y, text, colour, font, keep):**
1. Colour: if `keep` = 0 the colour argument becomes the current colour and is remembered; if `keep` ≠ 0 the
   remembered colour is used (the colour current at the end of the previous call, including colour codes). Before
   any call the remembered colour is opaque white.
2. State: culling off, modulate, blend normal (blending is **left on** afterwards).
3. Pen: x₀ = x, baseline row b = y + ascent (font table).
4. For each byte c:
   - 1–9, 11, 12, 14–31: current colour := text colour table entry c (SPEC-002); also remembered.
   - 10: b := b + line height, pen x := x₀.
   - 13: ignored.
   - 32: pen x += line height / 2 (integer division): `small` 5, `medium` 6, `title` 9, `interface` 16.
   - first code ≤ c < end code: glyph record c − 32 of the `.fnt`; if its width is 0, the record of `.` (46) is used
     instead (width and drawing). The glyph is drawn as a quad of the record's width × height pixels with its top row
     at b − baseline (record field) and its left edge at the pen; texture rectangle (s0, t0)–(s1, t1) of the record.
     Pen x += record width.
   - any other code (≥ end code): nothing drawn, no advance.
5. No scaling with screen resolution: glyphs are always drawn at their pixel size.

Consequences for SPEC-132's open questions: small = font1, medium = font4, title = font2, interface = inttext; the
advance is the record width (not width − 1); the drawn size is the record width/height, so an oversized texture
rectangle is squeezed into it; the line pitch is the table's line height, not a `.fnt` value. `inttext` glyphs, whose
baseline field is −1, are drawn with their top at y + 25 (derived; UNVERIFIED in game).

**Slot 0xcc (width)**: sum over bytes of: 32 → line height / 2; codes in range → record width (or `.`'s width if 0);
everything else (control bytes, newline, codes ≥ end) → 0. Lines are not separated: the result is the total of all
lines.

**Slot 0xd0 (height)**: the line height of the font table.

### 7. World labels (slot 0xac)

Given world point p, optional text and optional pic name, and an RGBA colour:
1. d = p − E; across = d·R, upward = d·U, depth = d·F. If depth < 0.01 nothing is drawn.
2. cx = view width / 2, cy = view height / 2 (integer division).
   sx = trunc(cx + across · cx / depth · 95 / fov_x), sy = trunc(cy − upward · cy / depth · 95 / fov_y).
3. Distance scale: k = 0.7 if depth > 256, else 1 − 0.3 · depth / 256.
   If sx < 0: sx := trunc(sx + 5k); if sx > 0: sx := trunc(sx − 5k). sy′ = trunc(sy − 20k).
4. Text: font `small`, height h. Drawn with slot 0xc8 at (sx − width/2, sy′ + h/2), **always opaque white** (the colour
   argument is not used for the text), keep = 0.
5. Pic: k₂ = 0.4 if depth > 256, else 1 − 0.6 · depth / 256. Size dw = trunc(pw·k₂), dh = trunc(ph·k₂) (pw = ph = −1
   if the pic is missing). Drawn with slot 0x54 at (sx − dw/2, trunc(sy′ − h/2 − dh/2 − 4k₂)), size dw × dh, tinted by
   the colour argument, rotation 0. Integer halves round toward zero.

### 8. Alpha quads

An alpha quad (SPEC-002 layout) is drawn either alone (sections 8 and 10) or as part of a quad chain (section 9).
Rules for one quad drawn alone:

**Blend mode** (first matching row):

| Flags | Blend | Alpha test | Colour sent |
|---|---|---|---|
| 0x0400 | darkening | off | (r·a, g·a, b·a) / 255², alpha 1 |
| 0x0200 | additive | off | (r·a, g·a, b·a) / 255², alpha 1 |
| neither | normal | on (unchanged) | bytes (r, g, b, a) |

Texture mode modulate. After the quad: blend factors back to normal, alpha test on.

**Lit (0x0008).** L = light colour at the quad origin, sampled like export slot 0xa4 with the quad's direction byte
(+0x3a). Darkening/additive: colour = (r·Lr·a, g·Lg·a, b·Lb·a) / 255². Normal: bytes
(trunc(r·Lr), trunc(g·Lg), trunc(b·Lb), a) with **no clamping**: values above 255 wrap modulo 256 (UNVERIFIED whether
light values above 1 reach this point in practice).

**Image.** On first use the texture name (+0x34) is registered as a *sprite* image and cached in +0x30. If it cannot
be loaded, the default "notexture" pic is cached instead and is never retried. A null name draws with "notexture".

**Decal offset (0x0100).** If cvar `gl_offsetfactor` or `gl_offsetunits` is non-zero, the quad is drawn with polygon
offset (factor, units) = (`gl_offsetfactor`, `gl_offsetunits`), defaults −2, −2. With both zero there is no offset.

**Fog.** The fog cache (+0x3c) is filled on first use with the fog volume of the world leaf containing the origin
(0 = none; 0 when the world has no fog volumes). A non-zero value draws the quad with that volume's fog, using fog
settings chosen for the blend mode (darkening, additive or normal; SPEC-410).

**Geometry.** Flag 0x0002 → billboard; otherwise flag 0x0004 → beam; otherwise nothing is drawn.

*Billboard.* Axes: up A and right B are the camera's U and R; with flag 0x0010 they come from the normal N at +0x20:
B = the unit vector perpendicular to N built from the coordinate axis along which |N| is smallest (that axis minus its
projection on N, normalised; first axis of x, y, z on ties), A = N × B. Half-extents: a = +0x0c along A (SPEC-400's
first dimension, "height"), b = +0x10 along B (second dimension, "width"). With c = +0x18 and s = +0x1c (rotation
cosine and sine), each corner (u, v) ∈ {(−a, −b), (a, −b), (a, b), (−a, b)} with texture coordinates (0,0), (0,1),
(1,1), (1,0) respectively is placed at origin + (u·c + v·s)·A + (v·c − u·s)·B.
So s runs along B (0 at −b) and t along A (0 at −a): the picture's **top row is at the −A (lower) edge**, i.e.
billboards show their image upside down relative to the camera's up (derived; UNVERIFIED in game with an asymmetric
sprite).

*Beam.* With P1 = origin, P2 = end point (+0x0c), half width w (+0x18) and side vector
S = normalise((P1 − E) × (P2 − P1)): corners P1 + wS (0,0), P1 − wS (0,1), P2 − wS (1,1), P2 + wS (1,0). The texture's
s axis runs along the beam.

**The hidden flag 0x0001 is never tested when drawing.** The renderer only sets it (section 9). A quad submitted with
0x0001 set is drawn like any other. This answers SPEC-400's open question: One Frame particles are visible for the one
frame in which they are submitted.

Flags 0x0080 and 0x0800 have no effect on a quad drawn alone.

### 9. Quad chains

A chain is drawn as a unit. Order of rules:
1. **Cull.** If the chain's bounding box is outside the view frustum (unless `r_nocull`), or the chain has no first
   quad, nothing is drawn.
2. **Level of detail.** T = sum of the frame's chain weights (section 10), F = cvar `gl_effectfill` (default 100).
   Only when T > F: drop = trunc((1 − F/T) × count), clamped to 0 … count, where count is the chain's quad count
   (+0x20).
   - count = 1: with probability 1 − F/T (a uniform random number in [0, 1) below 1 − F/T) the quad gets flag 0x0001
     and the chain is not drawn this frame.
   - otherwise the renderer walks the 1st, 3rd, 5th … quads, then the 2nd, 4th … quads. Each visited quad that does
     **not** carry the drawn mark 0x1000 is dropped (see below) and decrements `drop`; the walk stops when `drop`
     reaches 0.
   - "Dropped" = flag 0x0001 set and the fields +0x0c, +0x10 and +0x18 set to 0 (the quad collapses to a point).
   - If `drop` is still positive afterwards, during drawing every quad that follows a drawn quad is dropped and
     skipped, until `drop` reaches 0.
3. **Depth test.** If the first quad has 0x0800, depth testing is off for the whole chain.
4. **Shared settings from the first quad:** blend mode (section 8 table), image, fog volume.
   If the first quad is lit, one light value L is sampled at the point (centre x, centre y, **maximum** z) of the
   chain's bounding box, with the first quad's direction byte; all lit quads of the chain use this L.
5. **Batched chain (first quad has 0x0080):** the colour is set once from the first quad.
   - Billboards (first quad has 0x0002): each quad is one **triangle** with vertices origin (0, 0),
     origin + a·U (0, 1.25), origin + b·R (1.25, 0), using the camera axes (no orientation, no rotation). The quad's
     origin is a corner, not the centre.
   - Otherwise each quad is a beam (section 8).
6. **Normal chain:** each quad is drawn separately with its own colour (its own lit, additive and darkening flags,
   but the chain's blend mode and light value); the geometry type (billboard or beam) of the whole chain follows the
   **first** quad's flag 0x0002.
7. Every drawn quad gets flag 0x1000 ("has been drawn"). The renderer never clears it.
8. Afterwards: fog off, depth test back on (if it was turned off), blend factors normal, alpha test on.

Decal offset (0x0100) is not applied to chains.

### 10. Sorted translucent pass

After the opaque world (and its fog pass) the renderer draws, in this pass, **all** entities, all alpha quads, all
quad chains and the translucent world surfaces collected while drawing the world. Setup: modulate, blend normal on,
smooth shading, depth writes off.

**Collection.**
1. Alpha quads: a quad whose leaf index (+0x40) is ≥ 0 is skipped unless that world leaf's surfaces were marked
   visible this frame (leaf in the potentially visible set and inside the view frustum; for a leaf without
   surfaces the result is UNVERIFIED). Quads with flag 0x0040 are drawn **immediately**, in submission order, before
   everything sorted. Others get key = |origin − E|².
2. Quad chains: key = r_nearclipdist² (16 with the default 4) if the first quad has 0x0800; otherwise for each axis
   take, of (min − E) and (max − E) of the bounding box, the component with the smaller absolute value; key = squared
   length of that vector. Then the chain's weight (+0x1c) is **overwritten** with weight / √key and added to T.
   If cvar `gl_showeffectfill` ≠ 0, `Fill used is <T>` is printed every frame (`%f` format).
3. Translucent world surfaces: key = the smallest squared distance from E to any of the surface's vertices, but at
   most r_farclipdist².
4. Entities: key = 0 for RF_WEAPONMODEL (flag 0x4). Otherwise key = |P − E|² with P = origin for Ghoul and null-model
   entities and P = origin + centre of the model's bounding box (unrotated) for brush models.

**Order.** Everything is sorted by key, largest first (back to front). Equal keys have no defined order (an unstable
sort is used). Weapon models (key 0) and no-depth-test chains (key r_nearclipdist²) therefore come last.

**Drawing.**
- Alpha quads, chains and translucent world surfaces: depth writes off.
- Entities: skipped entirely when `r_drawentities` = 0. Otherwise depth writes on; with `gl_drawflat` ≠ 0 texturing is
  off around the entity. Ghoul entities are drawn by the Ghoul path with "fullbright" = (`r_fullbright` ≠ 0 or
  rdflags bit 0x4); null models as the placeholder; others as brush models (SPEC-411). After each entity the pass state
  (modulate, blend normal, smooth shading) is restored.
- At the end: flat shading, blending off, replace, depth writes on, and the frame-record scratch field (0x80) reset
  to 0.

### 11. Full-screen overlays of the 3D view

Drawn after the sorted pass, in this order, inside the 3D view with an identity model-view rotated to look along +x:
a quad at distance 15.9 spanning ±100 in both other axes (covers the view for any normal field of view).

**Night vision (rdflags bit 0x4).**
- A fade timer T starts at 0 whenever the bit is clear (the noise texture is then discarded). While the bit is set
  and T < 2, T grows by the frame's game-clock delta (refdef 0x4c), so it stops while the game is paused.
- Noise texture: 64 × 64 RGB, nearest filtering, repeating. It is regenerated every frame while T < 2. Once T ≥ 2 the
  previous texture is reused unless a uniform integer in 0…10 is below 2 (probability 2/11), or the previous one was
  a bright frame.
- Generation: bright = (a uniform integer in 0…10 is 0). Each texel: bright → (0, G, 0) with G uniform 220…255;
  otherwise (40, G, 40) with G uniform 160…210.
  While T < 2 all three channels are multiplied by m = T (T ≤ 1.5) or 3 − T (1.5 < T < 2), truncated; a green value
  whose product exceeds 255 is replaced by a uniform integer 230…255. So the effect fades in over 1.5 s of game time,
  overshoots to 1.5× brightness and settles to 1× at 2 s.
- Drawing: blend multiply (the framebuffer is multiplied by the noise colour), depth test and alpha test off, white
  colour, texture coordinates chosen anew every frame: s from u₀ to u₁ and t from v₀ to v₁ with u₀, v₀ uniform in
  [−16, −8] and u₁, v₁ uniform in [8, 16] (the 64-texel tile repeats 16–32 times across the view).

**Screen tint (refdef blend).** If the blend alpha is non-zero, the quad is drawn untextured with blend normal and
the blend colour (r, g, b, a as 0–1 floats), depth test and alpha test off.
Debug override: when cvar `r_warnings` ≠ 0, the view is in a non-solid leaf that belongs to a cluster, the world has
visibility data, and the previous frame exceeded a budget, the tint is replaced by:
- red channel 1 when more than 700 world polygons were drawn, contributing min(1, (polys − 700)/150 + 0.2)
- green channel 1 when the Ghoul triangle statistic (SPEC-010 method 43) exceeds 3500, contributing
  min(1, (tris − 3500)·0.001 + 0.2)
- blue channel 1 when `r_speeds` = 2 or `r_warnings` = 2 and the uploaded texture total exceeds the budget
  (4 194 304 texels if `gl_pictip` ≠ 0, else 16 777 216), contributing (excess / budget)·0.5 + 0.2
- alpha = min(1, sum of contributions); unused channels 0. (Texture total semantics: SPEC-413, UNVERIFIED.)

### 12. Screenshots, raw shots and thumbnails

**`screenshot`** (console command):
- creates directory `<user dir>/scrnshot`;
- picks the first free name `sof00.tga` … `sof99.tga` there; if none is free: `SCR_ScreenShot_f: Couldn't create a
  file`;
- reads the current framebuffer (whole window, read buffer as set at that moment) and writes an uncompressed
  true-colour TGA: 18-byte header with image type 2, width and height (16-bit little endian at offsets 12 and 14),
  24 bits per pixel, descriptor 0 (rows bottom to top), no ID, no colour map; then W × H pixels in B, G, R order,
  bottom row first. File size 18 + 3·W·H.
- No gamma or other colour change is applied. Prints `Wrote <path>` or `Error writing <path>`.

**`rawshot`**: directory `<user dir>/rawshot`, names `sof00.raw` … `sof99.raw`, content = a 128 × 128 thumbnail
(65 536 bytes) as below. Messages: `SCR_RawShot_f: Couldn't create a file`, `SCR_RawShot_f: Unable to allocate
memory`, `Wrote <path>`, `Error writing <path>`.

**Thumbnail (slot 0xd4, w × h):** the whole window is read as RGB. Output pixel (i, j), i = 0…w−1, j = 0…h−1, is the
window pixel at column (i·sx) div 65536 and row (j·sy) div 65536 counted **from the bottom**, where
sx = (W·65536) div w and sy = (H·65536) div h (integer division). Output is RGBA with A = 255, rows bottom to top.
This is the `.raw` layout of SPEC-144 (bottom-up rows).

**`gl_strings`** prints GL_VENDOR, GL_RENDERER, GL_VERSION and GL_EXTENSIONS, one per line.

### 13. AVI recording (`avi` command)

Registered at renderer start-up, removed at shutdown (which also stops a recording).

| Usage | Effect |
|---|---|
| no or unknown sub-command | prints `avi [ record < name > ] [ pause ] [ stop ]` |
| `avi record <name>` | stops a running recording first (printing `Record <n> frames`), then records to `<user dir>/<name>` (no extension added). Opens the standard Windows video-compression dialog to choose a codec. The stream is 10 frames per second, frame size W × H, 24-bit input. |
| `avi record` without name | only stops a running recording |
| `avi pause` | toggles pause; pausing prints `AVI Recording Paused`; if not recording prints `AVI is not recording` |
| `avi stop` | if recording: stops and prints `Record <n> frames` |

Failure messages (each aborts and releases the file): `ERROR: Could not create '<path>' AVI file`, `ERROR: Could not
create AVI stream`, `ERROR: Could not get AVI options` (dialog cancelled), `Could not create AVI compressed stream`,
`Could not set AVI format`, `Could not create AVI recording buffer`, `Could not write frame <n> of AVI`.

Capture timing: at the end of every frame, **before** the buffer swap (so the 2D overlay, console and menus are
included), if recording and not paused and at least 100 ms of real time (system millisecond clock) have passed since
the capture mark, the mark advances by exactly 100 ms and one frame is written (bottom-up, key frame). At most one
frame is written per rendered frame, so below 10 frames per second the video runs faster than real time; pausing
does not move the mark, so after a pause frames are written every rendered frame until the mark catches up.

## Constants

| Name | Value |
|---|---|
| Default alpha-test reference | > 0.05 |
| 2D orthographic depth range | −9999 … 9999 |
| Menu 3D view: vertical field of view / aspect | 33.75084377109428° / 1.3333 |
| Conchars glyph | 8 × 8 pixels, 16 × 16 grid |
| Tile clear repeat | 64 pixels |
| Bevel dark / light factors | 0.7 / 1.3 |
| Line cap extension | width / 2 at each end |
| World label distance scales | text offset k: 1 → 0.7 over 0…256 units; pic scale k₂: 1 → 0.4 over 0…256 |
| World label offsets | horizontal 5k toward x = 0, vertical 20k up, pic gap 4k₂ |
| World label projection constant | 95 |
| Batched billboard texture extent | 1.25 |
| Overlay quad | distance 15.9, half-size 100 |
| Night-vision texture | 64 × 64 RGB, 11 random draws per decision (0…10) |
| Night-vision normal texel | (40, 160…210, 40) |
| Night-vision bright texel | (0, 220…255, 0), probability 1/11 per regeneration |
| Night-vision fade | 2 s of game time, peak factor 1.5 at 1.5 s |
| Night-vision texture coordinate ranges | start −16…−8, end 8…16 |
| r_warnings budgets | 700 world polygons, 3500 Ghoul triangles, 4 194 304 / 16 777 216 texels |
| AVI rate | 10 frames per second, capture step 100 ms |
| Screenshot / rawshot names | `sof00`–`sof99` `.tga` / `.raw` |
| Thumbnail fixed-point scale | 65536 |

## Test vectors / how to verify

1. **Bevel box.** Slot 0x78 with (10, 10, 100, 20) and colour (100, 200, 250, 255) on a black screen: the fill is
   (100, 200, 250); the top and left border lines are (130, 255, 255); the right and bottom lines are (70, 140, 175).
2. **Line caps.** Slot 0x7c from (100, 100) to (200, 100), width 10: covered pixels span x 95…205, y 95…105.
3. **Float char scale.** Slot 0x6c with scale 2.0 draws the same 8 × 8 glyph as scale 1.0.
4. **Font metrics.** Slot 0xd0 returns 10 / 12 / 18 / 32 for `small` / `medium` / `title` / `interface`. Slot 0xcc of
   `"A B"` in `small` = width(A) + 5 + width(B) using font1's records. A byte 127 (zero record) measures as `.`.
5. **Font colour keep.** Call 0xc8 with text `"\x02red"` and colour white, then 0xc8 with `"x"` and keep = 1: the `x`
   is red. With keep = 0 it has the passed colour.
6. **Unknown font.** Slot 0xc8 with font name `Small` (capital S) draws nothing; 0xcc and 0xd0 return 0.
7. **World label.** View at the origin facing +x, 640 × 480 view, point (100, 0, 0), text `abc`: depth 100,
   across = upward = 0, k = 1 − 0.3 · 100/256 = 0.8828125, sx = trunc(320 − 5k) = 315, sy′ = trunc(240 − 20k) = 222.
   The text's left edge is 315 − width/2 (integer halving) and its top row is 222 + 5. The text is white even when the
   colour argument is red.
8. **Hidden flag.** Submit a single alpha quad with flags 0x0003 (hidden + billboard): it is drawn.
9. **Chain level of detail.** One chain of 10 billboards none of which has 0x1000, weight 200 at distance key 100
   (√ = 10, adjusted weight 20 > F when `gl_effectfill` = 10): T = 20, drop = trunc(0.5 × 10) = 5; quads 1, 3, 5, 7, 9
   (1-based) collapse and get 0x0001; `gl_showeffectfill 1` prints `Fill used is 20.000000`.
10. **Thumbnail sampling.** 640 × 480 window, 128 × 128 thumbnail: sx = 327 680, sy = 245 760; output column 127
    samples window column 635, output row 127 samples window row 476 from the bottom.
11. **Screenshot file.** At 640 × 480 the file is 921 618 bytes; bytes 0–17 are
    `00 00 02 00 00 00 00 00 00 00 00 00 80 02 E0 01 18 00`.
12. **Night vision fade.** With rdflags bit 4 set from game time 0, the green channel of the overlay texture at
    T = 0.5 is about half of the T ≥ 2 value; at T = 1.5 about 1.5×, with many texels saturated to 230–255.
13. **AVI.** `avi record test.avi`, pick "Full frames (uncompressed)", run 10 s at ≥ 30 fps, `avi stop`: the console
    reports about 100 frames; the file plays at 10 fps.

## Open questions

- Fog settings per blend mode for alpha quads and chains (defined with fog volumes in SPEC-410).
- Whether the save-game menu flips the bottom-up thumbnails when drawing them (engine side, SPEC-303). Verify with a
  probe renderer logging slot 0x54/0xa0 arguments while the load menu is open.
- Billboard texture orientation (section 8): confirm in game with an asymmetric sprite.
- Lit normal-mode alpha quads: can the light value exceed 1 so that colour bytes wrap? Test with a bright dynamic
  light next to a lit particle.
- Behaviour of the leaf visibility test for alpha quads in leaves without surfaces.
- Night-vision overlay and console text alpha behaviour are derived from state analysis; compare screenshots.
