# SPEC-014: Ghoul model files (.ghb) — how the engine reads and decodes them
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (the Ghoul model loader, decompressors and the built-in exporter)
Depends on: SPEC-120 (clean data spec of the `.ghb` container — this spec answers its UNKNOWNs and corrects a few
readings), SPEC-121 (text formats), SPEC-201 Appendix A (250-entry direction table), SPEC-010/011 (interfaces),
SPEC-012 (runtime use of the decoded data)

## Purpose

SPEC-120 describes the `.ghb` layout as far as it can be inferred from the data. This spec gives the engine's
reading of every field, with exact decoding rules, tables and test vectors, so that a reimplementation decodes
the stock files bit-for-bit as the original does:
- header constants;
- flag bytes;
- material properties;
- light parameters;
- node and camera records;
- the geometry header;
- per-sequence animated data;
- vertex-record modes 2/3;
- normal key frames;
- the full node rotation (including the twist and the three 4-bit corrections);
- the 256-entry direction table.

Field names below follow SPEC-120.

## Interface / layout

### 1. Primitive rules
- All integers and floats are little-endian. `STR` = u32 length + bytes without NUL. Length 0 is the empty
  string. A length above 100000 is fatal: `Ghoul :Huge String`.
- Every one-byte flag in part A is a boolean that is **true only when the byte is exactly 1**. Any other value is
  false.
- TRACK encodings (SPEC-120 "Animatable value") as the engine reads them:

  | Kind | Constant form | Per-frame form |
  |---|---|---|
  | bool (visibility) | u8 is_const = 1, u8 value | u8 0, u32 n, ⌈n/32⌉ u32 words, bit i (LSB first) = frame i |
  | u8 (image index) | u8 1, u8 value | u8 0, u32 n, n bytes |
  | f32 | u8 1, f32 | u8 0, u32 n, n × f32 |
  | vec3 | u8 1, f32[3] | u8 0, u32 n, n × f32[3] |
  | MAT | u8 1, MAT | u8 0, u32 n, n × MAT |

  MAT = f32[16] + u32 identity flag (SPEC-120). Per-frame tracks are indexed by **absolute model frame**.
- Part B is read completely into memory when the file is opened. Every "offset in part B" is relative to its start.

### 2. Header
| Offset | Field | Engine behaviour |
|---|---|---|
| 0 | magic (hex 198237FE, bytes `FE 37 82 19`) | other value or unreadable file → the load fails and the console prints `Old file <path>` (not fatal) |
| 4, 8 | size_a, size_b | part B = size_b bytes at file offset size_a |
| 12 | format tag 0x033FB1A3 | any other value is **fatal**: `Ghoul :Not a ghb file <path>` |
| 16 | format version 74 | must be exactly 74. Greater: the load fails silently (and the exporter refuses to overwrite the file); smaller: the load fails silently |
| 20 | "thunk" flag | 0 = normal. 1 = obsolete redirect file: console `Thunks are obsolete <path>`, the file counts as loaded but has **no content** (no sequences). Writers must write 0 |
| 24 | STR exporter source path | display name only (debug reports); no effect on loading |

The exporter's raw per-sequence source files use format tag 0x033FB1A2 with their own version 3. The engine reads
them only in a developer conversion mode (section 11).

### 3. Sequence and event records
| Field | Meaning |
|---|---|
| f32[3] | reserved: always written as (0,0,0) by the exporter. No reader was found; ignore it |
| f32 frame_ms | duration of one frame in milliseconds (SPEC-011/012 timing) |
| STR name | sequence name, upper case in stock data |
| u32 nevents, EVENT[] | 40-byte records in memory: f32 time_ms, i32 reserved (always −1, written by the exporter; ignore), STR type, STR argument |
| u32 first, u32 count | frame range |

Event types are stored lower case (`fire`) and become note names upper-cased (`FIRE`) when a model object
registers them (SPEC-012). Event times are milliseconds from the sequence start, **not** scaled by the instance
speed (SPEC-012).

### 4. Material record (resolves SPEC-120 "u8[6] flags" and the six properties)
| Field | Meaning |
|---|---|
| STR name | upper-cased on load |
| u8 two-sided | 1 = no back-face culling |
| u8 alpha-texture | 1 = the base texture's alpha channel is the surface transparency |
| u32 blend mode | 0 = normal, 1 = additive (destination += source). Any non-zero value makes the material non-opaque |
| u32 slot count | read and **ignored**: exactly two SLOT records always follow |
| SLOT ×2 | slot 0 = base texture, slot 1 = specular texture |
| TRACK vec3 P0 | diffuse colour: the surface colour when slot 0 has no texture |
| TRACK vec3 P1 | specular colour |
| TRACK f32 P2 | glossiness g: specular exponent = 2^(10·g) |
| TRACK f32 P3 | specular level: multiplier of the specular term |
| TRACK f32 P4 | self-illumination: added to the lit colour, all channels |
| TRACK f32 P5 | opacity: multiplied by the instance alpha |

SPEC-120's six "flag bytes" are therefore u8 two-sided, u8 alpha-texture and the four bytes of the u32 blend mode.
The stock patterns (byte0 hats/faceplates, byte1 gore/masks, byte2 EXPLODE/BEAM/…) are two-sided, alpha-texture
and additive.

Classification at load (constant tracks compared; an animated track always counts as "may change"):

| Class | Rule |
|---|---|
| opaque | blend = 0 AND NOT (alpha-texture AND slot 0 has a texture) AND P5 is constant ≥ 0.99 |
| specular-capable | NOT (P2 constant ≤ 0) AND NOT (P3 constant < 0.051) AND NOT (P4 constant ≥ 0.99) |
| textured | some slot has a texture and is not an environment map |

### 5. Material slot record (resolves SPEC-120 SLOT UNKNOWNs)
| Field | Meaning |
|---|---|
| STR texture | texture or image-list name (SPEC-121; lookup rules in SPEC-012) |
| STR reserved | always empty. SPEC-120's "u32 0" is this string's length |
| u8 has-texture | 0 = slot unused (nothing is loaded) |
| u8 environment | 1 = texture coordinates are generated by sphere environment mapping instead of the mesh UVs |
| u8 tile U | 1 = repeat along S, 0 = clamp |
| u8 tile V | 1 = repeat along T, 0 = clamp |
| u8 TRACK | image index into the slot's image list per frame (no blending: one frame's value is used) |
| MAT TRACK | UV transform (texture matrix) |
| u8 static-UV | 1 = the UV transform is constant, so no texture-matrix work is done. It equals the UV track's constant byte in all stock slots |

Stock `01 00 01 01` = textured, no environment map, repeat in both directions. The 0/0 tile slots are ROTOR_DISC,
the tank wheels, ARC_LOGO and the camera lights.

### 6. Bounds record (resolves the "u8[2]" before every box)
The model bounds, the bounds inside every node record, the second box of a mesh record and the geometry
bounds all share one layout: u8 **infinite**, u8 **empty**, f32[3] min, f32[3] max.
- infinite = 1: the box contains everything. Every test passes at distance 0, and adding points has no effect.
- empty = 1: the box contains nothing. Every test fails. The first point added sets min = max = that point and
  clears the flag.
- Stock data: both 0 everywhere.

### 7. Node lists and record kinds
After the model bounds come four lists. Each is a u32 marker (not checked by the reader), a u32 count and the
records. The list a record is in decides its part type (SPEC-010):

| Marker | List | Part type |
|---|---|---|
| 0x05DC159C | nulls ("dummies": bolts, helpers) | 0 |
| 0x05DC159D | cameras (empty in all stock files) | 2 |
| 0x05DC159E | meshes (followed by GEOMETRY when the count is non-zero) | 1 |
| 0x05DC159F | lights | 3 |

**NODE** (common head of all four record kinds; resolves SPEC-120 "u8[5] flags"):

| Field | Meaning |
|---|---|
| STR name | |
| STR user properties | free text from the modelling tool |
| bool TRACK | visibility per frame |
| u8 sprite | billboard: the node turns to face the viewer about its local Y axis |
| u8 spriteball | additionally pitches about its local X axis (spherical billboard) |
| u8 no-block | the node/mesh is ignored by ray and box traces |
| bounds record | the node's position range over all frames (section 6) |
| u32 ncomp + transform | SPEC-120 (uncompressed MAT TRACK, or compressed track parameters) |

The exporter sets these flags from the source scene:
- sprite: the name begins with `sprite` (any case);
- spriteball: the name begins with `spriteball` (this also sets sprite);
- no-block: the user properties contain `NOBLOCK` or `NO_BLOCK`, or the node is a sprite.

Stock patterns are (1,1,1) for SPRITEBALL meshes, (1,0,1) for SPRITE and (0,0,1) for the 13 NOBLOCK records.

**CAMERA** = NODE + four vec3 TRACKs. The exporter writes per frame the position, view direction, up vector and
right vector (= direction × up, normalised), in that order (UNVERIFIED: no camera exists in stock data).

**MESH** = NODE + a second bounds record: the mesh's geometry box over all frames, used for the per-mesh ray
pre-test.

**LIGHT** (corrects SPEC-120, which is misaligned by one byte here):

| Field | Meaning |
|---|---|
| NODE | |
| u8 world | 1 = "world light" (the exporter sets it when the name begins with `world`, any case; true for exactly the 26 stock lights named `WORLD_*`) |
| u32 kind | 0 omni, 1 target spot, 2 directional, 3 free spot, 4 target directional (all stock lights: 0). A kind above 4 gives no light |
| TRACK vec3 | position |
| TRACK vec3 | colour |
| TRACK f32 | brightness = length of the colour vector (written by the exporter) |
| kind 0: TRACK f32 ×3 | multiplier, attenuation start, attenuation end |
| kinds 1–4 | further tracks (cone/hotspot angles, target, aspect); not used by stock data, not specified (UNVERIFIED) |

SPEC-120's "type 0/1, u8 0" is this world byte plus the low byte of the kind. Its example "1.667, 1.0, 0.0, 150.0"
is brightness, multiplier, start and end. `NAV_LITE` animates start and end together, 100 → 30. A light is
**active** in a frame only when brightness ≥ 0.01 and multiplier ≥ 0.01. Lighting formulas and the use of the world
flag are in SPEC-012.

### 8. GEOMETRY header (corrects SPEC-120 "−1", counts 0/10/12 and the 1/256 vector)
| Field | Meaning |
|---|---|
| i32 first word | −1 = extended header follows. Any other value is the mesh count itself (older layout: no revision flag, no chunked byte) |
| u32 revision flag | only with −1; 1 in all files (SPEC-120 "count 0") |
| u32 mesh count | SPEC-120 count 1. It may exceed the number of MESH records (stripped variants keep the source count). Surface mesh indexes always index the MESH records |
| u32 material count, n_pos, n_uv, n_norm, n_surf, n_idx, n_corner, n_apos | as SPEC-120 |
| u32 n_atex | number of **animated texture coordinates** (SPEC-120 count 10; 0 in stock data) |
| u32 n_anorm | as SPEC-120 |
| u32[10] section offsets | relative to the static block, in this order: material groups, positions, uvs, normals, surfaces, index lists, corners, animated positions, animated uvs, animated normals. The first is SPEC-120's "count 12", always 0. Section k ends where k+1 starts; the last ends at static_size |
| bounds record | geometry bounds |
| f32[3] vscale, f32[3] vinv, f32[3] center | int16 position quantisation |
| f32[3] atex scale | dequantisation scale of animated texture coordinates: u = int16 × scale.x, v = int16 × scale.y (1/256 in all files, i.e. 8.8 fixed point) |
| u8 chunked (+ chunk header) | present only when the revision flag is 1 |
| u32 static_size, u32 static_offset | as SPEC-120 |

- vscale (exporter) = 65520 / max(extent, 650) per axis, extent = geometry box max − min. This gives 100.8 whenever
  the extent is ≤ 650. The exporter stores int16 = trunc((p − center) × vscale), truncating toward zero.
- Position = center + int16 × vinv.
- A corner UV reference ≥ 0 selects an animated texture coordinate (never in stock data). The exporter stores
  trunc(uv × 256) clamped to ±32767, with the console warning `Out of range texture coordinates`.

### 9. Per-frame data of non-chunked models (corrects SPEC-120 section 6/8 wording)
Animated positions, animated uvs and normal indices are stored **per sequence**. For each sequence s in file
order there is one block of n_apos × F_s int16[3] positions, where F_s is that sequence's frame count. The block is
vertex-major: element v, frame f of the sequence is at block_s + (v·F_s + (f − first_s)) × 6 bytes. Blocks follow
each other in sequence order. Animated uvs are int16[2] per (entry, frame), and normals one u8 index per
(normal, frame), with the same per-sequence, element-major blocks.

In the stock data every non-chunked animated model has exactly one sequence (18 files), so SPEC-120's "all frames
of vertex 0, then vertex 1" is the same thing there. Files with several sequences need the per-sequence rule.

### 10. Chunked vertex animation (completes SPEC-120 "Vertex animation chunks")
**Frame → chunk:** chunk = frame table[absolute model frame]; frame within chunk f = absolute − chunk.first_frame.
A whole chunk is decoded at once (SPEC-012 cache).

**Bit reading:** a field of k bits at stream bit p is the little-endian u32 at byte ⌊p/8⌋, shifted right by p mod 8
and masked to k bits. This requires k ≤ 25; the 4 padding bytes keep the read inside the data. k = 0 reads
nothing and gives 0.

**n = 1 (raw):** n_apos records of (x: bits[0], y: bits[1], z: bits[2]); position = offset + q × scale per axis.

**n ≥ 2:** a reference width R is kept for the chunk, starting at 1. Before reading vertex v, if 2^R ≤ v − 1 then
R increases by 1, so R = the bit length of v − 1, minimum 1. Each vertex record begins with a 2-bit mode:
- **Mode 0 (absolute):** per axis a = x, y, z: a start value (bits[a] bits) then w[a] (4 bits). Frame 0 = the
  start values. For each later frame, per axis with w[a] ≠ 0: read d (w[a] + 1 bits); value += d − 2^w[a].
  Positions = offset + value × scale.
- **Any other mode (1, 2 or 3) — relative:** ref (R bits), M (4 bits). Then per axis: w[a] (4 bits), then, only if
  M ≠ 0, an M-bit magnitude followed by one sign bit (1 = negative), which gives min[a] (0 when M = 0). Then for
  every frame 0…n−1, per axis with w[a] ≠ 0: u (w[a] bits). Position[v][f][a] =
  position[ref][f][a] + (min[a] + u) × scale[a]. This is computed in floating point from the already decoded
  reference position. There is no check that ref < v.

  **Modes 2 and 3 are decoded exactly like mode 1** (SPEC-120 open question 1). The engine's exporter only ever
  writes 0 and 1.
- The position stream ends byte-aligned plus 4 padding bytes.

**Normal stream:** k key samples per normal (k = 1 for n = 1, 2 for n = 2…4, 3 for n ≥ 5), stored normal-major,
then 4 padding bytes. The keys are direction-table indices (section 13) of these chunk frames:

| n | key frames |
|---|---|
| 1 | 0 |
| 2…4 | 0, n−1 |
| ≥ 5 | 0, ⌊n/2⌋, n−1 |

Reconstruction of frame f, with table vectors t0, t1 (and t2) and h = ⌊n/2⌋:

| n | Normal of frame f |
|---|---|
| 1, 2 | t_f |
| 3, 4 | t0 + f·(t1 − t0)/(n − 1) |
| ≥ 5, f ≤ h | t0 + f·(t1 − t0)/h |
| ≥ 5, f > h | t1 + (t1 − t0)/h + (f − h − 1)·(t2 − t1)/(n − 1 − h) |

The last row is an original quirk: the decoder takes one more step along the first segment before switching to
the second. Frame n−1 is therefore not exactly t2; over the stock data the median deviation is 7.1°, the 90th
percentile 23.8°. Reconstructed normals are **not renormalised**.

**Exporter parameters** (informative; needed only to write files identical to the original exporter):
- Chunks are runs of 8 frames starting at each sequence's first frame (the last run of a sequence is shorter), so
  they never cross sequences.
- Per chunk and axis: extent = max − min over all frames and vertices of the chunk, raised to at least 0.05.
  bits = the smallest b in 0…15 with extent/0.05 ≤ 2^b, capped at 15. qf = (2^b − 1.01)/extent. Stored
  scale = 1/qf, offset = min. q = trunc((p − min)·qf), clamped to 0…2^b − 1.
- Mode choice for n ≥ 2, where bits(x) = the smallest b in 0…15 with x < 2^b:
  - c[a] = bits(largest |frame-to-frame delta|);
  - reference r = the earlier vertex (first minimum wins) minimising Σ_a bits(max − min over frames of q[v][f] − q[r][f]);
  - M = max_a bits(|min_a|);
  - mode 1 is chosen when 3M + 19 + R + n·Σw < (n − 1)·Σ_a (c[a] + [c[a] ≠ 0]) + Σ bits[a] + 12.
- A normal key is the table index with the largest dot product: scan 0…255, strictly greater wins, start value 0,
  so the result is index 0 when no dot product is positive.
- An exporter buffer overflow is fatal: `Ghoul :Compression overflowed`.

### 11. Developer conversion mode (informative)
`gserver AUTOPREP` (off by default; SPEC-012) makes the loader prefer the exporter source files:
- `*.ghl`: one raw sequence per file;
- `*.gfl`: a text list of `.ghl` files with optional `exclude`/`only` sections.

The engine merges them, compresses them and writes `.ghb` plus a text report `<name>.gin`, printing
`GFL Preprocessing <path>` / `Preprocessing <path>`.

Merge rules: nodes of later sequences are matched to the first sequence by name and type, and a missing node is
added with `<Dummy|Mesh|Camera|Light> <name> is not present in the FIRST sequence`. These conditions are fatal:
- `Name collision, two (possibly hidden) objects named <name>.` (one name used for two node types);
- `Mesh <name> does not match up for …` (different vertex/uv/face/material counts);
- `Mesh with no vertices`;
- `Material layout change in sequence`.

The index lists are made by a greedy stripifier:
- longest strip or fan of at least 4 corners: +n strip / −n fan;
- the rest: the trailing `0, k` triangle list.

Not needed for stock data; a reimplementation may ignore the source formats.

### 12. Node transform tracks (resolves SPEC-120 open question 2)
**Uncompressed** (ncomp = 0): MAT TRACK; constant → that matrix.

**Compressed** (ncomp = frames): per frame two u32 words. **Word 1** (position) is as SPEC-120: x = bits 0–9,
y = 10–19, z = 20–31, position = qofs + q × qinv.
- Exporter: range per axis = max − min, replaced by 1.0 when below 1.0; qscale = (1023, 1023, 4095)/range;
  q = trunc((p − min) × qscale), clamped.

**Word 0** = packed rotation. It is the **same encoding as the network transform** (SPEC-201 B.6), with one
decoder for both. Let T[i] be the direction table (section 13):

1. t = T[bits 0–7].
2. a = normalise(t + c), with c = (q_x, q_y, q_z) × 0.018666666 − 0.14 for q_x = bits 8–11, q_y = bits 12–15,
   q_z = bits 16–19 (each 0…15). a is the zero vector if its length is ≤ 1e-10. There is no exact zero correction:
   q = 7 gives −0.0093333 and q = 8 gives +0.0093333.
3. p = t × X. Replace it by t × Y if |t × Y| is strictly larger, then by t × Z if |t × Z| is strictly larger
   (X = (1,0,0), Y = (0,1,0), Z = (0,0,1)). This uses the **uncorrected** t and is not normalised.
4. b = normalise(p × a); c′ = normalise(a × b).
5. θ = (bits 20–30) × 2π/2047. Note **2047**, not 2048 as SPEC-120 assumed. d = normalise(b·cos θ + c′·sin θ).
6. e = a × d; if bit 31 is set, e = −e; e = normalise(e). Bit 31 marks mirrored (left-handed) transforms; it is
   set in 4 stock tracks.
7. The 3×3 part of the 68-byte matrix (SPEC-011 layout, row-vector convention) gets a, d, e as its **columns**
   0, 1, 2: element[r][0] = a_r, element[r][1] = d_r, element[r][2] = e_r. Row 3 = the decoded position. The
   identity flag is cleared.

In column-vector terms the rotation matrix has rows a, d, e, which equals Rx(−θ)·[a; b; c′]. This is SPEC-120's
empirical finding R = Rx(−θ)·Q, with Q fully determined now: bits 0–7 give the table direction, bits 8–19 correct
it, and the twist reference is the axis perpendicular chosen in step 3.

**Encoder** (exporter and network writer). Input matrix M (row-vector layout):
1. Column 0 is normalised.
2. index = the first table entry with the largest dot product (strictly greater replaces; start value 0 at index 0).
3. q_k = floor((a_k − t_k + 0.14) × 53.57143 + 0.5), clamped to 0…15. The corrected a is then recomputed from the
   quantised q exactly as the decoder does.
4. b and c′ are built as in decoder steps 3–4.
5. roll = floor(atan2(col1·c′, col1·b) × 2047/2π), with the angle first wrapped to 0…2π. It is truncated, not
   rounded, and clamped to 0…2047.
6. Bit 31 is set when (normalise(a × col1))·col2 < 0.

### 13. Direction table (normals and rotations; resolves SPEC-120 open question 3)

> OpenMerc note (2026-10-26, user decision): OpenMerc does not ship the original's table. Indices 6 ... 255 are the
> 250 directions of a frequency-5 geodesic icosahedron without its poles, generated at start in the same order
> (docs/impl/LEGAL-tables.md); they agree with the original to within 1e-6 but are not bit-identical, so the
> SHA-256 below describes the original only. Model data stores indices, so models are unaffected.
256 unit vectors, built before any model is loaded:

| Index | Vector |
|---|---|
| 0 | (+1, 0, 0) |
| 1 | (−1, 0, 0) |
| 2 | (0, +1, 0) |
| 3 | (0, −1, 0) |
| 4 | (0, 0, +1) |
| 5 | (0, 0, −1) |
| 6 … 255 | SPEC-201 Appendix A entries 0 … 249, in order, bit-identical (the raw float bits there are normative) |

The axes are exact ±1.0/0.0. SHA-256 of the 3072-byte table (256 × 3 little-endian f32, index order) =
`3362bacbb561fe2c394b7413fa17d0b9facc93b58c165a65e90b1211fb9cf86f`. SPEC-120's empirical estimate agrees: 6 =
(0.21, 0, 0.98) is Appendix A entry 0 = (0.219625, 0, 0.975584). Its six unused indices are simply directions no
stock normal is nearest to.

### 14. Index lists, corners, surfaces
SPEC-120's grammar, triangle orientation and corner reference rules (negative = static element ~v) are what the
engine uses. Surface mesh indexes refer to MESH records. The material-group section has three u32 per material
(material, first surface, count).

## Behaviour

- **Evaluation of tracks at a pose.** Every per-frame value is evaluated at a frame pair (A, B, weight w of A;
  SPEC-012 gives how playback produces it):
  - f32, vec3 and UV-matrix values: B + (A − B)·w, element by element, no re-orthonormalisation. A UV-matrix
    track is blended only when A and B are adjacent frames of the same sequence; otherwise A's value is used.
  - bool and u8 tracks (visibility, image index): the value of the dominant frame (SPEC-012).
  - Node transforms:
    - compressed: both rotation words are decoded and the nine rotation elements interpolated linearly (no
      re-orthonormalisation); the position is interpolated in quantised units and then dequantised;
    - uncompressed: all 16 elements are interpolated, and two identity frames give the identity;
    - constant: the stored matrix.

    The identity flag is then recomputed with tolerance 0.0001.
- **Vertex positions and normals** of the two frames are blended linearly (SPEC-012). Normals are not renormalised.
- **Loading failures** (missing file, wrong magic, version ≠ 74) are non-fatal: the sequence registration
  returns 0 (SPEC-010). A wrong tag, an oversize string and the exporter-only errors are fatal.

## Constants
| Name | Value |
|---|---|
| Magic / tag / version | hex 198237FE / hex 033FB1A3 / 74 (exporter source files: tag hex 033FB1A2, version 3) |
| Maximum string length | 100000 bytes |
| Boolean true | byte value 1 only |
| Light kinds | 0 omni, 1 target spot, 2 directional, 3 free spot, 4 target directional |
| Light active thresholds | brightness ≥ 0.01, multiplier ≥ 0.01 |
| Material thresholds | opacity 0.99; specular level 0.051; self-illumination 0.99; glossiness exponent base 2^(10g) |
| Position quantisation (exporter) | vscale = 65520/max(extent, 650) |
| Animated UV scale | 1/256 (8.8 fixed point) |
| Chunk length (exporter) | ≤ 8 frames, restarting at each sequence |
| Chunk extent minimum / step | 0.05 / 0.05; bits ≤ 15; factor (2^b − 1.01)/extent |
| Rotation correction | q × 0.018666666 − 0.14 (encoder ×53.57143, +0.5, floor) |
| Rotation angle step | 2π/2047 |
| Node position bits | 10 / 10 / 12 (x / y / z) |
| Node position range floor (exporter) | 1.0 |
| Direction table | 256 entries, SHA-256 above |
| Vector length zero threshold | 1e-10 |
| Identity tolerance | 0.0001 |

## Test vectors / how to verify

1. **Rotation decode.** Word → 3×3 rows (row-vector layout; double precision, single precision within 1e-6):

   | Word | row 0 / row 1 / row 2 |
   |---|---|
   | 00000000 | 0.974508 0.160676 0.156580 / −0.158641 0.987007 −0.025490 / −0.158641 0.000000 0.987336 |
   | 00088800 | 0.999915 −0.009247 −0.009246 / 0.009246 0.999957 −0.000085 / 0.009246 0.000000 0.999957 |
   | 00188800 | 0.999915 −0.009275 −0.009217 / 0.009246 0.999952 −0.003155 / 0.009246 0.003069 0.999953 |
   | 80088800 | 0.999915 −0.009247 0.009246 / 0.009246 0.999957 0.000085 / 0.009246 0.000000 −0.999957 |
   | 00088804 | 0.009246 0.999957 −0.000085 / 0.009246 0.000000 0.999957 / 0.999915 −0.009247 −0.009246 |
   | 00088802 | 0.009246 0.999957 0.000085 / 0.999915 −0.009247 0.009246 / 0.009246 0.000000 −0.999957 |
   | 40088800 | 0.999915 0.009261 0.009232 / 0.009246 −0.999956 0.001620 / 0.009246 −0.001535 −0.999956 |
   | 1ff7a9c3 | −0.168538 −0.985485 −0.020335 / −0.981041 0.169709 −0.093583 / 0.095675 0.004177 −0.995404 |

2. **Stock-data node track.** `ghoul/enemy/meso/MESO_PLAYER.ghb`, node `WBOLT_HAND_R`:
   - frame 0: words (hex 3FF88605, 9F65E962) → position (−7.9463, −14.13325, 18.97554), rows
     (−0.02825 −0.9996 −0.00127 / 0.00942 −0.00153 0.99995 / −0.99956 0.02824 0.00946);
   - frame 819: words (hex 40687805, 74B5B548) → position (−9.88696, −15.89471, 3.1862), rows
     (0.00942 −0.99975 0.02004 / −0.00942 0.01995 0.99976 / −0.99991 −0.00961 −0.00923).

   Rigidity check: the 12 vertices of mesh `_RCLOSED` expressed in this bolt's frame stay constant over 80
   sampled frames. The median standard deviation is 0.084 units. Decoding with the corrected direction for the
   perpendicular, with the axis order Y, Z, X, or without the corrections gives 0.18–0.67.
3. **Position stream** (n_apos 3, n 3, bits (4,3,0), scale (0.5,1,1), offset (10,20,30)). Stream bytes
   `54 08 E0 22 51 10 C0 06` + 4 padding bytes. Vertex 0 is mode 0, vertex 1 mode 1 (ref 0, M 2, x min −1, y min
   +2), vertex 2 **mode 2** (ref 1, M 0, all w 0); 75 bits are consumed. Result:
   - v0: (12.5,22,30), (13,22,30), (12.5,22,30);
   - v1: (12,24,30), (13,24,30), (12.5,24,30);
   - v2 = v1.

   Raw 1-frame chunk, bits (4,3,0), bytes `7F 00` → v0 (17.5,27,30), v1 (10,20,30).
4. **Normal reconstruction.**
   - n = 5, keys (+Z, +X, +Y): f0 (0,0,1), f1 (.5,0,.5), f2 (1,0,0), f3 (1.5,0,−.5), f4 (1,.5,−.5).
   - n = 8, same keys: f4 (1,0,0), f5 (1.25,0,−.25), f6 (.9167,.3333,−.25), f7 (.5833,.6667,−.25).
   - n = 4, keys (+Z, +X): (0,0,1), (.3333,0,.6667), (.6667,0,.3333), (1,0,0).
5. **Header** of `FLARE.ghb`: `FE 37 82 19 | size_a | size_b | A3 B1 3F 03 | 4A 00 00 00 | 00 00 00 00 | STR …FLARE.GHL`.
6. **Lights.**
   - Every stock light named `WORLD*`: the byte after its NODE fields is `01`, followed by `00 00 00 00`. Every
     other light: `00 | 00 00 00 00`.
   - In all 90 lights of pak0 the brightness track equals the colour's length, frame by frame (e.g. colour
     (0.796, 0.345, 0.345) → 0.934).
7. **Materials and slots.**
   - Material bytes: EXPLODE-type `00 00 01 00 00 00` (additive); glasses `01 01 00 00 00 00` (two-sided +
     alpha-texture).
   - Slots (pak0, 2060 slots): 993 are `01 00 01 01` with a trailing 1 and a constant UV track; all 15 slots with an
     animated UV track have trailing byte 0.
8. **Direction table:** hash the table built from section 13 and compare with the SHA-256 above.
9. **In game.**
   - A model whose chunk normals change strongly within 8 frames shows a shading jump at the chunk boundary,
     because of the normal quirk of section 10.
   - A copy of a model with one vertex record's mode changed from 1 to 2 renders identically.

## Open questions
- Parameters of light kinds 1–4 (no stock data).
- Camera track order (no stock data; exporter evidence only).
- Readers of the sequence f32[3] and the event i32 (none found; the exporter writes 0 and −1).
- Version-skew details of the older geometry layout (first word ≠ −1); no stock file uses it.

## Amendments to SPEC-120 (proposed)
- "u8[6] material flags" → u8 two-sided, u8 alpha-texture, u32 blend mode; "nslots" is ignored and 2 slots are
  always read; the six properties are named as in section 4.
- SLOT "u32 0" → empty STR; the u8[4] are has-texture, environment, tile U, tile V; the trailing u8 is the static-UV
  flag.
- NODE "u8[5] flags + pos min/max" → sprite, spriteball, no-block + bounds record (infinite, empty, min, max).
  The same (infinite, empty) pair explains every other "u8[2] (0,0)".
- The always-empty 0x05DC159D list is the camera list. Lights: u8 world + u32 kind (SPEC-120's type/u8 are shifted by
  one byte).
- GEOMETRY: count 0 = revision flag, count 10 = animated-UV count, count 12 = first of ten section offsets; the
  1/256 vector = animated-UV scale; per-frame data is stored per sequence.
- The node rotation angle step is 2π/2047; the rotation is fully specified in section 12.
- Vertex-record modes 2/3 decode as mode 1.
