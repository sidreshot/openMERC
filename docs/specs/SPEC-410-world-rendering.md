# SPEC-410: World rendering
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: ref_gl.dll, version 1.07f (and the part of SoF.exe that converts BSP lumps for it)
Depends on: SPEC-002 (renderer interface, scene description, image registration), SPEC-100 (`.m32`), SPEC-101 (BSP v46),
SPEC-010/SPEC-011 (Ghoul system: world lights, render parameters), SPEC-412 (sorted alpha pass, screen overlays),
SPEC-413 (renderer cvars, image loading, colour-correction table), SPEC-411 (model rendering), SPEC-144 (`.trn`
terrain sets), SPEC-004 (sound module; A3D geometry export)

## Purpose

This spec describes how the original renderer draws the world of a SoF map so that a clean renderer produces the same
picture: which BSP data it uses and how it interprets the fields that SPEC-101 and SPEC-100 leave open, the order of the
world passes in a frame, lightmaps and dynamic lights, texture animation, scrolling, liquids, translucent surfaces, the
detail/environment pass, the sky box, fog regions and inline brush models.

Quake II 3.19 (`ref_gl`) is the baseline. Where a rule says "as in Quake II", the behaviour of the Quake II GPL renderer
applies unchanged. Everything else is a SoF difference.

Terms used below:
- **game clock**: refdef offset 0x50 (SPEC-002), seconds, stops while paused. All animation in this spec uses it.
- **clock delta**: refdef offset 0x4c.
- **eye**: refdef `vieworg`. **forward/right/up**: the view axes from `viewangles` (Quake convention).
- **s, t**: texture-space coordinates of a vertex, `s = v · vecs[0].xyz + vecs[0].w`, `t = v · vecs[1].xyz + vecs[1].w`
  (texinfo vectors, as in Quake II).
- **turb(x)**: the Quake II warp table: `8 · sin(2π · k / 256)` for `k = (integer part of x · 256 / (2π))` modulo 256.
  The integer part is taken by truncation toward zero; negative values wrap through the modulo-256 mask (two's complement).

## Interface / layout

### BSP data used by the renderer

The renderer reads lumps through the engine's shared lump cache (SPEC-002 import slot 0x58). It uses:

| Lump | SPEC-101 name | Engine console name | Renderer use |
|---|---|---|---|
| 1 | planes | `LUMP_PLANES` | node and face planes |
| 2 | vertexes | `LUMP_VERTEXES` | polygons |
| 3 | visibility | `LUMP_VISIBILITY` | PVS, as in Quake II |
| 4 | nodes | `LUMP_NODES` | BSP walk |
| 5 | texinfo | `LUMP_TEXINFO` | texture, flags, value, animation chain |
| 6 | faces | `LUMP_FACES` | surfaces |
| 7 | lighting | `LUMP_LIGHTING` | lightmaps |
| 8 | leafs | `LUMP_LEAFS` | cluster, area, region |
| 9 | leaffaces | `LUMP_LEAFFACES` | visible-surface marking |
| 11 | edges | `LUMP_EDGES` | polygons |
| 12 | surfedges | `LUMP_SURFEDGES` | polygons and region polygons |
| 13 | models | `LUMP_MODELS` | inline brush models |
| 19 | facepolys | `LUMP_REGIONFACES` | fog-region polygons (see *Fog regions*) |
| 20 | lights | `LUMP_LIGHTS` | Ghoul model lighting only (see *Model lights*) |
| 21 | volumes | `LUMP_REGIONS` | fog regions |

Lumps 0, 10, 14–18 are not read by the renderer (areas come from the engine's `areabits`).

The engine's lump names are visible in its console messages (for example `CM_LoadLump: lump %s too small`). Below,
lump 21 records are called **regions** and lump 19 records **region polygons**.

### Face fields (SPEC-101 face record)

| Field | Renderer use |
|---|---|
| `planenum`, `side`, `firstedge`, `numedges`, `texinfo` | as in Quake II |
| `volume` | the face's fog region (−1 = none) |
| `firstpoly`, `numpolys` | the face's region polygons (used only by the fog overlay) |
| `styles[4]` | light styles, 255 ends the list |
| `lightofs` | start of the face's lightmap blocks |
| `lm_width`, `lm_height` | lightmap size in samples (used as stored) |
| `lm_scale_s`, `lm_scale_t` | per-axis sample spacing in 16-texel units (1 or 2; see *Lightmaps*) |
| `lm_mins`, `lm_extents` | used **as stored** in place of Quake II's computed texture minimum and extent |

### Leaf fields

`contents`, `cluster`, `area`, `mins/maxs` and the leaf-face list are used as in Quake II. `volume` gives the leaf's fog
region. Contents bit 0x100 is not tested by the renderer.

### Region record (lump 21), as interpreted

| Offset | Type | Meaning for the renderer |
|---|---|---|
| 0 | int32 | Fog kind (0, 1, 2). Converted to a falloff mode at load, but **no rendering path reads it** in 1.07f. |
| 4 | float | **Maximum opacity** 0..1. The overlay alpha never exceeds 255 × this value. Also divides the end distance in hardware-fog mode. |
| 8 | float | **Shimmer amplitude** (used only when flag bit 0 is set) |
| 12 | float | **Fog start**, in squared world units (overlay mode); hardware-fog mode uses `value + 90` as linear fog start in units |
| 16 | float | **Fog range**: squared units per alpha step (overlay mode); hardware-fog end distance before division by offset 4 |
| 20 | float | not used by the renderer |
| 24 | byte[4] | **Fog colour** R, G, B, A |
| 28 | int32 | Flags: bit 0 (1) = shimmer animation; bit 1 (2) = **height falloff**; bit 2 (4) is not used by the renderer |
| 32 | float | **Shimmer speed** multiplier |
| 36 | float | **Far fog wall**: any non-zero value enables it (the value itself is not used; see *Far fog wall*) |
| 40 | float | not used by the renderer |
| 44 | 32 bytes | not used |

At load the renderer also derives per region:
- **bounds**: the axis-aligned box of all vertices of all region polygons of faces that reference the region, and its size;
- **boundary planes**: the planes of faces that reference the region and whose texinfo has flag 0x10000, without
  duplicates, at most the first 5 in face order.

### Region polygons (lump 19)

Each record is a loop of `numedges` surfedges starting at `firstedge`; its vertices are the edge vertices as for faces. A
face owns its `numpolys` records starting at `firstpoly`. They are used only to draw the fog overlay of that face; the
face itself is drawn from its own edge loop. Region polygons are triangle fans in loop order.

### Lights record (lump 20)

Used only to build the Ghoul system's static world-light set (SPEC-010 methods 36, 34, 35). See *Model lights*.

### Texinfo flags as interpreted

| Bit | Renderer behaviour |
|---|---|
| 0x1 (light) | none |
| 0x4 (sky) | not drawn; its polygon defines visible sky (see *Sky*); no lightmap |
| 0x8 (warp) | liquid warp (see *Liquids*); no lightmap; subdivided |
| 0x10, 0x20 (trans33/66) | **ignored**: such surfaces are drawn opaque unless they also have 0x800 |
| 0x40 (flowing) | texture scroll driven by the texinfo `value` (see *Flowing*) |
| 0x80 (nodraw) | not tested (stock maps contain no such faces) |
| 0x400 | no detail/environment pass for this surface |
| 0x800 (alpha image) | translucent surface: no lightmap, vertex-lit, drawn in the sorted alpha pass |
| 0x1000 | time-based texture animation at the rate stored in the `.m32` (see *Texture animation*) |
| 0x2000 | liquid wave: vertex height and colour/alpha modulation; effective only together with 0x8 |
| 0x8000, 0x200000, 0x400000, 0x800000 | not used by the renderer |
| 0x10000 | **fog-region boundary**: never drawn; contributes a boundary plane; receives the fog overlay |
| 0x20000 | environment map uses sphere mapping instead of planar reflection (see *Detail and environment pass*) |
| bits 24–31 (material id) | not used by the renderer |

Lightmaps are created for every face whose texinfo has none of 0x4, 0x8 and 0x800.

### `.m32` fields as interpreted (resolves SPEC-100 open questions where the renderer decides)

| Field | Renderer use |
|---|---|
| `alt_name` | Substitute texture. The named texture `textures/<alt_name>` is loaded **instead** when cvar `gl_pictip` ≠ 0, or when `kind` bit 0x4 is set and the parental-lock cvar `lock_textures` ≠ 0. The substitute is then used under the original name. |
| `kind` bit 0x4 | "content subject to the parental lock" (graffiti, posters, signs): see `alt_name` |
| `kind` bit 0x1 | only mip level 0 is uploaded (SPEC-413) |
| `kind` bit 0x2 / 0x8 | the image is a detail texture / an environment map (image types 5 / 8 of SPEC-002) |
| `value` (low byte) | animation rate in frames per second for texinfo flag 0x1000 (see *Texture animation*) |
| `flags` bit 0x800 | image has alpha (selects the alpha upload format, SPEC-413) |
| `damaged_name`, `damage_value` | the damaged variant is loaded as `textures/<damaged_name>`; `damage_value` is not used by the renderer |
| `detail_name` | second-pass texture `textures/<detail_name>` |
| `detail_scale_x/y` | multipliers applied to the base texture coordinates for the detail pass |
| `detail_shift_x/y` | detail scroll speed, detail-texture widths per second of game clock |
| `detail_blend_src/dst` | **ignored**: the pass always uses the blend (destination colour, source colour) |
| `unknown_2d8` | one extra mip level is skipped for this texture when `gl_pictip` ≠ 0 (SPEC-413) |
| `scale_x/y`, `unknown_36c`, `anim_next`, material id | not used by the renderer (animation chains come from texinfo `nexttexinfo`) |

## Behaviour

### World part of a frame

RenderFrame (SPEC-002) performs, in this order:
1. Updates the detail scroll offsets (see *Detail and environment pass*).
2. Marks surfaces touched by dynamic lights, including the Ghoul lights appended by SPEC-002's write-back (Quake II
   `R_PushDlights` rule: a light marks a node's surfaces when the distance to the node plane ≤ intensity − 64).
3. Frame set-up: view axes; the view leaf is the leaf containing the eye. A second probe is taken 16 units below the eye
   when the eye leaf's contents are 0, otherwise 16 units above. If the probe leaf is not solid and its cluster differs,
   its cluster becomes the second view cluster and **the probe leaf becomes the view leaf** (the view leaf decides
   "eye inside a fog region" below).
4. Frustum: four planes at ±(90° − fov/2) as in Quake II.
5. PVS marking as in Quake II (cvars `r_novis`, `gl_lockpvs`; two clusters merged when the probe found a second one).
6. **World surfaces** (if `r_drawworld` ≠ 0 and rdflags bit 2 is clear): BSP walk, opaque surfaces by texture,
   two-pass lightmap blending when multitexture is unavailable, the detail/environment pass, the sky box, the terrain set
   (see *Terrain sets*), debug outlines.
7. **Fog overlay pass** for the fog surfaces collected in step 6, with shimmer animation advanced.
8. The sorted alpha pass (SPEC-412): entities, alpha world surfaces, alpha quads, quad chains.
9. **Fog overlay pass** for fog surfaces collected during step 8 (no animation step, no far wall).
10. Screen overlays (SPEC-412).
11. `r_lightlevel` := 150 × the largest component of the light at the eye (see *Light sampling*), when a world is drawn.

### Visibility and culling

- Node and leaf culling against the PVS, the area bits and the frustum are as in Quake II (`r_nocull` disables frustum
  culling).
- A face is considered once per frame, when its node is walked and the eye is on the face's front side.

### Surface classification during the walk

For each visible front-facing world surface, by texinfo flags in this priority:
1. 0x10000: not drawn; queued for the fog overlay; its boundary plane is marked "seen this frame" (see *Fog regions*).
2. 0x4: added to the sky (polygon clipped into the sky box); queued for fog if it has a region.
3. 0x800: added to the frame's list of alpha surfaces (drawn in the sorted alpha pass); queued for fog if it has a region.
4. Otherwise: the current animation frame's image is chosen and the surface is chained to that image.

After the walk, each image's chain is drawn: warp surfaces as liquids, all others as lightmapped surfaces.

**Alpha image without the alpha flag.** If the image chosen for a world surface (after animation) has `.m32` flag
0x800 but the surface's texinfo does not, the surface is **not drawn at all** in the world: images with alpha are
skipped when the per-image chains are drawn, and such a surface is not on the alpha list either. Inline brush models
do not have this rule (see *Inline brush models*: their non-0x800 faces are drawn opaque with the alpha test).
Stock data: 42 world faces in 4 maps (29 `subway/2_watersteam` faces in `nyc2`, 8 `iraq/girdwall` in `kos3`,
3 `siberia/3_railing_snow` in `sib1`, 1 `iraq/3_catwalk2` in `irq2b`, 1 `tokyo/1_grating` in `jpn1`); 92 more
such faces belong to inline models and are drawn. UNVERIFIED in game: in `nyc2`, the steam-water surfaces
(`subway/2_watersteam`) should be invisible in the original.

### Texture animation

A texinfo's animation chain is its `nexttexinfo` sequence (a `nexttexinfo` ≤ 0 ends the chain; counting stops when the
chain returns to the start, or after 33 entries with a developer message `> 32 animframes on <texture>`). The frame
index is:
- if the texinfo has flag 0x1000 and the base image's `.m32` `value` low byte `r` is non-zero: `floor(r × game clock)`;
- otherwise the entity frame: for the world, `floor(2 × game clock)`; for inline models, the entity's frame.

The image used is the one `index mod frame count` steps along the chain. The same rule is used by every pass (opaque,
alpha, detail).

### Texture coordinates

- Base texture: `(s / w, t / h)` where w × h is the level-0 size of the texinfo's own image as loaded (not the uploaded
  size, not the animation frame's). When an `alt_name` substitute was loaded, its size is used, so a substitute of a
  different size changes the texture scale on the wall.
- Each polygon's base coordinates may be shifted by whole numbers (cvar `gl_textureclamp`, default 1); this has no visible
  effect.
- Lightmap (page of 128 × 128 samples), per axis, with `m` = stored `lm_mins` and `k` = stored scale:
  `u = (s − m) / 16` when k = 1, and `u = floor((s − m) / (16 · k))` when k ≠ 1;
  texture coordinate `= (u + block_x + 0.5) / 128`, where block_x is the sample column the face's lightmap occupies in its
  page. The same on t with block_y.

### Lightmaps

**Storage.** Each lit face gets a block of `lm_width × lm_height` samples (stored values; faces without lighting data use
`(extent / 16) + 1` per axis) in a 128 × 128 page, allocated as in Quake II (`LM_AllocBlock`, column skyline). Pages are
numbered from 1; at most 127 static pages ("LM_UploadBlock() - MAX_LIGHTMAPS exceeded" drop error). Page 0 is reserved for
lightmaps rebuilt every frame. Pages use linear filtering. At most 12 288 samples per face ("Bad s_blocklights size").

**Building** (at load, and when a lightmap changes). Let `M` = the value of cvar `gl_modulate` latched when the renderer
started (default 1). For each sample and colour channel:

`L = Σ over the face's styles ( sample_byte × M × style_rgb[channel] )`

then dynamic lights (below) are added. While a map loads, all styles are taken as (1, 1, 1). Faces without lighting data
are 255 in every channel. Then, per sample, with `R, G, B` = L rounded to the nearest integer and clamped at 0, and
`A = max(R, G, B)`: if `A > 255` all four are multiplied by `255 / A` (truncated). The stored texel depends on the first
character of cvar `gl_monolightmap` (default `0`):

| `gl_monolightmap` | Texel | Upload format |
|---|---|---|
| `0` | (T[R], T[G], T[B], A), T = the image colour-correction table of SPEC-413 | RGB |
| `L`, `I` | (A, 0, 0, A) | luminance / intensity |
| `C` | colour-weighted alpha as in Quake II | RGBA |
| anything else (`A` …) | (0, 0, 0, 255 − A) | RGBA for `A`, RGB otherwise |

**Dynamic updates** (cvar `gl_dynamic` ≠ 0). A face's lightmap is rebuilt when any of its styles' `white` value differs
from the value used for its last build, or when a dynamic light marked it this frame. Faces whose changing style is 1–31
(other than 13), or that are dynamically lit, are rebuilt every frame into page 0; others are rebuilt in their own page.
The visible result is the same: lightmaps follow the current light styles and dynamic lights.

**Dynamic lights on lightmaps** (Quake II rule with SoF constants). For each dynamic light marked on the face: `d` = signed
distance from the light to the face plane, `r = intensity − |d|`; nothing if `r < 64`. The light's projection on the plane
gives local coordinates `(ls, lt)` in texture units relative to `lm_mins`. Sample (i, j) sits at
`(16 · lm_scale_s · i, 16 · lm_scale_t · j)`. With `ds = |ls − sample_s|`, `dt = |lt − sample_t|` (integers) and
`dist = max(ds, dt) + min(ds, dt) / 2` (integer halving): if `dist < r − 64` the sample gains `(r − dist) × light_colour`
(colour components 0..1). Dynamic contributions are **not** multiplied by `M`.

**Combining with the texture.**
- With ARB multitexture: texture × lightmap in one pass (texture unit 0 replaces, unit 1 modulates).
- Without it: textures first (replace), then each lightmap page drawn over the surfaces with depth writes off and blend
  (zero, source colour) for `gl_monolightmap` `0`, `L`, `I`; (source alpha, one minus source alpha) for other values;
  (one, one) when `gl_saturatelighting` ≠ 0; no blending when `gl_lightmap` ≠ 0 (lightmaps only).
- There is **no overbright factor**: a sample of 255 shows the texture unchanged.
- No lightmaps (textures at full brightness) when `r_fullbright` ≠ 0 or rdflags bit 4 (night vision) is set.

### Flowing surfaces (flag 0x40)

The texinfo `value` holds two signed 16-bit speeds: `vs` = the upper 16 bits, `vt` = the lower 16 bits. Every base
texture coordinate is reduced by `(vs × clock × 0.01, vt × clock × 0.01)` (texture repeats per 1.0). Examples from the stock
maps: value 128 scrolls t by 1.28 per second; value 0xAC0000 scrolls s by 1.72 per second. The detail pass follows the same
scroll (scaled by the detail scale).

### Liquids (flag 0x8)

- Polygons are subdivided on a 64-unit grid with a centre vertex per piece, as in Quake II; their texture coordinates
  are `v · vecs[n].xyz` without the offset, as in Quake II.
- Per vertex, per frame: `s' = (s + turb(t / 8 + clock)) / 64`, `t' = (t + turb(s / 8 + clock)) / 64` (no scroll term).
- Vertex colour = the light at the vertex sampled straight down (see *Light sampling*); smooth shading; texture modulated.
  Liquids have no lightmap.
- With flag 0x2000 (waves), for each vertex (x, y, z):
  - `h = 0.5625 × turb((2.3x + y) · 0.015 + 2 · clock)`; the vertex is drawn at z + h (texture coordinates are computed
    before the shift);
  - colour = light × `k`, `k = (h + 4.5) × 0.1 + 0.1`;
  - alpha = `(0.5625 × turb((2.3x + y) · 0.015 + 2.7 · clock) × 0.125 × turb((1.7y + x) · 0.015 + 3.37 · clock) + 4.5) × 0.038889 + 0.6`.
  Alpha has a visible effect only when the surface is drawn with blending (flag 0x800).
- If the base image's second-pass texture is an environment map (type 8), the polygons are drawn again with blend
  (destination colour, source colour), sphere-mapped texture coordinates (OpenGL sphere-map generation), the face plane
  normal, colour 1 and the same wave height.
- Liquid surfaces without 0x800 are drawn in the opaque pass and are therefore **opaque**, even with the trans33/66 flags
  (UNVERIFIED in game, see tests).

### Alpha surfaces (flag 0x800)

- **World**: collected during the walk and drawn in the sorted alpha pass (sort order: SPEC-412; key = smallest squared
  distance from any vertex to the eye). Drawn with blend (source alpha, one minus source alpha), depth writes off, alpha
  test (greater than 0.05) on, smooth shading, texture modulated by a per-vertex colour = the light sampled straight down
  at each vertex. Flowing and animation rules apply; liquids use the liquid rule. The fog overlay for the surface is
  drawn immediately after it.
- **Inline brush models**: after the model's opaque faces, its 0x800 faces are drawn in face order (not sorted), depth
  writes off, texture modulated by one colour: the light sampled at the model's bounding-box centre (UNVERIFIED exact
  point).

### Opaque state

Opaque world surfaces are drawn with depth test and writes on, alpha test "greater than 0.05" (so texels with alpha ≤ 12
are cut out), no blending, back-face culling per `gl_cull`.

### Detail and environment pass

Applies to opaque surfaces whose (animated) base image has a second-pass texture, unless the texinfo has 0x400, when
`gl_detailtexturing` ≠ 0 (at most 1024 surfaces per pass). Drawn after all opaque world surfaces (and after each inline
model's surfaces), with blend (destination colour, source colour) — i.e. 2 × modulate, 128 grey is neutral — depth writes
off, alpha test off and a polygon offset of (`gl_offsetfactor`, `gl_offsetunits`) (default −2, −2) when either is non-zero.

- **Detail textures** (type 5, uploaded as luminance from the red channel, SPEC-413): coordinates
  `s_d = (s/w − flow_s) × detail_scale_x + scroll_x`, `t_d = (t/h − flow_t) × detail_scale_y + scroll_y`, where flow is the
  flowing offset above and scroll accumulates `detail_shift × clock delta` each frame, wrapped to [0, 1) once it exceeds 1.
  With `gl_detailtexturing` = 2 a surface is skipped unless at least one vertex has `(v − eye) · forward <
  gl_detailtexturingdistance` (default 250).
- **Environment maps** (type 8):
  - sphere mapping (OpenGL sphere-map generation with the face normal) when the texinfo has 0x20000 or the face plane is
    near-horizontal (`|n × (0,0,1)|² < 0.1`);
  - otherwise planar reflection: `U = normalize(n × (0,0,1))`, `V = normalize(U × n)`, `d = normalize(eye − v)`,
    `r = 2(n · d) n − d`, coordinates `((r · U + 1)/2, (r · V + 1)/2)`.
- `gl_detailtexturing` ≥ 3 is a diagnostic: base textures are not drawn and the pass replaces instead of blending.

### Sky

- `pics/sky/<name>.txt` (SPEC-002 SetSky): line *n* (1–6) names the image for face *n* in the Quake II order
  **rt, bk, lf, ft, up, dn**. Stock files list four side images, then `up`, then `none` for `dn`.
- A face whose line is `none` (any case) or whose image is missing is **not drawn**.
- The box is centred on the eye, half-size 37 800 units, rotated by `rotate × game clock` degrees about `axis`.
- Visible extents per face come from clipping the sky-flagged polygons into the box (Quake II rule). The whole face is
  drawn instead when `rotate` ≠ 0 or `fov_x` < 90.
- Texture coordinates are clamped to [1/512, 511/512], or to [1/256, 255/256] with one extra mip level dropped when
  `gl_skymip` ≠ 0 or `rotate` ≠ 0.
- The sky box is drawn after the opaque world and the detail pass, with depth testing.

### Fog regions

Fog is drawn as a translucent overlay on the region polygons of faces that belong to a region. Behaviour depends on cvar
`gl_fogmode` (default 0):

| `gl_fogmode` | Behaviour |
|---|---|
| 0 | overlay pass for all region faces (boundary and interior) |
| 1 | interior faces use OpenGL linear fog while drawn (start = offset 12 + 90, end = offset 16 / offset 4, or offset 16 when offset 4 is 0, colour = region colour); boundary faces still get the overlay |
| 2 | no fog |
| 3 | diagnostic: each region polygon filled with a colour derived from its identity at alpha 0.8 |

**Parameters** for a region: start = offset 12, range = offset 16, max alpha = 255 × offset 4, colour = offset 24. The cvars
`gl_fogstart`, `gl_fogend`, `gl_fogdensity` (× 255), `gl_fogcolor` (`"R G B"`, 0–255) replace the corresponding value for
every region when their numeric value is ≥ 0 (defaults −1 = use the map). `gl_fogtime` and `gl_fogdensityrange` replace the
shimmer speed and amplitude in the same way.

**Shimmer** (flag bit 0). At load each such region gets 10 phases (random 0..20) and 10 speeds (random 0.05..0.3). Once per
rendered frame (first overlay pass only, so the speed depends on the frame rate) `phase_i += speed_i × shimmer speed`
and `w_i = c·|c|` with `c = cos(phase_i) × amplitude`.

**Fog thickness of a point p** (overlay vertex or object):
1. `D = |p − eye|²`.
2. If the view leaf (frame set-up step 3) is not in this region, for each of the region's boundary planes not marked
   this frame:
   `a = n · eye − dist`, `b = dist − n · p`. If a and b are both non-zero and of opposite sign (eye and p on the same
   side), the plane is marked for the rest of the frame and skipped. Otherwise `D = min(D, D_0 · b² / (a² + b²))` with
   `D_0` the value from step 1. Planes whose boundary faces were walked this frame are already marked.
3. With flag bit 0: `D += w_k`, `k = floor(|p.x| + |p.y| + |p.z|) mod 10`.

**Overlay alpha** of a vertex: `α = (D − start) / range`, 0 when negative, capped at max alpha; with flag bit 1 multiplied
by `((bounds_max_z − p.z) / bounds_size_z)²`. The vertex colour is the region colour with alpha `floor(α)`. Note that the
distance is **squared**: with start 4, range 400 and opacity 1, alpha reaches 255 at about 319 units.

**Overlay drawing**: each region polygon of each queued face (at most 1024 faces per pass) is drawn as a triangle fan
without texture, per-vertex colour, smooth shading, blend (source alpha, one minus source alpha), depth test on, depth
writes off, polygon offset (`gl_offsetfactor`, `gl_offsetunits`). For inline models the eye is taken relative to the
model origin. `gl_fogverts` ≠ 0 only changes how the vertices are submitted.

**Far fog wall** (first overlay pass of a frame only, and only when that pass has at least one queued face). It is drawn
when the view leaf's region has offset 36 ≠ 0 (that region gives the colour), or otherwise when any overlay vertex of a
region with offset 36 ≠ 0 was processed (the region whose vertex had the smallest D gives the colour). It is a quad facing the viewer at `eye + 2000 × forward`, extending ±2500 along right and up, in the
region colour (or `gl_fogcolor`) including its alpha (255 in all stock maps), depth-tested, no depth writes. It hides all
geometry more than about 2000 units ahead.

**Point fog** (used for Ghoul models and alpha quads, SPEC-411/412). For a point p in region R: thickness D as above,
`f = min(1, clamp((D − start)/range, 0, max alpha) / 255)` using the start, range, max alpha and colour **of the region
most recently processed by an overlay pass** (or the cvar overrides) (UNVERIFIED whether R's own values were intended).
If `f > 0.01`, OpenGL linear fog is enabled with start 0 and end `|forward · (p − eye)| / f`, so the point receives fog
factor f. Colour modes: normal = fog colour, alpha 1; black (0,0,0,1) for additive or darkening quads; fog colour with
alpha 0 for a fourth mode (SPEC-412).

### Inline brush models

- Culled by their bounding box (rotated models by a sphere of the model radius); transformed by the entity origin and
  angles (Quake II).
- Dynamic lights mark the model's surfaces without being transformed into model space (as in Quake II).
- The model's fog region is the region of the world leaf containing its bounding-box centre plus origin.
- Faces are drawn when the local eye is more than 0.01 in front of their plane. Opaque faces first (same rules as the
  world), then 0x800 faces (see *Alpha surfaces*), then the detail pass and the fog overlay for the model.

### Light sampling (export slot 0xa4 and internal use)

- No lighting data in the world: (1, 1, 1).
- The ray goes from p to `p − (0, 0, 2048)` (dir 255) or to `p + 256 × dir_table[dir]`; the first surface hit that has a
  lightmap (not 0x4/0x8/0x800) is sampled at the hit point: sample `(floor((s − m) / 16) / k_s, floor((t − m) / 16) / k_t)`
  using integer division, s and t truncated, no filtering. Static light per channel:
  `min(1, Σ styles sample/255 × style_rgb × M)`. No hit: (0.1, 0.1, 0.1).
- Then every dynamic light adds `max(0, (intensity − distance) / 256) × colour`, and each channel is multiplied by `M`
  again and clamped to 1. (With `M` = 1 this is the Quake II result clamped to 1.)
- Static results are cached per 32-unit cell (cells aligned at −4096) and direction: the first query in a cell
  determines the static value for later queries in that cell while the entry lives (entries unused for 100 queries are
  dropped once there are more than 200; at most 1000; cleared when a map loads).

### Model lights (lump 20)

When the world loads, the Ghoul static light set is rebuilt (SPEC-010 methods 36, 34…, 35). For each lights record with
intensity > 0: radius `r = intensity`, but `r = sqrt(intensity) / 2` when type = 0 or the trailing integer > 1. Records
with `r > 1500` or type 3 are skipped. The light record passed is origin, radius, colour.

### Damaged surfaces

SPEC-002 slot 0xa8 switches a texinfo to its image's damaged variant and ends the texinfo's animation chain. The change
lasts until the world is reloaded; restarting the same map with `flushmap` 0 keeps the world loaded (UNVERIFIED whether
the damage then persists).

### Missing textures

A world texture that cannot be loaded prints a warning (`Warning : Couldn't load …`, after the image loader's own
message) and uses `pics/console/notexture`.

### Terrain sets

A *terrain set* is the procedurally scrolled landscape that the renderer draws outside the moving train in level
`trn1` (worldspawn key `"terrainset" "train"`, file `env/train.trn`, the only stock set). The landscape is a strip of
textured ground made of rows across the direction of travel. Rows move backwards at a constant speed; a row that
falls behind the viewer is regenerated in front with new random values. 32 flat cloud quads drift overhead.
This section completes SPEC-144 (data layout) and SPEC-002 slot 0x38 (entry point).

#### Selecting a set (export slot 0x38, console `terrain`)

1. Any set that is loaded is freed first (its textures stay registered as ordinary images).
2. A null name, an empty name or the exact text `(null)` means "no terrain": nothing is loaded and nothing is
   printed.
3. Otherwise the renderer loads `env/<name>.trn`. If the file cannot be read it prints
   `<name> isn't a valid terrain set name` and no terrain is shown.
4. The **first byte of the file must be `"` (0x22)**. This is the whole version check. Otherwise it prints
   `Invalid version for Terrain Set <name>` and no terrain is shown.
5. The file is parsed as below. Textures are registered as wall images (SPEC-002 type 2) during the call. When the
   engine calls the slot from its map registration sequence (SPEC-002), they belong to the current registration
   cycle.
6. If the *active* byte (offset 1) is non-zero, the landscape is built immediately: the row ring is allocated,
   the 32 clouds are placed (see *Clouds*), all rows are generated, and the cloud texture is registered. If the
   active byte is 0, the set stays loaded but is never drawn.

The set lives until the next call of slot 0x38 or until the renderer library is unloaded. `R_Shutdown`/`vid_restart`
alone does not reload it; the engine calls slot 0x38 again during its next map registration.

#### File layout (.trn), as the renderer reads it

Little-endian, byte-packed, no alignment. Offsets in the header are absolute.

**Header (88 bytes)**

| Offset | Size | Type | Value in `train.trn` | Meaning |
|---|---|---|---|---|
| 0x00 | 1 | byte | 0x22 | Signature/version: must be `"` |
| 0x01 | 1 | byte | 1 | Active: 0 = load but never build or draw |
| 0x02 | 4 | int32 | 150 | Not used |
| 0x06 | 4 | int32 | 40 | **Row count** N (rows in the ring) |
| 0x0A | 4 | int32 | 23 | **Columns per row** M; also the number of column records in each group |
| 0x0E | 4 | int32 | 1 | **Group count**. Every group is parsed; only group 0 is used |
| 0x12 | 4 | int32 | 1 | Sprite-list count |
| 0x16 | 4 | int32 | 6 | Texture-slot count |
| 0x1A | 4 | float | 3500 | **Scroll speed**, world units per second of game clock |
| 0x1E | 4 | float | 0 | Fog start distance |
| 0x22 | 4 | float | 0 | Fog end distance. Fog and clearing (below) are used only when this is ≥ 1.0 |
| 0x26 | 12 | float[3] | 0, 0, 0 | Fog colour R, G, B (0–1); also the clear colour |
| 0x32 | 1 | byte | 1 | Clear flag (see *Fog and buffer clears*) |
| 0x33 | 24 | float[6] | 0, 0, 1, 1, 1, 1 | Read; no effect |
| 0x4B | 1 | byte | 1 | "Sun" flag: when non-zero the vector below is normalised every frame; no visible effect |
| 0x4C | 12 | float[3] | 0, 0, 1000 | "Sun" vector; no visible effect |

**Group record (52 bytes), `group count` times from offset 0x58.** Each group is immediately followed by its M
column records.

| Offset | Size | Type | `train.trn` | Meaning |
|---|---|---|---|---|
| 0 | 4 | int32 | 900 | **Row spacing** S, world units along the direction of travel |
| 4 | 4 | float | 8300 | **Lateral offset**, subtracted from every column's lateral position |
| 8 | 4 | float | 150 | **Height offset**, subtracted from every column's height |
| 12 | 4 | float | 200 | Not used |
| 16 | 4 | float | 0 | Not used |
| 20 | 16 | float[4] | 0 | Row lateral wave f1, f2, a1, a2: evaluated per generated row, no visible effect |
| 36 | 16 | float[4] | 0 | Row height wave, same: no visible effect |

**Column record (68 bytes), M per group.** Column *c* gives the vertices of column *c* in every row.

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | int32 | **Texture slot** index (0 … slot count − 1) |
| 4 | 4 | int32 | Not used (−1 or 0 in stock data) |
| 8 | 8 | float[2] | **Lateral range** ymin, ymax |
| 16 | 8 | float[2] | **Height range** zmin, zmax |
| 24 | 8 | float[2] | Not used (0.3, 0.2 or 0 in stock data) |
| 32 | 4 | int32 | Flags. Bit 0x2: mirror the texture (swap s0/s1 and t0/t1, see below). Other bits unused |
| 36 | 16 | float[4] | **Lateral wave** f1, f2 (radians per second), a1, a2 (units) |
| 52 | 16 | float[4] | **Height wave** f1, f2, a1, a2 |

**Sprite section.** 4 bytes are skipped (stock: `2D 42 01 00`; the value is not checked). Then for each of the
sprite-list count: int32 *n*, then *n* entries, each a NUL-terminated name followed by 20 bytes (float, float,
float, int32, int32). **The whole section is parsed and discarded**; terrain sprites (`sprites/terrain/bush1.sp2`,
`bush2.sp2`) are never drawn. That the files are missing from the paks therefore has no effect.

**Texture section.** 4 bytes are skipped (stock: `2D 42 01 00`, not checked). Then for each texture slot: int32
*n* (≥ 1), then *n* texture entries:

| Size | Type | Meaning |
|---|---|---|
| var. | char[] | Texture name, NUL-terminated. A trailing `.m32` (any case) is removed. The name is registered as a wall image; if it cannot be loaded the default "notexture" image is used |
| 4 | float | Cumulative selection threshold (0–1, ascending within a slot; the last entry should be ≥ 1) |
| 4 | float | s divisor (units per texture repeat) |
| 4 | float | t divisor |
| 4 | float | Fixed s repeat count; non-zero overrides everything else for s |
| 4 | float | Fixed t repeat count; non-zero overrides everything else for t |
| 1 | byte | Whole-repeat flag |

Nothing after the last texture entry is read (stock: a final `2D 42 01 00`). The parser trusts the counts; it does
not check the file length.

`train.trn` slots: 0 `uganda/t_dirt_slopeb` (fixed 2 × 1), 1 `uganda/t_track_bed` (2 × 1), 2
`uganda/t_dirt_sandtran` (1 × 1), 3 and 4 `uganda/t_terrain5` (1 × 2), 5 `uganda/water1` (1 × 1), each with one
entry and threshold 1.0.

#### Geometry

**Coordinates.** Terrain vertices are in world coordinates without any transform: +X is the direction of travel
(the ground moves towards −X), Y is lateral, Z is up.

**Row ring.** N rows of M vertices each. They are built in order *i* = 0 … N−1 at

  x_i = (i − N/2) · S

so the strip initially covers −N·S/2 … (N/2 − 1)·S along X. For `train.trn`: 40 rows from −18 000 to +17 100, 23
columns, 900 units apart.

**Vertex of column c in a row being generated** (t = the game clock at that moment, refdef offset 0x50, see
SPEC-002; `rand(a, b)` = uniform random float, the engine's flrand):

  y = rand(ymin, ymax) + a1·sin(f1·t) + a2·sin(f2·t) − lateral offset

  z = rand(zmin, zmax) + a1'·sin(f1'·t) + a2'·sin(f2'·t) − height offset

The primed values are the height-wave fields. All vertices of a row share the row's x. Because t advances, the
waves make the landscape drift slowly sideways and up and down from row to row. For `train.trn` the track bed (slot
1) lies at y ≈ −100 and z ≈ −50; the outer hills move by up to ±1200 units.

**Texture of a vertex.** When the vertex is generated, r = rand(0, 1) is drawn. The first entry of the column's
slot whose threshold is ≥ r is chosen. The quad that starts at this vertex is drawn with that entry's image.

**Texture coordinates of a vertex** are computed once, when the row is generated. They are used only by the quad
that starts at that vertex (next paragraph).
- *s range* (from Ls = 3D distance to the next vertex in the same row, column c+1):
  - fixed s ≠ 0: s0 = 0, s1 = fixed s;
  - otherwise, whole-repeat flag set: s0 = 0, s1 = Ls / s divisor truncated toward zero;
  - otherwise: s0 = y of the vertex (raw world units) and s1 = s0 + Ls / s divisor.
  - The last column has no next vertex, so its s range is never set; it is never used.
- *t range*: the same rules with the t divisor, fixed t and Lt = 3D distance to the vertex of the same column in
  the neighbouring row generated just before (the row 1·S behind). In the last rule t0 = the game clock t.
  The row built first at selection time has no such neighbour: its t range is undefined until the row is
  regenerated. It is the rearmost row, so this lasts at most one pass.
- If the column record has flag 0x2, s0↔s1 and t0↔t1 are swapped.

**Quads.** For every pair of rows that are neighbours along X (each row and the row S ahead of it), except across
the seam between the frontmost and rearmost row, and every column c = 0 … M−2, one quad is drawn. Let V be vertex
(row, c) and let "ahead" be the row S further along +X. The corners are, in order:

| Corner | Vertex | (s, t) |
|---|---|---|
| 1 | (row, c) | (s0, t0) |
| 2 | (row, c+1) | (s0, t1) |
| 3 | (ahead, c+1) | (s1, t1) |
| 4 | (ahead, c) | (s1, t0) |

s0, s1, t0, t1 and the image all come from V. Note the cross-over: the s range, computed from the lateral
neighbour distance, runs along X, and the t range runs across the row. With `train.trn` (all fixed repeats) the
texture repeats s1 times along X and t1 times across each quad. Maximum quads: (N−1)·(M−1) = 858 for `train.trn`.

#### Per-frame update and drawing

Terrain is drawn inside the world pass: after all opaque world surfaces, the lightmap pass, the world's
translucent/detail surface pass and the sky box, and before the `gl_showtris` overlay (those passes are specified elsewhere in this spec).
It is therefore drawn only when the world is drawn (`r_drawworld` ≠ 0 and the frame has a world). Entities and the
sorted translucent pass are drawn later and appear in front of it where depth allows. Nothing is drawn unless a set
is loaded and its active byte is non-zero.

Each frame:
1. **Time step** dt = game clock − game clock at the previous terrain frame, clamped to 0 … 0.1 s. The game clock
   stops while the game is paused, and so does the terrain. At selection time the previous time is set to the clock
   then.
2. **Scroll.** If dt ≠ 0, every vertex moves by −speed·dt along X (3500 · dt for `train.trn`).
3. **Vertex brightness** b = 0.5 · max(0, (10000 − (|x − vx| + |y − vy|)) · 0.0001), where (vx, vy) is the view
   origin. This is a Manhattan distance in the horizontal plane: b = 0.5 at the viewer and 0 from 10 000 units on.
   If `r_fullbright` ≠ 0, b = 1.
4. **Behind test.** A vertex is behind when (vertex − view origin) · view forward < 0. Forward comes from the view
   angles.
5. **Recycling.** Let R be the rearmost row. If R's x < vx − N·S/2, R is regenerated as the new frontmost row at
   x = R's old x + N·S, with fresh random values, the current clock in the waves, and the old frontmost row as its
   neighbour for the t range. **At most one row is recycled per frame.**
6. **Draw the quads** (state below). A quad is skipped only when all four corners are behind. Each corner gets
   colour (0.46·b, 0.89·b, b) with alpha 1, where b is that corner's brightness. This gives the bluish-green fade to
   black with distance.
7. **Clouds**: move, then draw (see *Clouds*).

**Render state for the quads.**
- Blending on with (source alpha, one − source alpha). The texture's alpha is used.
- Gouraud shading; texture environment *modulate*.
- Face culling **off**: both sides of a quad are visible.
- Depth test as left by the world pass (enabled). Depth writes on.

Blending stays enabled and culling disabled when terrain drawing ends. Later passes set their own state.

**Fog and buffer clears** apply only if fog end (0x22) ≥ 1.0. None of this happens for `train.trn`, whose fog end is
0.
- Before the quads: linear fog is enabled with start/end from the header and colour (R, G, B, 0). If the clear flag
  is set, the colour and depth buffers are also cleared to (R, G, B, 0). This also erases the world already drawn
  this frame.
- After the quads: fog is disabled. If the clear flag is set, the depth buffer is cleared, and if `gl_clear` ≠ 0 the
  colour buffer as well.
- Clouds are drawn after this point and are never fogged.

#### Clouds

When a set is built, 32 clouds are created; this happens again on every successful selection. Every random draw
uses the engine's flrand.

| Property | Value |
|---|---|
| Centre x, y | uniform in −16383 … 16383, redrawn until the placement rule accepts it |
| Centre z | uniform 3000 … 5000 (only the corners are drawn, and their z is replaced every frame) |
| Half-extent H | 2 · uniform(1200, 1800), so each cloud is a square 2H = 4800 … 7200 units wide |
| Orientation | horizontal unit vector d from two uniform values in −1 … 1, normalised; p = (d.y, −d.x, 0) |
| Corners 1–4 | centre + H·(a·d + b·p) for (a, b) = (−1, −1), (−1, +1), (+1, +1), (+1, −1) |
| Velocity | (−400, uniform(−40, 40), 0) units per second |
| Texture | `textures/uganda/cloud` (wall image, fixed name, not from the file; "notexture" if missing) |

**Placement rule** for a candidate centre, checked against the clouds already placed. Rejected if:
- any placed cloud is closer than 800 units (horizontal distance); or
- a third placed cloud lies closer than 2000 units, i.e. at most two placed clouds may be within 2000.

There is also a random rejection with probability 2/3 when |y| > 8191. The original reads that lateral value from
the *first* cloud in the list, not from the candidate. So only the first cloud is biased towards |y| ≤ 8191; for
later clouds the test merely causes retries.

The direction vector is normalised from three components, and the third one is not initialised in the original.
Cloud size and shape may therefore differ from the table; UNVERIFIED. Check by looking up from the train roof in
`trn1` and comparing cloud sizes against 4800–7200 units.

**Every frame** (same dt as the terrain):
1. Each corner moves by velocity·dt. Its z is then set to 2500 + 1000 · (16383 − |corner x|) / 16383, so the cloud
   layer is a ridge 3500 units high over x = 0 that falls to 2500 at |x| = 16383.
2. The centre moves by velocity·dt.
3. If the centre's x or y leaves −16383 … 16383, that coordinate of the centre and of all four corners is shifted by
   ∓32766 (wrap-around).
4. Draw. Each cloud is one quad with texture coordinates (0, 0), (0, 1), (1, 1), (1, 0) on corners 1–4, colour
   white, blending (source alpha, one − source alpha), modulate, depth test on, **depth writes off**.

#### Statistics

`r_speeds 1` prints a line whose `tp` field is the number of terrain quads plus clouds drawn in that frame (0
without terrain). The full line format is in the renderer cvar spec.

#### Constants

| Name | Value |
|---|---|
| Signature byte | 0x22 |
| Header / group / column record sizes | 88 / 52 / 68 bytes |
| Sprite entry tail, texture entry tail | 20 / 21 bytes after the name |
| Section separators | 4 bytes, unchecked (stock `2D 42 01 00`) |
| dt clamp | 0 … 0.1 s |
| Brightness fade | 0.5 at 0, linear to 0 at 10 000 units (horizontal Manhattan distance) |
| Terrain tint | (0.46, 0.89, 1.0) × brightness |
| Clouds | 32; extent ±16383, wrap 32766; speed (−400, ±40, 0); height 2500–3500 |
| Cloud spacing | ≥ 800 units; at most 2 within 2000 |

#### Test vectors / how to verify

1. **Layout.** Parsing `env/train.trn` with the tables above consumes exactly 2149 of its 2153 bytes. The unread rest
   is the final 4-byte separator. N = 40, M = 23, S = 900, speed 3500, lateral offset 8300, height offset 150, 6
   slots.
2. **Version.** Copy `train.trn` with byte 0 changed to `#`, run `terrain <copy>`: console prints
   `Invalid version for Terrain Set <copy>` and the landscape disappears. With byte 1 set to 0: no error and no
   landscape.
3. **Off.** `terrain ""` or a name of `(null)`: landscape gone, nothing printed. `terrain nosuch`: prints
   `nosuch isn't a valid terrain set name`.
4. **Scroll and pause.** In `trn1` the ground passes at 3500 units/s and stops while paused. Clouds drift backwards
   at 400 units/s.
5. **Sprites.** No bush sprites appear in `trn1`, although the file lists two.
6. **Counts.** `r_speeds 1` in `trn1`: `tp` is at most 858 + 32 and changes with view direction (quads behind
   the viewer are skipped).
7. **Fullbright.** `r_fullbright 1` makes the terrain uniformly tinted (0.46, 0.89, 1.0) × texture, with no fade.

#### Open questions

- Header 0x02, 0x33–0x4A, the "sun" byte and vector, column offsets 4 and 24–31, group offsets 12–16 and the two
  row waves: read but without effect in 1.07f. Probably leftovers of removed features (sun flare, bushes, row
  sway).
- The uninitialised third component in the cloud orientation (see *Clouds*).

### A3D geometry export

Export slot 0xd8 (SPEC-002) lets the Aureal A3D sound library (A3Dsnd.dll) obtain world polygons for its
occlusion and reflection model. Only the sound library calls it. A3Dsnd.dll calls it once per sound update, only
while its A3D geometry object exists and only when its unit scale (below) is > 0.

#### Arguments

| # | Type | Meaning |
|---|---|---|
| 1 | COM pointer | The A3D root object (A3D 3.0 `IA3d4`) |
| 2 | COM pointer | The A3D geometry object (`IA3dGeom`), obtained by the sound library from argument 1 |
| 3 | COM pointer | The material (`IA3dMaterial`) to bind for all world polygons |
| 4 | pointer | Control block, below. The renderer reads it and temporarily modifies two fields; the sound library sets word 0 back to 0 after each call |

**Control block** (32-bit words; values are A3Dsnd.dll's initial values):

| Word | Type | Initial | Meaning for the renderer |
|---|---|---|---|
| 0 | int | 2 | **Reset request.** 0 = normal. Non-zero = discard all cached geometry and rebuild. 2 = the geometry object is new: forget cached lists **without** releasing them. Any other non-zero value releases them first. The sound library sets 2 when it creates the geometry object, 1 when one of its geometry settings changes, and 0 after every call |
| 1 | float | 1.0 | Unit scale: world units per A3D unit. Geometry is scaled by 1 / this. The sound library refreshes it before every call and skips the call when it is ≤ 0 |
| 3 | uint | 200 | Budget for submitting lists (see *Submission*) |
| 9 | float | 20000 | Minimum squared distance (world units²) the view must have moved since the last rebuild before a leaf change triggers a new rebuild |
| 14 | float | 1·10¹⁰ | Minimum size of a triangle: squared length of the cross product of two edges, i.e. (2·area)². Smaller triangles are not sent |
| 17 | float | 0 | Triangles whose (2·area)² is below this value get render-mode flags 0x40, the others 0x42 |

Other words carry sound-library settings that the renderer ignores: 2 polygon bloat, 4 and 5 reflection delay and
gain scale, 6 material reflectance, 7 and 8, 10, the enable flags 11–13, 15, 16 and 18. They belong to the A3D sound
spec.

#### Caching

The renderer keeps one A3D *list* per world leaf. A list holds the recorded polygons of that leaf.
- A table holds up to 19 × 50 entries; leaves are hashed into 19 buckets.
- Each entry has a generation stamp. A rebuild ages the table: every entry not used in the current or previous
  rebuild (stamp more than one generation old) has its list released.
- If a leaf's bucket is full, its new list is released at once and the leaf sends nothing. The debugger output
  shows `RenderLeaf: failed to store in C…`.
- If the geometry object cannot create a list, the debugger output shows `RenderLeaf critically failed to c…`.
- The whole table is discarded when control word 0 is non-zero or when the world model has changed since the last
  call (a new map).

#### Each call

1. **Reset check** as above.
2. **View leaf.** The renderer finds the world leaf containing the view origin of the **last rendered frame**, not
   the sound listener. Without a world, or if no leaf is found, nothing more happens.
3. **Rebuild decision.**
   - Control word 0 non-zero: rebuild. The reset also forgets the remembered leaf, so the next normal call
     rebuilds again once the distance condition below holds.
   - Otherwise, if the view leaf equals the leaf of the last rebuild: no rebuild.
   - Otherwise, if the squared distance from the view origin to the position of the last rebuild is less than
     word 9: no rebuild (and the leaf is not remembered, so the test repeats next call).
   - Otherwise: rebuild. The current position and leaf are remembered, and the cached lists of the old and the new
     view leaf are released so they are rebuilt with the detail level of their new role (step 6).
4. **Transform** (every call, even without a rebuild): on the geometry object, load identity, rotate 90° about
   (1, 0, 0), then scale uniformly by 1 / word 1.
5. **Without a rebuild the call ends here**: no lists are submitted.
6. **Rebuild.**
   - Call method 0x30 of the root object; it takes no arguments (`Clear` in the A3D 3.0 headers; UNVERIFIED).
     Bind the material (argument 3) and age the cache.
   - **Leaf set:** walk the BSP from the root. Only nodes and leaves marked visible in the renderer's current PVS
     frame (the marks made for the last rendered view) are visited. Solid leaves are skipped. At most 1000 leaves are
     collected.
   - **Order:** ascending squared distance between each leaf's bounding-box centre and the view leaf's
     bounding-box centre. The order of equal distances is not defined.
   - **Lists:** take each leaf's cached list, or build it (below). The first leaf in the order, normally the view
     leaf itself, is built with thresholds words 14 and 17 multiplied by 0.01 (100 times finer detail). Both words
     are restored afterwards by multiplying by 100. A cached list keeps the detail level it was built with.
7. **Submission.** The lists are called in order (list method 0x14, `Call`). The renderer adds up the values they
   return and stops before the next list once the sum is ≥ word 3. With word 3 = 0 nothing is submitted. If `Call`
   returns 0 on success (S_OK), the budget never stops the loop (UNVERIFIED).

#### Building a leaf's list

1. Create a list (geometry method 0x94, `NewList`) and begin recording (list method 0x0c).
2. For every surface of the leaf (the leaf's mark-surface list: world faces including sky, water and translucent
   faces; inline brush models are not included):
   - begin a primitive (geometry method 0x68) with type 3, then call geometry method 0x0c with value 2;
   - triangulate the face as a fan from its first vertex A over its edge loop.
     - Collinear vertices are dropped first: if the next fan vertex C, the vertex B before it and the vertex D after
       it satisfy |(C − B) × (D − B)|² < 1·10⁻⁶, C is skipped.
     - For each fan triangle (A, B, C), Q = |(B − A) × (C − A)|².
     - If Q < word 14 (× 0.01 for the nearest leaf), the triangle is skipped.
     - Otherwise: set the render-mode flags (geometry method 0xa8) to 0x40 if Q < word 17 (× 0.01 for the nearest
       leaf), else 0x42; set a tag (geometry method 0x80) to the signed BSP edge number read last while forming the
       triangle (no stable meaning); emit vertices A, B, C (geometry method 0x70, three floats each) in world units;
   - end the primitive (geometry method 0x6c).
3. End recording (list method 0x10).
4. If fewer than 3 triangles were emitted for the whole leaf, the list is released and the leaf contributes
   nothing (and is not cached, so it is retried at the next rebuild).

**Interface methods used** (byte offsets in the COM method tables; names from the public A3D 3.0 SDK headers, mapping
UNVERIFIED against the headers):

| Object | Offset | A3D 3.0 name | Use |
|---|---|---|---|
| root | 0x30 | Clear | once per rebuild |
| geometry | 0x0c | Enable | value 2 at the start of every surface |
| geometry | 0x40 | LoadIdentity | every call |
| geometry | 0x58 | Rotate3f | (90, 1, 0, 0) every call |
| geometry | 0x60 | Scale3f | (1/scale) ×3 every call |
| geometry | 0x68 / 0x6c | Begin / End | per surface, Begin(3) |
| geometry | 0x70 | Vertex3f | world-unit vertex |
| geometry | 0x80 | Tag | per triangle |
| geometry | 0x90 | BindMaterial | argument 3, per rebuild |
| geometry | 0x94 | NewList | per uncached leaf |
| geometry | 0xa8 | SetRenderMode | 0x40 or 0x42 per triangle |
| list | 0x08 | Release | eviction, reset, tiny leaves |
| list | 0x0c / 0x10 | Begin / End | recording |
| list | 0x14 | Call | submission |

#### Test vectors / how to verify

1. Load the original ref_gl.dll from a probe "sound library" that passes logging fake COM objects and a control
   block with the initial values above. In a map, the first call (word 0 = 2) shows Clear, BindMaterial,
   NewList/Begin/…/End per leaf, and Call on lists. The next call, with word 0 = 0 and an unmoved view, shows only
   LoadIdentity, Rotate3f(90, 1, 0, 0) and Scale3f(1, 1, 1).
2. Move the view across a leaf boundary by less than 141.4 units since the last rebuild: no rebuild. Move further:
   rebuild, and only leaves whose lists were evicted or newly visible get NewList calls.
3. Every emitted triangle satisfies |(B − A) × (C − A)|² ≥ 1·10¹⁰ (≥ 1·10⁸ in the view leaf's list), and the
   vertices equal BSP vertex positions.
4. Set word 3 = 0: no Call is made.

#### Open questions

- Meaning of primitive type 3, of the per-surface `Enable(2)` and of render-mode flags 0x40/0x42 in A3D terms
  (likely "occlusion only" and "occlusion + reflection"). Check against the A3D 3.0 SDK `ia3dapi.h`.
- What `IA3dList::Call` returns, and whether the word-3 budget can ever stop submission.
- Whether the sound library changes words 9, 14 and 17 from their initial values (cvars); this belongs to the A3D
  sound spec.

### World diagnostics

| cvar | Effect |
|---|---|
| `r_drawworld` 0 | no world surfaces, sky or terrain |
| `gl_drawflat` 1 | surfaces untextured, each in a colour derived from its identity; 2 also blends lightmaps |
| `gl_drawmode` ≠ 0 | world and brush models drawn see-through: no depth test, colour (1,1,1,0.4), blended, texture modulated |
| `gl_showtris` 1 | triangle outlines over the world without depth test; 2 with depth test; ≥ 3 also the lightmap sample grid (red lines on faces with scale 1 × 1, blue otherwise); 4 grid only |
| `gl_lightmap` ≠ 0 | lightmaps only (two-pass path only) |
| `r_fullbright` ≠ 0 | no lightmaps |
| `gl_dynamic` 0 | lightmaps never rebuilt after load |

## Constants

| Name | Value |
|---|---|
| Lightmap page | 128 × 128 texels, linear filtering |
| Lightmap pages | page 0 dynamic + up to 127 static |
| Max samples per face | 12 288 |
| Sample spacing | 16 × `lm_scale` texture units |
| Dynamic light cut-off | 64 (marking and lightmaps) |
| Liquid subdivision | 64 units |
| Liquid texture divisor | 64 |
| Wave constants | 2.3, 1.7, 0.015, 2, 2.7, 3.37, 0.5625, 0.125, 4.5, 0.1, 0.038889, 0.6 |
| Flow scale | 0.01 texture per unit of value per second |
| World texture animation | 2 frames per second of game clock |
| Max animation frames counted | 32 |
| Sky box half-size | 37 800 |
| Fog surfaces per pass | 1024 |
| Detail surfaces per pass | 1024 |
| Boundary planes per region | 5 |
| Shimmer table | 10 entries, phase 0..20, speed 0.05..0.3 |
| Far fog wall | 2000 ahead, ±2500 |
| Hardware-fog start offset | +90 |
| Alpha test | greater than 0.05 |
| Light probe length | 2048 down, or 256 along a direction |
| Light miss value | 0.1 |
| Light cache | 32-unit cells, 1000 entries max |
| Model-light radius limit | 1500 |
| `r_lightlevel` scale | 150 |

## Test vectors / how to verify

1. **Lightmap scale.** In a stock map, find a face with `lm_scale` 2 (SPEC-101 data) next to one with scale 1. Screenshot
   with `gl_showtris 3`: the grid on the scale-2 face is blue and twice as coarse.
2. **Flowing.** In `bos*` maps, texinfo `bosnia/water4b` value 128 scrolls 1.28 textures per second along t; time a full
   texture cycle (≈ 0.78 s).
3. **Texture animation by rate.** A texture with texinfo flag 0x1000 and `.m32` value 4 changes frame 4 times per second;
   with value 0 it changes twice per second.
4. **Water opacity.** `uganda/water2` (flags 0x2018) is expected opaque with moving waves and brightness ripple; verify in
   game (trn/ugn maps) — if it is translucent, this spec is wrong about trans33/66.
5. **Waves.** On a 0x2018 surface, vertex heights move by up to ±4.5 units; check by viewing the water edge against a wall.
6. **Fog alpha.** In `irq3b`, region with opacity 0.88, start 4, range 580: a fog-region vertex 200 units from the eye
   gets alpha `floor((40000 − 4)/580)` = 68; at 400 units it is capped at `floor(0.88 × 255)` = 224.
7. **Height fog.** In `ger1` (flag 2), fog is densest at the bottom of the volume and absent at its top face.
8. **Far wall.** Stand in a region with offset 36 ≠ 0 (`tsr1`, `nyc2`, `sud3`): geometry beyond ~2000 units ahead is
   covered by the fog colour. `gl_fogmode 2` removes it.
9. **gl_fogcolor.** `gl_fogcolor "255 0 0"` turns all fog red; `gl_fogcolor -1` restores map colours.
10. **Sky none.** `sky none` (all lines `none`): sky surfaces show whatever the colour buffer contained (no clear with
    `gl_clear 0`).
11. **Light sampling.** Export slot 0xa4 at a point above a fully lit floor (style 0, sample 255, `gl_modulate 1`)
    returns (1, 1, 1); above void (no hit) returns (0.1, 0.1, 0.1).
12. **Model lights.** A lights record of type 0 with intensity 40 000 gives a Ghoul light radius of 100; type 3 records are
    ignored.
13. **Detail distance.** With `gl_detailtexturing 2` and `gl_detailtexturingdistance 64`, detail texture disappears from
    walls more than 64 units ahead.
14. **Alpha image without the alpha flag.** In `nyc2`, the world faces textured `subway/2_watersteam` (texinfo flags
    0x29, image flag 0x800) are expected to be invisible; in `jpn1` the inline-model gratings (`tokyo/1_grating`, texinfo
    flags 0) are expected to be drawn with cut-out holes and no blending.

## Open questions

- Region offsets 0, 20, 40 and flag bit 4: not used by the renderer; they may matter to the engine or the map compiler.
- Region offset 36 non-zero values (128–5000): the renderer only tests for non-zero. Possibly an intended wall distance.
- Point fog uses the parameters of the most recently processed region instead of the point's own region: verify with two
  differently coloured fog regions in view and a model in each.
- Exact light-sampling point for alpha faces of inline models (bounding-box centre assumed).
- Whether damaged surfaces persist across a same-map restart.
- The thickness formula `D_0 · b² / (a² + b²)` is not the exact path length through the plane; it is reproduced as observed.
