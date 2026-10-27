# SPEC-101 — `.bsp` maps, IBSP version 46

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — all 76 `maps/**/*.bsp` in `pak0` (52), `pak2` (19), `pak3` (5). No game binary was
consulted. Reference for the Quake II layout: `qcommon/qfiles.h` of the Quake II 3.19 GPL source
(`BSPVERSION 38`).
Implementation: `tools/formats/bsp.py`

## Purpose

Compiled level geometry, BSP tree, visibility, lightmaps and entity text. The format is the Quake II IBSP format
with a new version number, larger face and leaf records and three additional lumps.

## Layout

### Header (184 bytes)

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | char[4] | `IBSP` (as Quake II) |
| 4 | 4 | int32 | version **46** (Quake II: 38) |
| 8 | 22 × 8 | {int32 offset, int32 length}[22] | lump directory (Quake II: 19 entries) |

The first lump in the file (planes) starts at 184 = 8 + 22·8 in all 76 maps. Lumps are 4-byte aligned, never
overlap, and the last lump ends at end of file. Empty lumps have length 0 and an offset inside the file.
Lump order on disk is not the directory order (planes, leafs, vertexes, nodes, texinfo, faces, brushes,
brushsides, leaffaces, leafbrushes, surfedges, edges, models, areas, areaportals, lights, lighting, visibility,
entities, pop); readers must use the directory.

### Lump directory

| # | Name | Record | Same as Quake II? | Notes |
|---|---|---|---|---|
| 0 | entities | text | yes | `{ "key" "value" … }` blocks, NUL-terminated (76/76) |
| 1 | planes | 20 | yes | `float normal[3], dist; int type` |
| 2 | vertexes | 12 | yes | `float point[3]` |
| 3 | visibility | — | yes | `dvis_t` + run-length PVS/PHS rows (checked on all maps) |
| 4 | nodes | 28 | yes | `dnode_t` |
| 5 | texinfo | 76 | layout yes | `float vecs[2][4]; int flags, value; char texture[32]; int nexttexinfo`. New flag bits, see below |
| 6 | faces | **44** | **no** (Q2: 20) | see Face |
| 7 | lighting | bytes | partly | RGB8 samples; block sizes use per-face scale, see Lightmaps |
| 8 | leafs | **32** | **no** (Q2: 28) | see Leaf |
| 9 | leaffaces | 2 | yes | uint16 face index |
| 10 | leafbrushes | 2 | yes | uint16 brush index |
| 11 | edges | 4 | yes | uint16 v[2] |
| 12 | surfedges | 4 | layout yes | int32; also holds face sub-polygon loops (lump 19) |
| 13 | models | 48 | yes | `dmodel_t`; model 0 = world, faces of models are contiguous from 0 |
| 14 | brushes | 12 | yes | `firstside, numsides, contents` |
| 15 | brushsides | 4 | yes | `uint16 planenum; int16 texinfo` |
| 16 | pop | 256 | yes | all zero in 76/76 |
| 17 | areas | 8 | yes | `numareaportals, firstareaportal`; area 0 = (0, 0) |
| 18 | areaportals | 8 | yes | `portalnum, otherarea` |
| 19 | facepolys | **8** | **new** | non-empty in 31 maps |
| 20 | lights | **36** | **new** | non-empty in 76 maps |
| 21 | volumes | **76** | **new** | non-empty in 33 maps |

Size ranges over the 76 maps: faces 73 392–991 188 bytes, lighting 205 749–2 392 440 bytes (always a multiple of 3),
lights 2 880–335 340 bytes, facepolys 0–157 152 bytes, volumes 0–2128 bytes (0–28 records).

### Face (lump 6, 44 bytes)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 2 | uint16 | `planenum` | as Q2 |
| 2 | 2 | int16 | `side` | 0/1, as Q2 |
| 4 | 4 | int32 | `firstedge` | into surfedges, as Q2 |
| 8 | 2 | int16 | `numedges` | as Q2 |
| 10 | 2 | int16 | `texinfo` | as Q2 |
| 12 | 2 | int16 | `volume` | −1 or index into lump 21 |
| 14 | 2 | uint16 | — | always 0 |
| 16 | 4 | int32 | `firstpoly` | −1 or index into lump 19; −1 exactly when `volume` = −1 |
| 20 | 2 | uint16 | `numpolys` | 0 when `firstpoly` = −1, else 1–40 |
| 22 | 4 | uint8[4] | `styles` | light styles; slot 0 is always 0 on lit faces; unused slots 255 (filled left to right) |
| 26 | 2 | uint16 | — | always 0 |
| 28 | 4 | int32 | `lightofs` | byte offset into lighting |
| 32 | 1 | uint8 | `lm_width` | lightmap samples along s |
| 33 | 1 | uint8 | `lm_height` | lightmap samples along t |
| 34 | 1 | uint8 | `lm_scale_s` | 1 or 2 (0 on unlit faces) |
| 35 | 1 | uint8 | `lm_scale_t` | 1 or 2 (0 on unlit faces) |
| 36 | 4 | int16[2] | `lm_mins` | texture-space minimum (s, t), multiple of 16 |
| 40 | 4 | int16[2] | `lm_extents` | texture-space extent (s, t), multiple of 16 |

The first 12 bytes are Quake II's `dface_t` prefix; styles and lightofs moved; the lightmap geometry that Quake II
computes at load time is stored.

### Leaf (lump 8, 32 bytes)

| Offset | Size | Type | Field | Meaning |
|---|---|---|---|---|
| 0 | 4 | int32 | `contents` | as Q2 (plus bit 0x100, see Volumes) |
| 4 | 2 | int16 | `cluster` | as Q2 |
| 6 | 2 | int16 | `area` | as Q2 |
| 8 | 2 | int16 | `volume` | **new**: −1 or index into lump 21 |
| 10 | 12 | int16[6] | `mins[3], maxs[3]` | as Q2 (mins ≤ maxs in all 414 203 leafs) |
| 22 | 8 | uint16[4] | `firstleafface, numleaffaces, firstleafbrush, numleafbrushes` | as Q2 |
| 30 | 2 | uint16 | — | always 0 |

Leaf 0 is all zero except `contents` = 1 in every map.

### Facepoly (lump 19, 8 bytes)

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 4 | int32 | `firstedge` (into surfedges) |
| 4 | 4 | int32 | `numedges` (3–12) |

A face with `firstpoly ≥ 0` owns `numpolys` consecutive facepolys. Their surfedge loops are stored right after the
face's own loop and before the next face's loop (85 194/85 194 faces), so surfedges are interleaved
`[face][poly 1][poly 2]…[next face]`. The loops cut the face into smaller polygons using extra vertices
(e.g. a 4-edge face split into two quads sharing the original corners). 84 764 of these faces have all poly
vertices within 0.1 unit of the face plane; 80 deviate by ≥ 1 unit (up to 160, mostly in `dm/sibctf1`), cause
UNKNOWN. Only faces with a `volume` have facepolys; plausibly a tessellation for per-vertex volume (fog) effects,
UNVERIFIED.

### Light (lump 20, 36 bytes)

| Offset | Size | Type | Field | Observed |
|---|---|---|---|---|
| 0 | 12 | float[3] | `origin` | world position |
| 12 | 12 | float[3] | `color` | 0..1, matches the `_color` key of light entities |
| 24 | 4 | float | `intensity` | entity `light` value for point lights (e.g. 90, 550); large (10⁴–10⁶) for types 0 and 3 |
| 28 | 4 | int32 | `type` | 0–3 |
| 32 | 4 | int32 | `unknown` | 0 (141 287), 1 (17), 2 (61) |

Type counts over all maps: 0 = 22 180, 1 = 19 803, 2 = 2 839, 3 = 96 543. Correlations with entity text:

- type 2 count = number of `light*` entities with `_cone`, `_spotlight` or `target` in 68/76 maps → **spotlights**;
- type 1 count = the remaining `light*` entities in 38/76 maps (differences ≤ 20) → **point lights**;
- type 3 present iff worldspawn has `_sun_*` keys in 74/76 maps; records lie on a regular grid → **sky/sun emitters**
  (likely sky-surface patches);
- type 0 present iff some face has a `SURF_LIGHT` texinfo in 74/76 maps → **surface-light patches**.

These assignments are strong correlations, not proofs. What the game uses the list for (dynamic lighting of models
is the natural candidate) is UNKNOWN.

### Volume (lump 21, 76 bytes)

210 records in 33 maps. Columns as observed (all 210 records):

| Offset | Type | Values | Notes |
|---|---|---|---|
| 0 | int32 | 0 (186), 2 (23), 1 (1) | kind? `dm/sibctf1` (whole map in snow) has 2 |
| 4 | float | 0.2–1.0 (16 distinct) | density / opacity? |
| 8 | float | 0 (199), 1 (11) | |
| 12 | float | 4 (206), 1, 0 | |
| 16 | float | 10–5000 (20 distinct; 400 most common) | distance? |
| 20 | float | 0, 16, 32, 128 | |
| 24 | uint8[4] | R, G, B, 255 | colour (e.g. 20 40 40, 43 51 66) |
| 28 | int32 | 0, 2, 4, 5 | |
| 32 | float | 1.0 (all) | |
| 36 | float | 0–5000 | |
| 40 | float | 128, 256, 512 | |
| 44 | 32 bytes | zero | reserved |

References: face `volume` (faces inside or bounding the volume; in `dm/sibctf1` 17 307 of 17 308 faces point to
record 0) and leaf `volume` (36 167 leafs). Every leaf whose contents has bit **0x100** has a volume index
(12 436/12 436); 207 brushes carry contents 0x100. No entity key mentions the volume parameters, so their source
is UNKNOWN. The colour field and the context (fogged snow/sewer/smoke areas) suggest fog volumes; this is an
interpretation.

### Texinfo flags

Same layout as Quake II; `texture` names resolve to `textures/<name>.m32` in 219 199/219 199 texinfos.
The high byte is the material id of SPEC-100 (agrees with the `.m32` in 99.75 %); the low bits are SPEC-100 surface
flags plus bits that never occur in a `.m32`: 0x2000 (19 texinfos, flowing liquids), 0x8000 (228), 0x10000 (458),
0x20000 (44), **0x200000 (9372)**, 0x400000 (6), 0x800000 (8). Their meanings are UNKNOWN. `nexttexinfo` chains
follow the `.m32` `anim_next` links (1358/1358).

### Lightmaps

- Each lit face stores one block per used style (styles ≠ 255), consecutively from `lightofs`.
- A block is `lm_width × lm_height` samples, rows along t, samples along s, **3 bytes R, G, B** each (no alpha,
  no padding).
- `lm_width = lm_extents[0] / (16·lm_scale_s) + 1`, `lm_height = lm_extents[1] / (16·lm_scale_t) + 1` (integer
  division) — holds for all 663 827 lit faces. So one sample covers 16 texture units at scale 1 and 32 at scale 2;
  Quake II has a fixed 16. Sizes reach 119 samples per side; the Quake II software renderer rejects extents > 256
  (17 samples, `ref_soft/r_model.c`) and `ref_gl` packs lightmaps into 128-wide pages (`gl_rsurf.c`), so an
  engine needs pages of at least 119 samples.
- `lm_mins`/`lm_extents` equal Quake II's `floor(min/16)·16` / `ceil(max/16)·16 − mins` of the face's texture
  coordinates in 96.5 % of face axes; in the rest the stored bound is 16 tighter, and the vertex lies within 0.01
  unit of the grid line (compiler epsilon). Use the stored values.
- Sample (i, j) is taken to lie at texture position `(lm_mins[0] + 16·lm_scale_s·i, lm_mins[1] + 16·lm_scale_t·j)`.
  This is inferred and consistent with the renders (light pools align with fixtures and walls), not measured.
- Unlit faces (23 305: every sky and warp texinfo) have styles 0,0,0,0, `lm_*` = 0 and `lightofs` = 0.
- In 131 faces that use all four style slots the lighting data holds 5 or 6 blocks before the next face's block.
  Read blocks only from `lightofs` and the style count.
- No two lit faces share a `lightofs`; blocks are packed in face order. The last block ends at the end of the lump
  in 76/76 maps.
- Surfaces with `SURF_LIGHT` carry their own emission colour in their lightmap (e.g. `tokyo/3_red`, value 2000,
  is pure red).

## Validation

`python tools/formats/bsp.py validate <extracted paks>`: **76 parsed, 0 failed** (687 132 faces, 141 365 lights,
210 volumes, 124 279 facepolys). Checks per map: lump bounds and no overlap; all lump lengths are whole records;
plane normals unit length and type 0–5; every edge, surfedge, face, node child, leaf list, leafface, leafbrush,
model, brush, brushside, area, areaportal and facepoly index in range; `nexttexinfo` in range; face
`volume`/`firstpoly` presence agree; lightmap size rule; lit faces have style 0 first; lightmap blocks inside the
lighting lump; leafs with contents 0x100 have a volume; light types 0–3; every PVS row run-length-decodes to
exactly ⌈clusters/8⌉ bytes.

Visual checks (`<openmerc-data>\renders\`):

- `jpndm2_topdown_lit.png`: orthographic view from +Z of `maps/dm/jpndm2`, z-buffered, sampling `.m32`
  level 0 through the texinfo vectors and modulating by the bilinear lightmap.
- `jpndm2_topdown_lightmap.png`: same view, lightmap only.
- `irq1a_topdown_lit.png`: outdoor single-player map.
- `jpndm2_lightmap_atlas.png`: first-style block of all 4333 lit faces, shelf-packed, ×2.
- `obj/jpndm2.obj` + `.mtl`: world model (4393 faces, 20 460 vertices, 128 materials named by texture);
  `jpndm2_obj_isometric.png` is drawn from the OBJ file read back from disk.

## Open questions

1. Lump 21 field meanings (fog density, distances, kind) and where the values come from at compile time.
2. Lump 19 use, and the 80 faces whose sub-polygons leave the face plane.
3. Lump 20 type assignments (strong correlations only) and the trailing int (0/1/2).
4. Texinfo flags 0x2000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000, 0x800000.
5. How the compiler chooses `lm_scale_s/t` (not explained by face size or texinfo; mixed within one texinfo).
6. Extra lightmap blocks on 131 four-style faces.
7. Exact luxel position within a sample cell (assumed corner-aligned as in Quake II).
