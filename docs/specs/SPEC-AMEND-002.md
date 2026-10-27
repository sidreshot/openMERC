# SPEC-AMEND-002: Corrections to approved specs found while specifying the renderer
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: ref_gl.dll and SoF.exe, version 1.07f
Depends on: SPEC-410, SPEC-411, SPEC-412, SPEC-413 (the approved renderer specs that establish the corrected facts)
Amends: SPEC-002, SPEC-400, SPEC-010, SPEC-011, SPEC-012, SPEC-100, SPEC-102, SPEC-132, SPEC-144; editorial note on
SPEC-410/412/413

## Purpose

While the renderer was specified (SPEC-410 to SPEC-413), several statements in earlier approved specs turned out to
be wrong, incomplete, or open questions with a now-known answer. This document lists them so that the earlier specs can
be corrected. For every item it gives the old statement, the corrected statement and how to verify it.

Items are of three kinds:
- **Correction**: the old statement is wrong; an implementation that follows it differs visibly from the original.
- **Resolution**: the old statement was marked UNVERIFIED or was an open question; the answer is now known.
- **Refinement**: the old statement is true but incomplete in a way that matters for matching the original.

The new specs are the authority for the details; the sections they are taken from are named in each item.

## Interface / layout

No new binary layouts. All offsets below refer to tables already defined in the amended specs (SPEC-002 export
slots, SPEC-002 alpha-quad and quad-chain records, SPEC-011 render-parameter block).

## Behaviour

### 1. SPEC-002 renderer module interface

**1.1 Import slot 0x40, cvar change callback (Correction).**
- Old: "the renderer always passes null" as the fourth argument.
- Corrected: the renderer passes null for every cvar **except** `r_isf` and `r_iaf`. For these two it passes a
  callback that checks the value against the 43 format names of SPEC-413; an unknown name prints
  `Avaiable formats: ` (sic), an empty line and every valid name, and the cvar is set back to its default.
  An engine that never invokes the callback loses only this validation.
- Verify: `r_isf foo` in the original prints the format list and `r_isf` reads `GL_SOLID_FORMAT` afterwards.

**1.2 Export slot 0x6c, float character (Resolution).**
- Old: "whether [scale] has any effect is UNVERIFIED".
- Corrected: the scale argument is ignored; the glyph is always 8 × 8 pixels (SPEC-412 §3).
- Verify: call slot 0x6c with scale 2.0 and 1.0 through a probe renderer harness; the glyphs are identical.

**1.3 Export slot 0x78, bevel box (Resolution).**
- Old: "Shading formula UNVERIFIED".
- Corrected: after the fill, the top and left one-pixel borders use the light colour
  (min(255, trunc(1.3·r)), min(255, trunc(1.3·g)), min(255, trunc(1.3·b)), a) and the right and bottom borders the dark
  colour (trunc(0.7·r), trunc(0.7·g), trunc(0.7·b), a) (SPEC-412 §3).
- Verify: SPEC-412 test vector 1 — colour (100, 200, 250, 255) gives top/left (130, 255, 255) and right/bottom
  (70, 140, 175).

**1.4 Export slot 0x8c–0x9c, clipping switch (Resolution).**
- Old: "A renderer option can disable it (UNVERIFIED which cvar)".
- Corrected: cvar `gl_scissor_broken` ≠ 0 makes all five clip calls (0x8c, 0x90, 0x94, 0x98, 0x9c) do nothing
  (SPEC-412 §4).
- Verify: with `gl_scissor_broken 1`, scrolling menu lists are expected to draw past their frame.

**1.5 Export slot 0xac, world labels (Resolution and Correction).**
- Old: "Draws `text` (font `small`) and/or `pic` centred near it … Exact offsets UNVERIFIED."
- Corrected: placement and distance scaling are given in SPEC-412 §7. The colour argument tints **only the pic**;
  the text is always drawn opaque white.
- Verify: SPEC-412 test vector 7.

**1.6 Export slot 0xb8, argument (Resolution).**
- Old: "How `flag` is used is UNVERIFIED."
- Corrected: the slot always turns on depth test and depth writes, flat shading, back-face culling and texturing.
  Only when `flag` ≠ 0 does it save the current projection, set a perspective projection (vertical field of view
  33.75084377109428°, aspect 1.3333, near `r_nearclipdist`, far `r_farclipdist`), remember whether 2D clipping was on
  and turn clipping off. Slot 0xb4 with a non-zero argument undoes exactly this (SPEC-412 §2).
- Verify: a probe renderer logging calls from the engine's menu model view sees 0xb8(1) … 0xb4(1) pairs.

**1.7 Export slot 0xc0, colour-correction parameters (Resolution).**
- Old: "Parameter meanings UNVERIFIED."
- Corrected: the four floats are (gamma, brightness, contrast, scale), in this order. The table formula is in
  SPEC-413 §9; the defaults (0, 0.5, 0.5, 1) give the identity table. The renderer builds the table at start-up from
  `vid_gamma`, `vid_brightness`, `vid_contrast` and 1.0.
- Verify: SPEC-413 test vectors for the colour table.

**1.8 Export slot 0xa4, light sampling (Correction).**
- Old: "The value is the Quake II `R_LightPoint` output, exported."
- Corrected (SPEC-410 *Light sampling*):
  - a ray that hits no lit surface gives (0.1, 0.1, 0.1) static light, not black;
  - the static light is multiplied by the latched `gl_modulate` value and clamped to 1 per channel; after the dynamic
    lights are added the result is multiplied by `gl_modulate` **again** and clamped to 1;
  - static results are cached per 32-unit cell and direction, so all points of a cell return the first value computed
    in it while the cache entry lives (cache cleared when a map loads).
  - "No light data in the world → (1, 1, 1)" is unchanged.
- Verify: SPEC-410 test vector 11 (lit floor → (1, 1, 1); no hit → (0.1, 0.1, 0.1)).

**1.9 SetSky, `none` faces and rotation (Correction and Resolution).**
- Old: "A line equal to `none` (any case) leaves that face as the default 'notexture' image." Face order and rotation
  unit UNVERIFIED.
- Corrected: a face whose line is `none`, or whose image is missing, is **not drawn** (the colour buffer shows
  through). The face order is the Quake II order rt, bk, lf, ft, up, dn. The box rotates by `rotate` × game clock
  degrees about `axis` (degrees per second of the refdef game clock) (SPEC-410 *Sky*).
- Verify: SPEC-410 test vector 10 (`sky none` shows the uncleared buffer, not the notexture pattern).

**1.10 Fonts (Correction and Resolution).**
- Old: "A missing `.fnt` file prints `Unable to find : <file>`."
- Corrected: a missing `.fnt` is a **fatal error** at renderer start-up, message `Unable to find : <file>.fnt`
  (SPEC-412 §6).
- Old: `interface` grid parameters "32, 64, 32, 24 have UNVERIFIED meaning (probably first character, character count,
  cell width, cell height)".
- Corrected: they are first code 32, end code 64 (exclusive), line height 32, ascent 24. All four fonts have such a
  row (`small` 32/256/10/8, `medium` 32/256/12/10, `title` 32/256/18/14); see the font table of SPEC-412.
- Old: slot 0xc8 `keep` "(UNVERIFIED)".
- Corrected: confirmed. With `keep` ≠ 0 the colour remembered from the end of the previous call (including colour
  codes) is used; before the first call it is opaque white.
- Verify: SPEC-412 test vectors 4 and 5; removing `pics/menus/fonts/font1.fnt` from the search path stops the renderer
  from starting.

**1.11 Render flags (`entity.flags`) (Corrections and Resolutions).**
- RF_MINLIGHT (0x1). Old: "Ghoul light raised to at least 0.1 per component." Corrected: the colour becomes
  (0.1, 0.1, 0.1) **only when all three components are ≤ 0.1**; otherwise it is unchanged (SPEC-411 §5 step 9).
  Verify: a model with RF_MINLIGHT in light (0.05, 0.5, 0.5) keeps 0.05 red.
- RF_VIEWERMODEL (0x2). Old: "Not drawn in the normal pass." Corrected: for a Ghoul entity, when shadows are on and
  `ghl_player_shadow` ≥ 0.5, only its **shadow** is drawn; when shadows are off or `ghl_player_shadow` < 0.5 nothing
  is drawn; if the instance does not cast a shadow at all (not a character model and no flag 0x1000), the entity is
  drawn **normally** (SPEC-411 §5 step 6, §7). Verify: in first-person view with `ghl_shadows 2` the player's own
  shadow is visible but not the player model; with `ghl_player_shadow 0` neither is drawn.
- 0x1000. Old: "Passed to the Ghoul system as a boolean. Meaning UNVERIFIED." Corrected: "this entity casts a shadow"
  even if its model is not a character model (SPEC-411 §7).
- Bit 28 (value 2²⁸, the last row of the render-flag table). Old: "use plain point lighting, without directional probing (UNVERIFIED)". Corrected:
  ambient-only lighting — colour from the light point at the origin, no probe vector, no world lights, no specular
  (SPEC-411 §5 step 8).
- Which game entities set 0x1000 and bit 28 remains open (SPEC-411 open question 4).

**1.12 Alpha-quad flags (Corrections and Resolutions).**
- 0x0001. Old: "Hidden. The renderer also sets it on quads dropped for level of detail." Corrected: the renderer
  **never tests** this flag when drawing; a quad submitted with it is drawn like any other. The renderer only sets it,
  on chain quads it drops for level of detail, and then also sets the quad's fields +0x0c, +0x10 and +0x18 to 0 so the
  quad collapses to a point (SPEC-412 §8, §9). Verify: SPEC-412 test vector 8.
- 0x0080. Old: "Chain head: the whole chain is drawn as one batch (UNVERIFIED)." Corrected: with 0x0080 on the first
  quad, the colour is set once from the first quad, and billboard chains are drawn as one **triangle** per quad
  (corner at the quad origin, texture coordinates up to 1.25) instead of a rotated quad; beam chains are unchanged
  (SPEC-412 §9 rule 5). On a quad drawn alone the flag has no effect.
- 0x0100. Old: "Depth offset, decal style (when enabled by renderer options; UNVERIFIED which)." Corrected: polygon
  offset (`gl_offsetfactor`, `gl_offsetunits`), default (−2, −2), applied when either cvar is non-zero; never applied
  to quad chains (SPEC-412 §8, §9).
- 0x0800. Old: "Chain head: drawn without depth test, sorted as farthest (UNVERIFIED)." Corrected: depth testing is
  off for the whole chain, and the chain's sort key is `r_nearclipdist`² (16 with the default 4). Since the pass draws
  the largest key first, such chains are drawn **almost last** (only weapon models, key 0, come after them), not
  first. On a quad drawn alone the flag has no effect (SPEC-412 §9, §10). Verify: a "No ZBuffer" effect (SPEC-400) in
  front of a translucent window is drawn over it.
- 0x1000. Old: "Set by the renderer: drawn this frame." Corrected: set on every drawn quad and **never cleared** by the
  renderer, so it means "has been drawn at least once". Level-of-detail dropping skips quads that carry it, so quads
  already on screen are not dropped (SPEC-412 §9).

**1.13 Quad-chain level of detail (Resolution).**
- Old: "Exact formula UNVERIFIED."
- Corrected: SPEC-412 §9 rule 2 and §10 (weight divided by the square root of the chain's sort key, T = sum of the
  frame's weights, drop = trunc((1 − F/T) × count) when T > F = `gl_effectfill`).
- Verify: SPEC-412 test vector 9.

**1.14 Image type 8 (Resolution).**
- Old: "8 | UNVERIFIED".
- Corrected: environment map, assigned from `.m32` `kind` bit 0x8 (bit 0x2 gives detail, type 5; 0x8 wins)
  (SPEC-413 §8, SPEC-410 *Detail and environment pass*).

**1.15 Open questions of SPEC-002 now answered.** Ghoul method table: SPEC-010/011. Slot 0xc0, 0x6c, 0xb8, 0x78:
items 1.2–1.7 above. Image type 8 and `.m32` flag bits: item 1.14 and SPEC-410/413. The remaining SPEC-002 open
questions (lump descriptor contract, entity fields with no renderer reader, frame-record field 0x00, print level 2,
file-load third argument, rdflags 0x1, A3D argument types) are not affected; SPEC-410 *A3D geometry export* describes
the A3D call in more detail.

### 2. SPEC-400 client effects

**2.1 Hidden flag and One Frame particles (Resolution).**
- Old (open question): "If the renderer skipped such quads, One Frame particles … would never be visible — verify."
- Corrected: the renderer never skips quads with flag 0x0001 (item 1.12). One Frame particles are visible for the one
  frame in which they are submitted. Quads that the renderer drops for level of detail are collapsed to a point
  before the chain is drawn (SPEC-412 §9), so they are not visible in the frame of the drop.
- Verify: muzzle-flash effects (One Frame emitters) are visible in screenshots of the original.

### 3. SPEC-010 Ghoul interfaces

**3.1 "Lights from models" list (Resolution).**
- Old (open question): "What fills the 'lights from models' list read by methods 28/29 (no writer found statically)."
- Corrected: while an instance whose object or bolt-ons contain light nodes is drawn, its active light nodes whose
  names begin with `WORLD` are exported to that list; the renderer appends them to its dynamic lights in the **next**
  frame (SPEC-411 §3 and §6.5). Models without light nodes add nothing.

**3.2 `ghl_shadow_tilt` (Refinement).** SPEC-010 lists it with default ".25"; it is never read in 1.07f and has no
effect (SPEC-411 cvar table).

### 4. SPEC-011 Ghoul instance interface, render-parameter block

**4.1 Offset 0x08, light colour (Correction).** "raised to ≥ 0.1 for RF_MINLIGHT" → replaced by the rule of item 1.11
(all three components ≤ 0.1 → (0.1, 0.1, 0.1)).

**4.2 Offset 0x14, directional light vector (Refinement).** "directional light vector (× 500)" holds for the probe
lighting method (`ghl_light_method` exactly 1), where the vector is expressed in the **entity's own axes**
(SPEC-411 §5 step 8). For RF_VIEWERMODEL entities it is replaced by (−25·up.x, −25·up.y, 0), up = the view's up
vector; the shadow-only drawing uses it to shift the player's own shadow (SPEC-411 §7).

### 5. SPEC-012 Ghoul runtime

**5.1 `gserver LIGHT_AMBIENT` default (Correction).**
- Old: "LIGHT_AMBIENT … default 1.0" (lighting section, `gserver` table and Constants).
- Corrected: the default is **0.6** (clamp 0–1 unchanged). It is the ambient factor of the software lighting
  (SPEC-411 §6.1).
- Verify: in the original, `gserver LIGHT_AMBIENT` with no value prints `LIGHT_AMBIENT = 0.60`.

**5.2 `gserver LIGHT_SMOOTH` meaning (Resolution).** Default 1.0, clamp 0–6 as stated; it is the fade time in seconds
of lights switching on and off for an instance (SPEC-411 §6.3).

**5.3 Shadow direction smoothing (Correction).**
- Old: shadow light direction "smoothed per instance: new = old·0.95 + current·0.05, or 0.8/0.2 for shadow-only
  drawing".
- Corrected: the direction is updated only when the render-parameter block's "use OpenGL-style lights" byte is set
  (`ghl_light_method` ≥ 2) **and** the entity is not drawn shadow-only. The 0.8/0.2 branch is therefore never reached;
  with `ghl_light_method` < 2, and always for the player's own shadow, the direction stays (0, 0, 1) (shadow straight
  down) (SPEC-411 §7).
- Verify: with `ghl_light_method 1`, model shadows fall straight down regardless of nearby lights; with 2 they lean
  away from lights.

**5.4 Light falloff details (Resolution).** The world-light and dynamic-light falloff left as "Details UNVERIFIED" are
given in SPEC-411 §6.3.

### 6. SPEC-102 `.ifl` image lists (Resolution of open questions 1–4)

From SPEC-411 §10:
1. 3ds Max extras are not supported: a line is one name (trailing white space removed, extension cut); anything else on
   the line becomes part of the name, so such entries are not found.
2. A missing entry keeps its index slot and is drawn with `GHOUL/COMSKIN/DEFAULT.TGA`; the console shows
   `Ghoul :<dir><name> not found`.
3. Only `<name>.TGA` is searched; `.m32` is never used for Ghoul textures.
4. There is no list frame rate: the image index is a per-frame animated value of the material slot plus the
   instance's skin index, clamped to the list (no wrap-around).

### 7. SPEC-100 `.m32` (Resolutions; one implementation-relevant correction)

Answers from SPEC-410 (`.m32` fields as interpreted) and SPEC-413:
- `alt_name`: loaded instead of the texture when `gl_pictip` ≠ 0, or when `kind` bit 0x4 is set and the parental-lock
  cvar `lock_textures` ≠ 0.
- `kind` bit 0x4: parental-lock content (graffiti, posters, signs).
- flag 0x1000: when it is set in the texinfo flags (the stock maps carry the `.m32` bit there), the texture animates
  through its `nexttexinfo` chain at the `.m32` `value` (low byte) frames per second.
- `unknown_2d8`: one extra mip level dropped when `gl_pictip` ≠ 0.
- `detail_shift_x/y`: detail scroll speed in detail-texture widths per second of game clock.
- `scale_x/y`, `unknown_36c`, `damage_value`, material id, `anim_next`: not used by the renderer.
- **`detail_blend_src/dst` are ignored by the renderer**; the second pass always blends with (destination colour,
  source colour). SPEC-100's reading of 0x306/0x300 as these GL factors matches the stock detail textures, but an
  implementation that used the fields would draw the second pass of the 375 textures whose second-pass texture is an
  environment map and whose blend fields are 0/0 with factors (zero, zero), i.e. black. Do not use the fields.

### 8. SPEC-132 `.fnt` fonts (Resolutions and one correction)

From SPEC-412 §6:
- small = font1, medium = font4, title = font2, interface = inttext.
- The advance is the record `width` (not width − 1); a space advances by half the font's line height.
- The line pitch is a renderer constant per font (10, 12, 18, 32), not a value in the file.
- No scaling with screen resolution.
- When `width`/`height` and the texture rectangle disagree, the rectangle is drawn squeezed into width × height.
- Correction of the `[hyp]` "inttext drawn top-aligned": glyphs with baseline field −1 are drawn with their top row at
  y + 25 (ascent 24 + 1), not at y (derived; UNVERIFIED in game, see SPEC-412).

### 9. SPEC-144 terrain sets (Corrections of hypotheses)

From SPEC-410 *Terrain sets*:
- The version check is **only** the first byte, which must be `"` (0x22). The second byte is an "active" flag: 0 loads
  the set but never draws it. (Old: the uint16 0x0122 at offset 0 was taken to be one version value.)
- Header 0x06 (40) is the number of rows in the scrolling ring; 0x0A (23) is the number of columns per row and the
  number of column records per group; 0x0E (1) is the number of groups; 0x12 (1) the number of sprite lists; 0x1A
  (3500) the scroll speed in units per second of game clock; 0x1E–0x32 hold fog start, fog end, fog colour and a clear
  flag. The header is 88 bytes; a 52-byte group record follows at 0x58 (row spacing 900, lateral offset 8300, height
  offset 150, …), so the 68-byte records start at 0x8C as SPEC-144 observed.
- The 68-byte records are **columns**, not features along the track: field 0 is a texture-slot index (not a feature
  type), 8–15 is a lateral range (not start/end distance along the track), 16–23 a height range, 32 a flag word
  (bit 0x2 mirrors the texture), 36–51 and 52–67 lateral and height waves (frequencies in radians per second,
  amplitudes in units).
- The sprite section is parsed and discarded: terrain sprites are never drawn, so the missing `.sp2` files have no
  effect.
- The texture section holds, per texture slot, a count followed by entries (name, selection threshold, s and t
  divisors, fixed s and t repeat counts, whole-repeat byte).

### 10. Editorial note on SPEC-410, SPEC-412 and SPEC-413

These specs sometimes write "rdflags bit 2" and "rdflags bit 4". They mean the flag **values** 0x2 (no world model) and
0x4 (night vision) of SPEC-002's frame-flag table, not bit numbers 2 and 4 (values 0x4 and 0x10). Read "rdflags bit N"
there as "rdflags & N".

## Constants

| Item | Old value | Corrected value | Source |
|---|---|---|---|
| `gserver LIGHT_AMBIENT` default | 1.0 | 0.6 | item 5.1 |
| Light probe, no surface hit | (0, 0, 0) (Quake II) | (0.1, 0.1, 0.1) | item 1.8 |
| Light probe cache cell | — | 32 units | item 1.8 |
| Bevel border factors | UNVERIFIED | light 1.3 (top, left), dark 0.7 (right, bottom) | item 1.3 |
| Sort key of chains with 0x0800 | "farthest" | `r_nearclipdist`² (16) | item 1.12 |
| `interface` font row | 32, 64, 32, 24 "grid" | first 32, end 64, line height 32, ascent 24 | item 1.10 |
| Menu 3D view (slot 0xb8) | — | vertical fov 33.75084377109428°, aspect 1.3333 | item 1.6 |
| Terrain header size | 140 | 88 header + 52 group record | item 9 |

## Test vectors / how to verify

1. `r_isf foo` prints `Avaiable formats: ` and the 43 names; `r_isf` is `GL_SOLID_FORMAT` afterwards (item 1.1).
2. Slot 0x6c with scale 2.0 draws the same glyph as scale 1.0 (item 1.2).
3. Bevel box (10, 10, 100, 20), colour (100, 200, 250, 255): borders (130, 255, 255) top/left, (70, 140, 175)
   right/bottom (item 1.3).
4. `sky none` with `gl_clear 0`: sky surfaces show the uncleared colour buffer, never the notexture pattern (item 1.9).
5. An alpha quad with flags 0x0003 is drawn (item 1.12); a One Frame muzzle flash is visible (item 2.1).
6. With `ghl_light_method 1`, Ghoul shadows fall straight down; with 2 they lean away from world lights (item 5.3).
7. `gserver LIGHT_AMBIENT` without a value prints `LIGHT_AMBIENT = 0.60` (item 5.1).
8. World label with a red colour argument: the name text is white, the team icon is red-tinted (item 1.5).
9. Removing `pics/menus/fonts/font1.fnt` makes renderer start-up fail with `Unable to find : pics/menus/fonts/font1.fnt`
   (item 1.10).
10. `terrain train` with the second byte of `env/train.trn` changed to 0: no error, no landscape (item 9).

## Open questions

- Which entities set render flags 0x1000 and bit 28 (item 1.11).
- The inttext top-row offset (item 8) and billboard texture orientation (SPEC-412) are derived, not observed.
- Whether SPEC-002's remaining open questions (listed in item 1.15) are affected by later work on the engine side.
