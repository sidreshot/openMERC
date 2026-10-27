# SPEC-120 — Ghoul binary model container (.ghb)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: the 498 `.ghb` files under `ghoul/` in the stock data (`pak0.pak` 477, `pak2.pak` 20,
`pak3.pak` 1; 188 MB).  No game binary was examined.
Related: SPEC-121 (the `.gsq/.gpm/.gbm/.gpl/.ifl` text files that select models, parts and skins).
Tools: `tools/formats/ghb.py` (parser, geometry, OBJ export), `tools/formats/ghb_render.py`
(software preview renderer).

## Purpose

A `.ghb` holds one Ghoul model: its animation sequences (with timed events), materials,
a flat list of named nodes (attachment "bolts" and helpers), the meshes, one shared
geometry block, per‑frame vertex animation, per‑frame node transforms and lights.
Characters (`enemy/meso/MESO_*.ghb`, 4–8 MB) are **vertex‑animated**: every frame stores
vertex positions.  There is no skinning skeleton in the file; "bones" exist only as nodes
whose transforms are stored per frame (bolt points for weapons, accessories, gore caps).
The "bind pose" of a character is therefore just one of its frames.

Notation: all integers little‑endian.  `STR` = `u32 n` + `n` bytes (Latin‑1, no terminator,
no padding).  `MAT` = `f32[16]` row‑major 4×4 matrix with the translation in elements 12–14
(row vector convention, `p' = p·M`) followed by `u32 is_identity` (1 iff the matrix is the
identity; agrees with the matrix in all 12937 constant matrices of the stock data).  "UNKNOWN" marks fields whose
meaning cannot be inferred from the data; observed values are listed.

### Animatable value ("TRACK")

Several values may be constant or per frame.  Encoding:

| Size | Type | Meaning |
|---|---|---|
| 1 | u8 | `is_const` |
| … | value | if `is_const`: one value |
| 4 | u32 | else: `n` (= number of model frames in every stock case) |
| … | value[n] | else: one value per frame |

## Layout

The file has two parts: **part A** (structured description, starts at 0) and **part B**
(bulk data, starts at `size_a`).  `size_a + size_b` = file length in all 498 files.

### Header

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | u32 | magic `0x198237FE` |
| 4 | 4 | u32 | `size_a` — length of part A = file offset of part B |
| 8 | 4 | u32 | `size_b` — length of part B |
| 12 | 4 | u32 | `0x033FB1A3` (constant in all files; tag/version, UNKNOWN) |
| 16 | 4 | u32 | 74 (constant; format version, UNKNOWN) |
| 20 | 4 | u32 | 0 in all files (UNKNOWN) |
| 24 | … | STR | source file path of the exporter (`…\FLARE.GHL` 423×, `…\PISTOL1.GFL` 74×, `.IFL` 1×) |

### Sequences

| Type | Meaning |
|---|---|
| u32 | `frames` — total number of model frames |
| u32 | `nseq` |
| SEQ[nseq] | see below; sequences are contiguous and tile `0…frames-1` exactly (498/498) |

SEQ:

| Type | Meaning |
|---|---|
| f32[3] | UNKNOWN; 0,0,0 in all 6448 sequences (hypothesis: per‑sequence root motion) |
| f32 | `frame_ms` — duration of one frame in milliseconds (100 in 5217 sequences; also 200, 66.67, 50, 83.33, 33.33, 250, 1000, 16.67, 333.33, 125, 37.5) |
| STR | sequence name (`FIRE1_A`, `STD_IALERT_N_P_N`, …; `.gsq` files list these names, SPEC‑121) |
| u32 | `nevents` |
| EVENT[nevents] | `f32 time_ms` (from the sequence start; `50.0` with `frame_ms` 50 = second frame), `i32` (−1 in all 4301 events, UNKNOWN), `STR type`, `STR argument` |
| u32 | first frame |
| u32 | frame count |

Event types seen: `fire` 1872, `dropweapon` 780, `thud` 566, `sound` 462 (argument = sound
path, e.g. `weapons/gpistol/reloadout`), `knee` 198, `effect` 139 (argument e.g.
`weapons/playermz/pistol1 flash`), `throw` 102, `jump` 78, `inair` 58, `still` 28,
`altfire` 10, `eos` 3.

### Materials

| Type | Meaning |
|---|---|
| u32 | `nmat` |
| MATERIAL[nmat] | |

MATERIAL:

| Type | Meaning |
|---|---|
| STR | name (`TRASHCAN`, `PISTOL1`, character materials `A`, `B`, `C`, `F`, `H`, `HD`, `MD`, `GZ`) |
| u8[6] | flags, UNKNOWN; observed patterns (bytes 0…5): all 0 (802); byte1=1 (90: `GZ` gore materials, some masks); byte0=1 (78: hats/faceplates — bolt‑ons whose `.gbm` re‑skins them); byte2=1 (63: `EXPLODE`, `NAV_LIGHTS`, `SPRITEBALL`, `BEAM`, `HLIGHT` — glowing/additive‑looking surfaces); byte0+1 (38: glasses, goggles, `G_BRAIN_TRANS` — translucent‑looking); byte0+2 (7: light beams) |
| u32 | `nslots` (= 2 in all 1078 materials) |
| SLOT[nslots] | texture slots |
| TRACK colour (f32[3]) | property 0, e.g. (0.54, 0.20, 0.20) |
| TRACK colour (f32[3]) | property 1, e.g. (0.9, 0.9, 0.9) or (1,1,1) |
| TRACK f32 | property 2, e.g. 0.25, 0.15 |
| TRACK f32 | property 3, e.g. 0.05, 0.65 |
| TRACK f32 | property 4, e.g. 0.0, 0.38 |
| TRACK f32 | property 5, e.g. 1.0; animated in `EMERGENCY.ghb` (0, 0.5, 0, 0.5, … = blinking) |

The six properties look like a modelling package's diffuse colour, specular colour,
glossiness, specular level, self‑illumination and opacity (UNKNOWN; hypothesis based on
values only).

SLOT:

| Type | Meaning |
|---|---|
| STR | texture name; resolves to `<name>.ifl` / `<name>.tga` in the model directory or `ghoul/comskin` (SPEC‑121). Slot 0 = base texture (empty in 52 materials); slot 1 is empty in 1053 materials, otherwise a `*_SPEC` texture (`PISTOL1_SPEC`, `HANDS_SPEC`) |
| u32 | 0 in all slots (UNKNOWN) |
| u8[4] | flags, UNKNOWN: `01 00 01 01` with a texture, `00 00 00 00` without (also `01 00 00 00`, `00 00 01 01`) |
| u8 + … | image‑index TRACK with **u8** values: index into the `.ifl` image list per model frame (`FLASHPAK` 0,1,…,9,0; `tv` 0…5 repeating) |
| u8 + … | UV‑transform TRACK of `MAT` (animated in `ROTOR_ANIM.ghb`: rotation of the UV plane in 36° steps about (0.5, 0.5)) |
| u8 | UNKNOWN; equals the UV‑transform `is_const` byte in all 2156 slots |

### Model bounds

| Type | Meaning |
|---|---|
| u8[2] | 0,0 in all files (UNKNOWN) |
| f32[6] | model bounding box min xyz, max xyz |

### Node list

| Type | Meaning |
|---|---|
| u32 | `0x05DC159C` (section marker) |
| u32 | `nnodes` |
| NODE[nnodes] | |
| u32 | `0x05DC159D` (section marker) |
| u32 | 0 in all files (an always‑empty list; UNKNOWN content) |

NODE (also the first part of MESH and LIGHT records):

| Type | Meaning |
|---|---|
| STR | name |
| STR | user properties, empty except `NOBLOCK` (13×, sometimes with CR‑LF) |
| u8 | `vis_const` |
| … | if `vis_const`: `u8 visible` (0/1); else `u32 n` + `u32[ceil(n/32)]` bit set, bit *i* (LSB first) = visible in frame *i* |
| u8[5] | flags: byte2 = 1 exactly for the 13 `NOBLOCK` records; bytes 0,1,2 = 1 for `SPRITEBALL*` meshes (35), bytes 0,2 for `SPRITE` (3); otherwise 0 |
| f32[3] | minimum of the node's position over all frames |
| f32[3] | maximum of the node's position over all frames (= min for static nodes) |
| u32 | `ncomp` — 0, or number of frames of a compressed transform track |
| … | if `ncomp == 0`: transform TRACK of `MAT` (constant 10796×, per frame 4×) |
| f32[3] | else: `qscale` = (1023/range_x, 1023/range_y, 4095/range_z) where range = max − min (1023 when the range is 0) |
| f32[3] | `qinv` = 1/qscale |
| f32[3] | `qofs` = min |
| u32 | `data_offset` — offset in part B of `ncomp` × 8 bytes (see *Node tracks*) |

There is no parent index: all node transforms are given in model space for every frame
(a flat list).  Node names follow conventions: `ABOLT_*` accessory bolts, `WBOLT_*` weapon
bolts, `SBOLT_*` (mouth, heels), `GBOLT_*` gore bolts (`GBOLT_CAP_<A>_<B>_<type>`: where a
gore cap is attached when part A is severed from B; `GBOLT_HEAD_F`, `GBOLT_GUT_B` …),
`QUAKE_ORIGIN`, `QUAKE_GROUND`, `TO_*` (attachment point of a bolt‑on, SPEC‑121),
`SCENE ROOT`, `DUMMY01` …

### Mesh list and geometry

| Type | Meaning |
|---|---|
| u32 | `0x05DC159E` |
| u32 | `nmeshes` |
| MESH[nmeshes] | NODE fields, then `u8[2]` (0,0 always) and `f32[6]` bounding box |
| GEOMETRY | present only if `nmeshes > 0` (3 files have none) |

Characters carry every body‑part variant as separate meshes (`_BALDHEAD`, `_CREWHEAD`,
`_BOSSHEAD`, `_TIGHTCHEST`, `_BULKYCHEST`, …), gore caps `_CAP_<A>_<B>_<type>` (material `C`,
textures `c_black`/`c_blood`), and gore zones `GZ_*` (material `GZ`, gore‑material flag).
All meshes are visible in the file; `.gpm`/`_poff.gsq` switch them off (SPEC‑121).

GEOMETRY:

| Type | Meaning |
|---|---|
| i32 | −1 (all files; UNKNOWN) |
| u32[13] | counts, see below |
| u32[9] | offsets of the static sections (relative to the static block) |
| u8[2] | 0,0 (UNKNOWN) |
| f32[6] | geometry bounding box; `center` below = its midpoint (495/495) |
| f32[3] | `vscale` — int16 vertex quantisation, 100.8 in 490 files (smaller for very large models) |
| f32[3] | `vinv` = 1/vscale |
| f32[3] | `center` |
| f32[3] | 1/256 in all files (UNKNOWN; unused by anything decoded so far) |
| u8 | `chunked` — 1 = vertex animation is stored compressed in chunks (114 files) |
| … | if chunked: CHUNK HEADER (below) |
| u32 | `static_size` |
| u32 | `static_offset` — offset of the static block in part B |

Counts:

| # | Meaning |
|---|---|
| 0 | 1 in all files (UNKNOWN; number of geometry blocks?) |
| 1 | mesh count; equals `nmeshes` in 437 files, larger in 58 stripped character variants (e.g. `ECTO_ARM`: 118 vs 39 records) — UNKNOWN, hypothesis: mesh count of the full source model |
| 2 | material count (= `nmat`, 495/495) |
| 3 | `n_pos` static positions |
| 4 | `n_uv` texture coordinates |
| 5 | `n_norm` static normals |
| 6 | `n_surf` surfaces (a mesh may have one surface per material) |
| 7 | `n_idx` int16 index‑list entries |
| 8 | `n_corner` corners |
| 9 | `n_apos` per‑frame (animated) positions |
| 10 | 0 in all files (UNKNOWN; its section is always empty) |
| 11 | `n_anorm` per‑frame normals (stored as 1‑byte indices) |
| 12 | 0 in all files (UNKNOWN) |

CHUNK HEADER (only if `chunked`):

| Type | Meaning |
|---|---|
| u32 | `n_apos` again |
| u32 | `n_anorm` again |
| u32 | `frames` |
| u32[frames] | chunk index of every frame |
| u32 | `vanim_size` — bytes of chunk data in part B |
| u32 | `nodeanim_size` — bytes of compressed node tracks at the start of part B (= offset of the chunk data) |
| u32 | `nchunks` |
| CHUNK[nchunks] | 43 bytes each: `u32 first_frame`, `u32 nframes` (1…8, chunks never cross a sequence boundary, 15730/15730), `f32[3] scale`, `f32[3] offset`, `u8[3] bits` (per axis, 0 and 6…15; 0 = axis constant at `offset`), `u32 pos_offset`, `u32 norm_offset` (both relative to `nodeanim_size`) |

### Lights

| Type | Meaning |
|---|---|
| u32 | `0x05DC159F` |
| u32 | `nlights` (0–6; 100 lights in 42 files) |
| LIGHT[nlights] | NODE fields, `u32 type` (0: 74 lights, 1: 26), `u8` (0 always), then six TRACKs: position f32[3], colour f32[3], four f32 (e.g. 1.667, 1.0, 0.0, 150.0) |

Light names: `WORLD_OMNI`, `GUNOMNI1`, `MUZZLE_LITE`, `LIGHT`.  `NAV_LITE.ghb` animates the
colour 0→red and the last two scalars 100→30 over 11 frames.  Meaning of `type` and of the
four scalars is UNKNOWN (hypothesis: omni/spot; intensity, contrast, near and far
attenuation).

Part A ends exactly after the last light in all 498 files.

### Part B

Part B is tiled exactly, without gaps or overlaps, by (verified 498/498):

1. compressed node/mesh tracks (`ncomp × 8` bytes at each record's `data_offset`), together
   `nodeanim_size` bytes at the start;
2. chunk data (`vanim_size` bytes at `nodeanim_size`), if chunked;
3. the static block (`static_size` bytes at `static_offset`);
4. compressed light tracks (after the static block).

### Static block

Section *k* spans `offsets[k] … offsets[k+1]` (`offsets[9]` := `static_size`):

| Section | Content |
|---|---|
| 0…offsets[0] | material groups: `u32[3]` (material, first surface, surface count) per material; surfaces are sorted by material and the groups tile them exactly (495/495) |
| 0 | `f32[3] × n_pos` static positions (model space) |
| 1 | `f32[2] × n_uv` texture coordinates (u right, v down: v = 0 is the top row of the image) |
| 2 | `f32[3] × n_norm` static normals |
| 3 | `i16[3] × n_surf` surfaces: (mesh index, first index‑list entry, entry count); the surfaces tile the index list exactly |
| 4 | `i16 × n_idx` index lists (grammar below) |
| 5 | `i16[3] × n_corner` corners: (position, normal, uv) |
| 6 | non‑chunked models only: `i16[3] × n_apos × frames` per‑frame positions, **vertex‑major** (all frames of vertex 0, then vertex 1 …); position = `center + v × vinv`.  Evidence: the rigid lid of `TRASHCAN.ghb` and the hanging meat `SWING1.ghb` keep pairwise vertex distances within 0.02–0.03 units over all frames in this order (18–73 units of distortion with frame‑major order) |
| 7 | empty in all files |
| 8 | non‑chunked models only: `u8 × n_anorm × frames` normal indices, normal‑major (all frames of normal 0, then normal 1 …) |

Corner references: a **negative** value *v* means static element `~v` (= −v−1), a value
≥ 0 means per‑frame element *v*.  Positions: static `pos[~v]` or animated position *v*;
normals: static `norm[~v]` or animated normal index *v*; UVs are always static (negative).
All corner references are in range in 495/495 geometry blocks.

Index list of one surface (8103/8103 surfaces consume exactly their entry count):

```
repeat:  i16 c;  if c == 0: break
         |c| corner indices; c < 0: triangle fan, c > 0: triangle strip
i16 k;   k corner indices = k/3 independent triangles (k is a multiple of 3)
```

Triangle orientation: fan (c0,cj,cj+1); strip triangle j = (cj,cj+1,cj+2) for even j and
(cj+1,cj,cj+2) for odd j; list triangles as stored.  With this order `(p1−p0)×(p2−p0)`
points outwards: it agrees with the stored static normal in 41542 of 42682 corner tests,
and this strip parity gives the highest edge‑pairing consistency of the four
possibilities.

Normal indices (`u8`) select directions from a fixed 256‑entry table that is **not in the
data files**.  An empirical table is obtained from the stock models by averaging, for every
index, the outward face normals of all corners that use it (`ghb.py normals`): 250 of 256
indices occur (unused: 21, 24, 77, 135, 136, 154) and the estimate is highly consistent
(median resultant length 0.998 over 6698+ samples, no index below 0.97), so the indices
really denote fixed directions.  Entries 0…5 are +X, −X, +Y, −Y, +Z, −Z; entries 6…25 lie
around +Z towards +X/+Y (e.g. 6 = (0.21, 0, 0.98), 11 = (0.63, 0, 0.78), 20 = (0.89, 0,
0.46)), 26…40 around +Z towards −X/+Y, 41…55 towards −X/−Y, and so on — i.e. a sphere
tessellation ordered by regions.  The exact generating rule and the six unused entries are
UNKNOWN; the empirical table is accurate to a few degrees.

### Vertex animation chunks (chunked models)

For chunk *c* the position stream is `B[nodeanim_size + pos_offset … nodeanim_size + norm_offset]`
and the normal stream runs up to the next chunk's `pos_offset` (or `vanim_size`).

**Normal stream**: `n_anorm × k` bytes + 4 padding bytes, k = 1 for 1‑frame chunks, 2 for
2–4 frames, 3 for 5–8 frames (15730/15730).  Bytes are normal indices (table above), k per
normal, normal‑major (triples like `93 93 93` in `G_EXIT_LRG`, as in the non‑chunked
layout).  For 1‑frame chunks the indices agree with the geometry of the raw frame (median
dot product 0.994 between table direction and face‑derived normal, 2129 normals of four
characters).  For k = 3 the samples best match the first, a middle (≈ 4th of 8) and the
last frame of the chunk (26 moving normals of `FLAG`/`BANNER`/`PAIN`/`GONG`/`SWING1`; mean
dot product peaks at frames 0–1, 4 and 6–7 respectively), so the samples appear to be
time keys spread over the chunk; the exact key frames and how in‑between frames are
treated (nearest key vs. interpolation) is UNKNOWN; for k = 2 the hypothesis is first and
last frame.

**Position stream**: a bit stream read LSB‑first (bit *i* of the stream is bit *i mod 8* of
byte *i div 8*), followed by 4 padding bytes (bytes `00` or `CD`).  Quantised position
*q* (unsigned, `bits[a]` bits per axis a) maps to model space as
`offset[a] + q × scale[a]`.

* `nframes == 1`: the stream is `n_apos` raw records `(x: bits[0], y: bits[1], z: bits[2])`.
* `nframes > 1`: `n_apos` vertex records, each starting with a 2‑bit **mode**.

Mode 0 record (solved):

| Bits | Meaning |
|---|---|
| 2 | mode = 0 |
| bits[0], 4 | x of the first frame, `wx` |
| bits[1], 4 | y, `wy` |
| bits[2], 4 | z, `wz` |
| then for every further frame, for each axis with w > 0: w+1 bits *d* | value of that axis += *d* − 2^w (cumulative deltas; axes with w = 0 stay constant) |

Evidence: `ROTOR_START.ghb` frame 0 equals the static `ROTOR.ghb` mesh to quantisation
precision (7483 vs 7483.2, 1077 vs 1076.6, …); decoded rotor vertices keep their distance to
the hub within 0.05 units over all frames; the searchlight `ARM.ghb` box stays rigid
(pairwise distances change ≤ 0.06); vertex 0 of characters is continuous across chunk
boundaries (typical jump 0.04–0.3 units); the stream ends exactly 4 bytes before the
next section in every chunk containing only mode‑0 records.

Mode 1 record (solved) — the vertex track is an earlier vertex's track plus a small
per‑axis box:

| Bits | Meaning |
|---|---|
| 2 | mode = 1 |
| R = 1 if v ≤ 1, else ⌈log2 v⌉ (= bit length of v−1) | `ref`: index of an earlier vertex of the same chunk (< v) |
| 4 | `M` |
| per axis: 4, then M+1 if M > 0 | `w[a]`, then `min[a]` in sign‑magnitude (top bit of the M+1‑bit field = sign, low M bits = magnitude); M = 0 means no `min` fields (min = 0) |
| for **every** frame 0…n−1, for each axis with w > 0: w bits *u* | `q[f][v][a] = q[f][ref][a] + min[a] + u` (unsigned, no bias) |

Mode‑1 records make up about 78 % of all records (16k‑record sample); exact duplicates
(seam/cap vertices) are `M = 0, w = 0` records of 2+R+16 bits.  Modes 2 and 3 never occur
in the stock data.

Evidence: decoding every chunk of all 114 chunked files (15730 chunks) with modes 0 and 1
ends exactly 4 bytes before the next section in 15730/15730 chunks and produces no
quantised value outside `0…2^bits−1`; the rigid searchlight `ARM.ghb` matches the rigid
motion predicted from another chunk to ≤ 1 quantisation step; mode‑1 vertices of
characters are continuous across chunk boundaries like mode‑0 vertices (median boundary
step 1.20 vs 1.49 for neighbouring in‑chunk steps; 1.0 % of boundary steps exceed twice
the neighbouring step + 0.5) and match a following raw 1‑frame chunk as well as mode‑0
vertices do (98.6 % within tolerance where the mode‑0 vertices of the chunk agree to
≥ 95 %).  Renders of a running character (`meso_player_dekker_run_seq.png`) show a
correct run cycle.

### Node tracks

Each compressed node/mesh/light track is `ncomp` (= model frames) entries of two u32
(word 0 first).

**Word 1 — position (solved):** x = bits 0–9, y = bits 10–19, z = bits 20–31 (LSB = bit 0),
`pos = qofs + q × qinv` in model space (nodes with zero range store 0).  Evidence: in all
2407 compressed tracks the decoded positions stay inside the record's
`[pos_min, pos_max]` and reach both bounds (as expected if the exporter derived the range
from the track); `WBOLT_HAND_R` of `MESO_PLAYER.ghb` stays 0.36–0.43 units from the
centroid of the `_RCLOSED` hand vertices in all 28 raw (1‑frame‑chunk) poses.

**Word 0 — rotation (partly solved):**

| Bits | Meaning |
|---|---|
| 0–7 | index into the same 256‑direction table as the vertex normals |
| 8–11, 12–15, 16–19 | three signed 4‑bit values (stored − 8), mostly near 0; each step changes the orientation by about 1° — axes/signs UNKNOWN |
| 20–30 | angle θ = value × 2π/2048 |
| 31 | 0 (2403 of 2407 tracks entirely; UNKNOWN) |

With column vectors (`world = R·local + pos`) the rotation factors as `R = Rx(−θ) · Q`,
where Q depends only on bits 0–19: changing only θ by one step rotates the attached
geometry by 0.176° (= 360°/2048) about model −X.  This was tested model‑free on 10656
frame‑pair combinations built from rigid vertex sets (hands, feet, heads) of the raw
character poses: median error 0.61°, 90 % below 1.46° (the opposite sign gives 12.9°).
For Q, the table direction selected by bits 0–7 matches Qᵀ·x̂ (the local direction of
model X) to a median of 6°; the twist about that direction and the role of the three
4‑bit corrections are UNKNOWN, so node orientations cannot be reconstructed yet.

## Validation

`python tools/formats/ghb.py validate <dir> --deep` over all 498 stock files (parse, static
block, every vertex chunk, every compressed node track): 498 passed, 0 failed (~2 min).
Additional checks run over the whole set:

* part A consumed exactly (`size_a`) and part B tiled exactly by the regions above: 498/498;
* sequences tile the frame range: 498/498; chunks tile it and never cross sequences:
  114/114 files, 15730 chunks; `frame → chunk` table consistent: 114/114;
* static block section sizes match the counts: 495/495 (381 non‑chunked, 114 chunked);
* material groups / surfaces / index lists / corner references consistent: 495/495,
  8103/8103 surfaces;
* normal stream size rule: 15730/15730 chunks;
* vertex position streams (modes 0/1 and raw) decode to the exact stream end with all
  values in range: 15730/15730 chunks (`ghb.decode_position_stream`, ~110 s for the set);
* compressed node tracks: decoded positions inside and spanning `[pos_min, pos_max]`:
  2407/2407 tracks;
* empirical normal table consistency 0.998 (median); 1‑frame chunk normals vs geometry:
  median dot 0.994.

Rendered previews (software rasteriser, textures from the data) in
`openmerc-data/renders/ghoul/`:

* `trashcan_f0.png`, `medkit.png`, `bench_top.png`, `w_assault_rifle.png`, `grenade.png` —
  static models, textures map correctly ("Pitch In" lettering, syringe);
* `trashcan_anim.png`, `side_o_beef_swing1.png` — per‑frame int16 vertices (swinging lid, hanging meat), frames 0/4/8/12 and 0…20;
* `rotor_static.png`, `rotor_start_seq.png` — chunk‑compressed animation, frames 0/10/20/30;
* `searchlight_arm_seq.png` — rigid rotating searchlight arm, frames 0/8/16/24;
* `pistol1_inview_seq.png` — first‑person pistol reload (`RELOAD_A`) frames 14/18/22/26,
  with per‑frame mesh visibility applied;
* `meso_player_dekker_f819.png` (+ `.obj`) — character `MESO_PLAYER.ghb`, frame 819 (last
  frame of `STD_IALERT_N_P_N`, a standing pose), skins and part selection from
  `Dekker.gpm`; `meso_player_dekker_f0.png` (+ `.obj`) — frame 0 (a crouch pose);
* `meso_player_dekker_run_seq.png` — `STD_LRUN_FWD_L_2` frames 1059/1063/1067/1071;
* OBJ exports: `medkit.obj`, `trashcan.obj`, `meso_player_f819.obj` (all meshes, grouped).

There is no separate bind pose in a `.ghb`: every frame is a complete posed mesh.

## Open questions

1. **Vertex record modes 2 and 3** — never used in stock data; hypothesis: reserved for
   other predictors.  Verification only possible with the original engine (black box:
   patch a record header in a copy of a model and observe whether it loads).  Also: the
   exact key frames of the 2/3 normal samples per chunk (see above) — verification: render
   a chunk in game with a normal that flips mid‑chunk and observe when the shading changes.
2. **Rotation word of node tracks, factor Q** (bits 0–19) — known: bits 0–7 select a
   table direction that is the local image of model X (median 6°); unknown: the twist
   about it (residual twists show 90° jumps, hypothesis: a per‑octant or axis‑switching
   reference "up" vector) and the axes of the three 4‑bit corrections.  Verification:
   with ground‑truth relative rotations from rigid vertex sets, test candidate rules via
   `Q(L_j)·Q(L_i)ᵀ ≈ Rx(θ_j)·M·Rx(−θ_i)`; black box: attach a weapon in game in a known
   pose and compare its orientation with the decoded one.
3. Exact normal index table (empirical table known to a few degrees, 6 entries never
   used).  Verification: fit generating rules (geodesic sphere orderings) against the
   empirical table; black box: compare in‑game vertex lighting of a flat test face.
4. Header constants `0x033FB1A3`, 74, 0; count 0 (=1), count 10/12 (=0); `−1` before the
   counts; the always‑empty `0x05DC159D` list; `1/256` vector; sequence `f32[3]`; event
   `i32` (−1).  Hypotheses: version/tag; reserved; root motion; event target.  Verification
   needs a non‑stock file (exporter output) — not available.
5. Material/slot/node flag bytes and the six material properties (correlations listed
   above).  Verification: render the same model in game with lighting changes.
6. Light `type` and the four scalar properties.  Verification: compare the in‑game light
   radius of `NAV_LITE` over its 11 frames with the decoded 100→30 ramp.
7. Geometry count 1 in stripped character variants (118 vs 39 records).

## Progress log

* 2026‑10‑21: header, size fields and part A/B split found (`FLARE.ghb`, 1063 bytes);
  section markers `0x05DC159C…9F`; length‑prefixed strings.
* Sequence records and timed events decoded (`pistol1.ghb`); material records with
  TRACKs found via `EMERGENCY.ghb` (blinking opacity), `FLASHPAK.ghb` (image index),
  `ROTOR_ANIM.ghb` (UV matrix track); node records with visibility bit sets (32‑bit words,
  found via `SNIPERRIFLE.ghb`), `NOBLOCK` user strings, per‑frame matrices
  (`B_WEB_DOORS.ghb`), lights.  Part A parses exactly for 498/498.
* Static block: groups, positions, UVs, normals, surfaces, index‑list grammar, corners;
  per‑frame int16 positions and normal‑index sections for non‑chunked models; part B
  tiling verified 498/498.  Static and int16‑animated models render correctly.
* Chunk header and 43‑byte chunk records; chunk normal stream size rule; mode‑0 vertex
  records solved and validated (rotor, searchlight arm, character vertex 0).
* Character rendered from a raw 1‑frame chunk with `.gpm` skins/parts.
* Correction: per‑frame int16 positions are vertex‑major (found by rigidity test); with
  that the empirical normal table became consistent (0.59 → 0.998).
* Normal‑sample timing per chunk (first / middle / last) estimated from moving normals.
* Node track word 1 (position) solved; word 0 field layout found, outer X angle solved,
  residual rotation Q only partly understood.
* Mode‑1 vertex records (relative to an earlier vertex, sign‑magnitude offset box) solved;
  all 15730 chunks decode exactly; character animation frames render correctly.
* Not solved: rotation factor Q of node tracks, exact normal table, meaning of several
  flags/constants (see Open questions).
